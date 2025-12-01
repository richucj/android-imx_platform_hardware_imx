/*
 * Copyright (C) 2022 The Android Open Source Project
 * Copyright 2024-2025 NXP
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "EvsLibcameraCamera.h"

#include <libcamera/libcamera.h>

#include <aidl/android/hardware/graphics/common/HardwareBufferDescription.h>
#include <aidlcommonsupport/NativeHandle.h>
#include <android-base/logging.h>
#include <android-base/unique_fd.h>
#include <ui/GraphicBufferAllocator.h>
#include <ui/GraphicBufferMapper.h>
#include <utils/SystemClock.h>

#include <sys/stat.h>
#include <sys/types.h>

// #include "VendorTags.h"

namespace {

using ::aidl::android::hardware::graphics::common::BufferUsage;
using ::aidl::android::hardware::graphics::common::HardwareBufferDescription;
using ::android::base::Error;
using ::android::base::Result;
using ::ndk::ScopedAStatus;

// Default camera output image resolution
constexpr std::array<int32_t, 2> kDefaultResolution = {640, 480};

// Arbitrary limit on number of graphics buffers allowed to be allocated
// Safeguards against unreasonable resource consumption and provides a testable limit
constexpr unsigned kMaxBuffersInFlight = 100;

}  // namespace

namespace aidl::android::hardware::automotive::evs::implementation {

#define fourcc_code(a, b, c, d) ((__u32)(a) | ((__u32)(b) << 8) | \
                       ((__u32)(c) << 16) | ((__u32)(d) << 24))

libcamera::PixelFormat EvsV4lCamera::AidlFromat2PixelFormat(AidlPixelFormat AidlFmt) {
    int v4lFormat = -1;
    switch (AidlFmt) {
        case AidlPixelFormat::RGBA_8888:
            v4lFormat = fourcc_code('A', 'B', '2', '4');
            break;
        case AidlPixelFormat::RGB_888:
            v4lFormat = fourcc_code('B', 'G', '2', '4'); // This DRM_FORMAT_BGR888 is the only format recognized by libcamera as RGB888 at the moment.
                                                         // TODO: Fix this in libcamera::PixelFormat().
            break;
        default:
            ALOGE("%s: unsupported AidlFromat 0x%x", __func__, AidlFmt);
            break;
    }
    return libcamera::PixelFormat(v4lFormat);
}

EvsV4lCamera::AidlPixelFormat EvsV4lCamera::formatV4l2ToAidl(int v4lFormat) {
    AidlPixelFormat format = AidlPixelFormat::UNSPECIFIED;

    switch (v4lFormat) {
        case fourcc_code('A', 'B', '2', '4'):
            format = AidlPixelFormat::RGBA_8888;
            break;
        case fourcc_code('B', 'G', '2', '4'): // DRM_FORMAT_BGR888
        case fourcc_code('R', 'G', 'B', '3'):
            // TODO: Check this when Libcamera updates. Currently libcamera + neo ISP validate(fourcc_code('B', 'G', '2', '4')) returns fourcc_code('R', 'G', 'B', '3').
            format = AidlPixelFormat::RGB_888;
            break;
        default:
            ALOGE("%s: unsupported V4L2Fromat 0x%x", __func__, v4lFormat);
            break;
    }
    return format;
}

unsigned EvsV4lCamera::decreaseAvailableFrames_Locked(unsigned numToRemove) {
    unsigned removed = 0;

    for (std::vector<std::unique_ptr<EvsV4lCamera::FrameBuffer>>::iterator buf_itr = mAllocatedBuffers_.begin();
            buf_itr != mAllocatedBuffers_.end();
            /* `++map_pair` - not iterating here as `erase()` bellow iterates.*/ )
    {
        std::unique_ptr<EvsV4lCamera::FrameBuffer> &a = *buf_itr;
        libcamera::Request *request = a->request();

        if (request && request->hasPendingBuffers()) {
            ++buf_itr;  // Frame buffer is owned by Libcamera thus cannot be removed. Try to free next one.
            continue;
        } else {
            buf_itr = mAllocatedBuffers_.erase(buf_itr); // Buffer destructor calls gralloc::free() for underlying memory.
            --mFramesAllowed;
            ++removed;
            if (removed == numToRemove) {
                break;
            }
        }
    }
    return 0;
}

EvsV4lCamera::EvsV4lCamera(std::shared_ptr<libcamera::Camera> libcamera,
                           std::unique_ptr<ConfigManager::CameraInfo>& camInfo) :
    mCamera(libcamera), mState(AVAILABLE), mFramesAllowed(0), mFramesInUse(0), mDropFrames(0), mCameraInfo(camInfo), mFrameCounter(0) {
    LOG(DEBUG) << "EvsV4lCamera instantiated";

    mDescription.id = libcamera->id().c_str();
    if (camInfo) {
        uint8_t* ptr = reinterpret_cast<uint8_t*>(camInfo->characteristics);
        const size_t len = get_camera_metadata_size(camInfo->characteristics);
        mDescription.metadata.insert(mDescription.metadata.end(), ptr, ptr + len);
    }
    // Default output buffer format.
    mFormat = AidlPixelFormat::RGBA_8888;
    // How we expect to use the gralloc buffers we'll exchange with our client
    mUsage = GRALLOC_USAGE_HW_TEXTURE | GRALLOC_USAGE_HW_CAMERA_WRITE | GRALLOC_USAGE_PRIVATE_3 | GRALLOC_USAGE_SW_READ_OFTEN; // Mali gralloc() refuses to allocate buffer if there are too few flags.
}

EvsV4lCamera::~EvsV4lCamera() {
    LOG(DEBUG) << "EvsV4lCamera being destroyed";
    shutdown();
}

void EvsV4lCamera::shutdown() {
    // EVS manager calls this if new client for the same camera appears.
    LOG(DEBUG) << "EvsV4lCamera shutdown";
    if ((mState != RUNNING) && (mState != STOPPING)) {
        LOG(INFO) << "shutdown called when not running";
    }

    // Libcamera RUNNING -> STOPPING
    stopVideoStream();
    mRequests.clear(); // TODO: Any cleanup with requests expected??

    // Drop all the graphics buffers we've been using
    mFramesAllowed = 0;
    mAllocatedBuffers_.clear(); // Buffer destructor calls gralloc::free() for underlying memory.
    mStride = 0;
    // Note:  Since stopVideoStream is blocking, no other threads can now be running

    // Libcamera CONFIGURED -> AVAILABLE
    // Close our video capture device
    mCamera->requestCompleted.disconnect();
    mCamera->release();
    mLibCameraStream = NULL;
    mLibcameraCamCfg = NULL;

    mState = AVAILABLE;
}

// Methods from ::aidl::android::hardware::automotive::evs::IEvsCamera follow.
ScopedAStatus EvsV4lCamera::getCameraInfo(CameraDesc* _aidl_return) {
    LOG(DEBUG) << __FUNCTION__;

    // Send back our self description
    *_aidl_return = mDescription;
    return ScopedAStatus::ok();
}

ScopedAStatus EvsV4lCamera::setMaxFramesInFlight(int32_t bufferCount) {
    LOG(DEBUG) << __FUNCTION__; // Called 2nd - right after create().
    std::lock_guard<std::mutex> lock(mAccessLock);

    // If we've been displaced by another owner of the camera, then we can't do anything else
    if (mState != CONFIGURED) {
        LOG(WARNING) << "Ignoring setMaxFramesInFlight call when camera has been lost.";
        return ScopedAStatus::fromServiceSpecificError(static_cast<int>(EvsResult::OWNERSHIP_LOST));
    }

    // We cannot function without at least one video buffer to send data
    if (bufferCount < 1) {
        LOG(ERROR) << "Ignoring setMaxFramesInFlight with less than one buffer requested";
        return ScopedAStatus::fromServiceSpecificError(static_cast<int>(EvsResult::INVALID_ARG));
    }

    // Update our internal state
    if (setAvailableFrames_Locked(bufferCount)) {
        return ScopedAStatus::ok();
    } else {
        return ScopedAStatus::fromServiceSpecificError(static_cast<int>(EvsResult::BUFFER_NOT_AVAILABLE));
    }
}

ScopedAStatus EvsV4lCamera::startVideoStream(const std::shared_ptr<IEvsCameraStream>& client) {
    LOG(DEBUG) << __FUNCTION__; // Called 3rd - after set max in flight.
    std::lock_guard<std::mutex> lock(mAccessLock);

    // If we've been displaced by another owner of the camera, then we can't do anything else
    if (mState != CONFIGURED) {
        LOG(WARNING) << "Ignoring startVideoStream call when camera has been lost.";
        return ScopedAStatus::fromServiceSpecificError(static_cast<int>(EvsResult::OWNERSHIP_LOST));
    }

    if (mEvsStreamClient) {
        LOG(ERROR) << "Ignoring startVideoStream call when a stream is already running.";
        return ScopedAStatus::fromServiceSpecificError(
                   static_cast<int>(EvsResult::STREAM_ALREADY_RUNNING));
    }

    // If the client never indicated otherwise, configure ourselves for a single streaming buffer.
    if (mFramesAllowed < 1) {
        if (!setAvailableFrames_Locked(1)) {
            LOG(ERROR) << "Failed to start stream because we couldn't get a graphics buffer";
            return ScopedAStatus::fromServiceSpecificError(
                       static_cast<int>(EvsResult::BUFFER_NOT_AVAILABLE));
        }
    }

    // Record the user's callback for use when we have a frame ready.
    mEvsStreamClient = client;

    // Set up the video stream with a callback to our member function forwardFrame().
    mCamera->requestCompleted.connect(this, &EvsV4lCamera::requestComplete);

    // Start the camera.
    auto status = mCamera->start();
    if (status) {
        mEvsStreamClient = nullptr;
        LOG(ERROR) << "Underlying camera start stream failed." << (status == EACCES) ? " Camera is not in startable state." : " Camera device not available.";
        // I don't call shutdown() because it gets called automatically by ~EvsV4lCamera().
        return ScopedAStatus::fromServiceSpecificError(static_cast<int>(EvsResult::UNDERLYING_SERVICE_ERROR));
    }

    mState = RUNNING;
    mDropFrames = 2;
    // Give framebuffers to te libcamera.
    for (auto &buffer : mAllocatedBuffers_) {
        queueBufferToCamera(buffer);

    }
    return ScopedAStatus::ok();
}

ScopedAStatus EvsV4lCamera::stopVideoStream() {
    LOG(DEBUG) << __FUNCTION__;

    // Tell the capture device to stop (and block until it does)
    int ret = mCamera->stop();
    if (ret) {
        ALOGW("%s: Failed to stop camera", __func__);
    }
    mState = STOPPING;

    /* If still has on-fly requests from map_frame_request, wait to finish */
    int i = 20;
    while (mFramesQueued > 0 && i) {
        LOG(ERROR) << "Still has requests to process. Wait 1s. In queue: " << mFramesQueued;
        sleep(1);
        --i;
    }
    if (mEvsStreamClient) {
        std::unique_lock<std::mutex> lock(mAccessLock);

        EvsEventDesc event;
        event.aType = EvsEventType::STREAM_STOPPED;
        auto result = mEvsStreamClient->notify(event);
        if (!result.isOk()) {
            LOG(WARNING) << "Error delivering end of stream event";
        }
        // Drop our reference to the client's stream receiver
        mEvsStreamClient = nullptr;
    }
    mState = CONFIGURED;

    return ScopedAStatus::ok();
}

ScopedAStatus EvsV4lCamera::getPhysicalCameraInfo([[maybe_unused]] const std::string& id,
        CameraDesc* _aidl_return) {
    LOG(DEBUG) << __FUNCTION__;

    // This method works exactly same as getCameraInfo_1_1() in EVS HW module.
    *_aidl_return = mDescription;
    return ScopedAStatus::ok();
}

ScopedAStatus EvsV4lCamera::doneWithFrame(const std::vector<BufferDesc>& buffers) {
    LOG(DEBUG) << __FUNCTION__;

    for (const auto& buffer : buffers) {
        doneWithFrame_impl(buffer);
    }

    return ScopedAStatus::ok();
}

ScopedAStatus EvsV4lCamera::pauseVideoStream() {
    return ScopedAStatus::fromServiceSpecificError(static_cast<int>(EvsResult::NOT_SUPPORTED));
}

ScopedAStatus EvsV4lCamera::resumeVideoStream() {
    return ScopedAStatus::fromServiceSpecificError(static_cast<int>(EvsResult::NOT_SUPPORTED));
}

ScopedAStatus EvsV4lCamera::setPrimaryClient() {
    /* Because EVS HW module reference implementation expects a single client at
     * a time, this returns a success code always.
     */
    return ScopedAStatus::ok();
}

ScopedAStatus EvsV4lCamera::forcePrimaryClient(const std::shared_ptr<IEvsDisplay>&) {
    /* Because EVS HW module reference implementation expects a single client at
     * a time, this returns a success code always.
     */
    return ScopedAStatus::ok();
}

ScopedAStatus EvsV4lCamera::unsetPrimaryClient() {
    /* Because EVS HW module reference implementation expects a single client at
     * a time, there is no chance that this is called by the secondary client and
     * therefore returns a success code always.
     */
    return ScopedAStatus::ok();
}

ScopedAStatus EvsV4lCamera::getParameterList(std::vector<CameraParam>* _aidl_return) {
    if (mCameraInfo) {
        _aidl_return->resize(mCameraInfo->controls.size());
        auto idx = 0;
        for (auto& [name, range] : mCameraInfo->controls) {
            (*_aidl_return)[idx++] = name;
        }
    }

    return ScopedAStatus::ok();
}

ScopedAStatus EvsV4lCamera::getIntParameterRange(CameraParam id, ParameterRange* _aidl_return) {
    if (!mCameraInfo) {
        return ScopedAStatus::fromServiceSpecificError(static_cast<int>(EvsResult::NOT_SUPPORTED));
    }

    auto it = mCameraInfo->controls.find(id);
    if (it == mCameraInfo->controls.end()) {
        return ScopedAStatus::fromServiceSpecificError(static_cast<int>(EvsResult::NOT_SUPPORTED));
    }

    _aidl_return->min = std::get<0>(it->second);
    _aidl_return->max = std::get<1>(it->second);
    _aidl_return->step = std::get<2>(it->second);

    return ScopedAStatus::ok();
}

ScopedAStatus EvsV4lCamera::setIntParameter(CameraParam id, int32_t value, std::vector<int32_t>* effectiveValue) {
    return ScopedAStatus::fromServiceSpecificError(static_cast<int>(EvsResult::NOT_SUPPORTED));
}

ScopedAStatus EvsV4lCamera::getIntParameter(CameraParam id, std::vector<int32_t>* value) {
    return ScopedAStatus::fromServiceSpecificError(static_cast<int>(EvsResult::NOT_SUPPORTED));
}

ScopedAStatus EvsV4lCamera::setExtendedInfo(int32_t opaqueIdentifier,
        const std::vector<uint8_t>& opaqueValue) {
    mExtInfo.insert_or_assign(opaqueIdentifier, opaqueValue);
    return ScopedAStatus::ok();
}

ScopedAStatus EvsV4lCamera::getExtendedInfo(int32_t opaqueIdentifier,
        std::vector<uint8_t>* opaqueValue) {
    const auto it = mExtInfo.find(opaqueIdentifier);
    if (it == mExtInfo.end()) {
        return ScopedAStatus::fromServiceSpecificError(static_cast<int>(EvsResult::INVALID_ARG));
    } else {
        *opaqueValue = mExtInfo[opaqueIdentifier];
    }

    return ScopedAStatus::ok();
}

ScopedAStatus EvsV4lCamera::importExternalBuffers(const std::vector<BufferDesc>& buffers,
                                                  int32_t* _aidl_return) {
    LOG(DEBUG) << __FUNCTION__;
    // If we've been displaced by another owner of the camera, then we can't do anything else
    if (mState != CONFIGURED) {
        LOG(WARNING) << "Ignoring a request add external buffers, camera has been lost.";
        *_aidl_return = 0;
        return ScopedAStatus::fromServiceSpecificError(static_cast<int>(EvsResult::OWNERSHIP_LOST));
    }

    size_t numBuffersToAdd = buffers.size();
    if (numBuffersToAdd < 1) {
        LOG(DEBUG) << "No buffers to add.";
        *_aidl_return = 0;
        return ScopedAStatus::ok();
    }

    {
        std::lock_guard<std::mutex> lock(mAccessLock);
        if (numBuffersToAdd > (kMaxBuffersInFlight - mFramesAllowed)) {
            numBuffersToAdd -= (kMaxBuffersInFlight - mFramesAllowed);
            LOG(WARNING) << "Exceed the limit on number of buffers.  " << numBuffersToAdd
                         << " buffers will be added only.";
        }
        const auto framesAllowedOrig = mFramesAllowed;
        for (size_t i = 0; i < numBuffersToAdd; ++i) {
            // TODO: reject if external buffer is configured differently.
            auto& b = buffers[i];
            std::unique_ptr<EvsV4lCamera::FrameBuffer> buffer = frameImport(b);
            unsigned result = frameAddAndQueue(std::move(buffer));
        }
        *_aidl_return = mFramesAllowed - framesAllowedOrig;
        return ScopedAStatus::ok();
    }
}

EvsResult EvsV4lCamera::queueBufferToCamera(std::unique_ptr<EvsV4lCamera::FrameBuffer> &buffer) {
    libcamera::Request *request = buffer->request();
    if (request == nullptr) {
        LOG(WARNING) << "A buffer has no request!";
    }
    if (request->status() != libcamera::Request::RequestPending) {
        ALOGE("%s: request not in RequestPending state!", __func__);
        return EvsResult::INVALID_ARG;
    }
    int ret = mCamera->queueRequest(request);
    if (ret != 0) {
        LOG(ERROR) << "queueRequest failed " << ret;
        return EvsResult::INVALID_ARG;
    }
    mFramesQueued++;
    return EvsResult::OK;
}

void EvsV4lCamera::requestComplete(libcamera::Request *request) {
    // TODO: See what kind of metadata is caller looking for because libcamera returns many things:
    //   - cookie - holds our buff id
    //   - Less important:
    //   - GetTimestamp(request)
    //   - libcamera::ControlList &metadata = request->metadata();
    //   - (unsigned) request->sequence()
    //   - request->buffers().size()
    //   - libcamera::FrameBuffer *frameBuffer = request->findBuffer(mLibCameraStream);
    if (request == NULL) {
        ALOGE("%s: request NULL", __func__);
        return;
    }
    mFramesQueued--; // Count also canceled requests. Assuming each request has one frame buffer.

    if (request->status() == libcamera::Request::RequestCancelled) {
        ALOGI("%s: request canceled", __func__);
        return;
    }
    // TODO: Optimize code by skipping all the following when stopping. (Even if we are stopping, program should handle following lines fine.)
    request->reuse(libcamera::Request::ReuseBuffers);

    if (request->buffers().size() != 1) {
        LOG(ERROR) << "Discarding framebuffer, the application will starve. The `requestComplete()` is called with multiple streams and buffers!";
    } else {
        forwardFrame((EvsV4lCamera::FrameBuffer * const) request->buffers().begin()->second); // Using `begin()` because request->buffers() is a map.
    }
}

EvsResult EvsV4lCamera::doneWithFrame_impl(const BufferDesc& bufferDesc) {
    if ((mState != RUNNING) && (mState != STOPPING))
    {
        LOG(WARNING) << "Ignoring doneWithFrame call when camera has been lost.";
        return EvsResult::OK;
    }
    --mFramesInUse;

    // Find buffer by id and pass it to camera again.
    for (auto &buffer : mAllocatedBuffers_) {
        if (bufferDesc.bufferId == (int32_t)buffer->cookie()) {
            return queueBufferToCamera(buffer);
        }
    }
    return EvsResult::BUFFER_NOT_AVAILABLE; // Buffer haven't been found.
}

bool EvsV4lCamera::setAvailableFrames_Locked(unsigned bufferCount) {
    if (bufferCount < 1) {
        LOG(ERROR) << "Ignoring request to set buffer count to zero";
        return false;
    }
    if (bufferCount > kMaxBuffersInFlight) {
        LOG(ERROR) << "Rejecting buffer request in excess of internal limit";
        return false;
    }
    // Is an increase required?
    if (mFramesAllowed < bufferCount) {
        // An increase is required
        auto needed = bufferCount - mFramesAllowed;
        LOG(INFO) << "Allocating " << needed << " buffers for camera frames";

        auto added = increaseAvailableFrames_Locked(needed);
        if (added != needed) {
            // If we didn't add all the frames we needed, then roll back to the previous state
            LOG(ERROR) << "Rolling back to previous frame queue size";
            decreaseAvailableFrames_Locked(added);
            return false;
        }
    } else if (mFramesAllowed > bufferCount) {
        // A decrease is required
        auto framesToRelease = mFramesAllowed - bufferCount;
        LOG(INFO) << "Returning " << framesToRelease << " camera frame buffers";

        auto released = decreaseAvailableFrames_Locked(framesToRelease);
        if (released != framesToRelease) {
            // This shouldn't happen with a properly behaving client because the client
            // should only make this call after returning sufficient outstanding buffers
            // to allow a clean resize.
            LOG(ERROR) << "Buffer queue shrink failed -- too many buffers currently in use?";
        }
    }

    return true;
}

std::unique_ptr<EvsV4lCamera::FrameBuffer> EvsV4lCamera::frameImport(const BufferDesc& bDesc)
{
    const HardwareBufferDescription& description = bDesc.buffer.description;

    if (description.layers != 1) {
        LOG(WARNING) << "Failed to import a buffer " << bDesc.bufferId << " because it has more than 1 layer (" << description.layers << ")";
        return nullptr;
    }
    if (mStride > 0) {
        if (mStride != static_cast<uint32_t>(description.stride)) {
            LOG(ERROR) << "We did not expect to get buffers with different strides!";
            return nullptr;
        }
    } else {
        mStride = static_cast<uint32_t>(description.stride);
    }
    // Import a buffer to add
    buffer_handle_t memHandle = nullptr;
    ::android::GraphicBufferMapper& mapper = ::android::GraphicBufferMapper::get();
    auto result =
            mapper.importBuffer(::android::dupFromAidl(bDesc.buffer.handle), description.width,
                                description.height, description.layers,
                                static_cast<::android::PixelFormat>(description.format),
                                static_cast<uint64_t>(description.usage),
                                description.stride, &memHandle);
    if (result != ::android::NO_ERROR) {
        LOG(ERROR) << "Error " << result << " allocating " << mWidth << " x " << mHeight << " graphics buffer.";
        return nullptr;
    }
    if (memHandle == nullptr) {
        LOG(ERROR) << "We didn't get a buffer handle back from the allocator";
        return nullptr;
    }
    return frameBufferCreate(memHandle);
}

std::unique_ptr<EvsV4lCamera::FrameBuffer> EvsV4lCamera::frameAllocate()
{
    unsigned pixelsPerLine = 0; // Gralloc defines stride in terms of pixels per line.
    buffer_handle_t memHandle = nullptr;
    ::android::GraphicBufferAllocator& alloc(::android::GraphicBufferAllocator::get());
    auto result = alloc.allocate(mWidth, mHeight, static_cast<int>(mFormat), 1, mUsage, &memHandle, &pixelsPerLine, 0, "EvsV4lCamera");
    if (result != ::android::NO_ERROR) {
        LOG(ERROR) << "Error " << result << " allocating " << mWidth << " x " << mHeight << " graphics buffer.";
        return nullptr;
    }

    // Check each buffer has the same stride as previously allocated buffers.
    if (mStride > 0) {
        if (mStride != pixelsPerLine) {
            LOG(ERROR) << "We did not expect to get buffers with different strides!";
            return nullptr;
        }
    } else {
        mStride = pixelsPerLine;
    }
    return frameBufferCreate(memHandle);
}

std::unique_ptr<EvsV4lCamera::FrameBuffer> EvsV4lCamera::frameBufferCreate(buffer_handle_t &memHandle)
{
    if (memHandle == nullptr) {
        LOG(ERROR) << "We didn't get a buffer handle back from the allocator";
        return nullptr;
    }

    // Create FrameBuffer::Planes
    std::vector<libcamera::FrameBuffer::Plane> planes(mNumPlanes);
    for (unsigned i = 0, offset = 0; i < planes.size(); ++i)
    {
        auto &plane = planes[i];
        const size_t planeSize = mHeight * mStride;

        plane.fd = libcamera::SharedFD{ memHandle->data[0] }; // TODO - Multiplanar support: The handle->data[0-2] contains separate fd for three planes. In fact gralloc creates two fd per plane so there are six in total.
        plane.offset = offset;
        plane.length = planeSize;
        offset += planeSize;
    }
    return std::make_unique<EvsV4lCamera::FrameBuffer>(planes, memHandle);
}

unsigned EvsV4lCamera::frameAddAndQueue(std::unique_ptr<EvsV4lCamera::FrameBuffer> fb)
{
    LOG(DEBUG) << "frameAddAndQueue()";
    if (!fb) {
        LOG(ERROR) << "No fb.";
        return -1;
    }
    // The `cookie()` is `BufferDesc.bufferId` which is a 32-bit number. Value 0 of `buffer->cookie()` and `BufferDesc.bufferId` is invalid. FP: Is it for sure?
    auto cookie = mLastCookie = (mLastCookie+1 != 0) ? mLastCookie+1 : 1;
    std::unique_ptr<libcamera::Request> request = mCamera->createRequest();
    if (!request) {
        LOG(ERROR) << "createRequest() failed for cookie.";
        return -1;
    }
    int ret = request->addBuffer(mLibCameraStream, fb.get());
    if (ret < 0) {
        LOG(ERROR) << "request->addBuffer() failed with " << ret;
        return -1;
    }
    fb->setCookie(cookie);
    mRequests.push_back(std::move(request));
    mAllocatedBuffers_.push_back(std::move(fb));
    ++mFramesAllowed;
    mLastCookie = cookie;
    return 0;
}

unsigned EvsV4lCamera::increaseAvailableFrames_Locked(unsigned numToAdd) {
    LOG(DEBUG) << "increaseAvailableFrames_Locked()";
    unsigned added = 0;
    while (added < numToAdd) {
        auto fb = frameAllocate();

        if (!fb) {
            LOG(ERROR) << "frameAllocate failed";
            return -1;
        }
        frameAddAndQueue(std::move(fb));
        ++added;
    }
    return numToAdd;
}

// This is the async callback from the video camera that tells us a frame is ready
void EvsV4lCamera::forwardFrame(EvsV4lCamera::FrameBuffer *aBuff) {
    LOG(DEBUG) << __FUNCTION__;

    auto *pBuffer = (EvsV4lCamera::FrameBuffer *)aBuff;
    if (pBuffer) {
        int32_t buffId = static_cast<int32_t>(pBuffer->cookie());

        if (buffId == 0) {
            ALOGE("%s: buffId NULL", __func__);
            return;
        }

        if (mDropFrames > 0) {
            --mDropFrames;
            for (auto &buffer : mAllocatedBuffers_) {
                if (buffer.get() == pBuffer) {
                    EvsResult ret = queueBufferToCamera(buffer);
                    if (ret != EvsResult::OK) {
                        LOG(ERROR) << "Failed to requeue buffer to camera" << static_cast<int>(ret);
                    }
                }
            }
            return;
        }
        // Assemble the buffer description for the client.
        BufferDesc bufferDesc = {
            .buffer =
            {
                .description =
                {
                    .width = static_cast<int32_t>(mWidth),
                    .height = static_cast<int32_t>(mHeight),
                    .layers = 1, // TODO - Obtain count (save as a member, get from the buffer).
                    .format = mFormat,
                    .usage = static_cast<BufferUsage>(mUsage),
                    .stride = static_cast<int32_t>(mStride),
                },
                .handle = ::android::dupToAidl(pBuffer->handle()),
            },
            .bufferId = (int32_t) pBuffer->cookie(),
            .deviceId = mDescription.id,
            .timestamp = static_cast<int64_t>(::android::elapsedRealtimeNano() * 1e+3),
        };

        auto flag = false;
        if (mEvsStreamClient) {
            mFramesInUse++;
            std::vector<BufferDesc> frames;
            frames.push_back(std::move(bufferDesc));
            flag = mEvsStreamClient->deliverFrame(frames).isOk();
            if (flag) {
                LOG(DEBUG) << __func__ << ": Delivered id " << pBuffer->cookie();
            } else {
                // This can happen if the client dies and is likely unrecoverable. To avoid consuming resources generating failing calls, we stop sending frames.
                // Note, however, that the stream remains in the "STREAMING" state until cleaned up on the main thread.
                LOG(ERROR) << __func__ << ": Frame delivery call failed in the transport layer.";
                --mFramesInUse;
            }
        }
    }
    ++mFrameCounter;
}

std::shared_ptr<EvsV4lCamera> EvsV4lCamera::Create(std::shared_ptr<libcamera::Camera> libcamera) {
    std::unique_ptr<ConfigManager::CameraInfo> nullCamInfo = nullptr;
    return Create(libcamera, nullCamInfo);
}

std::shared_ptr<EvsV4lCamera> EvsV4lCamera::Create(
    std::shared_ptr<libcamera::Camera> libcamera, std::unique_ptr<ConfigManager::CameraInfo>& camInfo, const aidlevs::Stream* requestedStreamCfg) {
    std::shared_ptr<EvsV4lCamera> evsCamera = ndk::SharedRefBase::make<EvsV4lCamera>(libcamera, camInfo);
    if (!evsCamera) {
        return nullptr;
    }
    LOG(INFO) << __func__ << ": Create " << evsCamera->mDescription.id;

    // TODO: Consider separating this code into a method called by EvsV4lCamera::Create().
    //          - Currently nice thing is: Destructor EvsV4lCamera gets called after Create() failure.
    //          - But static Create() doesn't allow anyone to inherit from us.
    int ret = evsCamera->mCamera->acquire(); // Acquire libcamera.
    if (ret) {
        LOG(ERROR) << __func__ << ": Failed to acquire camera " << evsCamera->mCamera->id().c_str();
        return nullptr;
    }
    evsCamera->mState = ACQUIRED;

    if (requestedStreamCfg != nullptr) {
        LOG(INFO) << __func__ << ": Requested configuration: " << requestedStreamCfg->width << " x " << requestedStreamCfg->height << " format: " << static_cast<int>(requestedStreamCfg->format);

        if (camInfo != nullptr) {
            // Validate a given stream configuration.
            // If there is no exact match, this will try to find the best match based on:
            //  1) same output format
            //  2) the largest resolution that is smaller that a given configuration.
            int32_t streamId = -1, area = INT_MIN;
            for (auto& [id, cfg] : camInfo->streamConfigurations) {
                if (cfg.format == requestedStreamCfg->format) {
                    if (cfg.width == requestedStreamCfg->width &&
                            cfg.height == requestedStreamCfg->height) {
                        // Find exact match.
                        streamId = id;
                        break;
                    } else if (cfg.width < requestedStreamCfg->width &&
                               cfg.height < requestedStreamCfg->height &&
                               cfg.width * cfg.height > area) {
                        streamId = id;
                        area = cfg.width * cfg.height;
                    }
                }
            }
            if (streamId >= 0) {
                evsCamera->mFormat = camInfo->streamConfigurations[streamId].format;
                evsCamera->mWidth = camInfo->streamConfigurations[streamId].width;
                evsCamera->mHeight = camInfo->streamConfigurations[streamId].height;

                LOG(INFO) << __func__ << ": Validated configuration by XML: " << evsCamera->mWidth << " x " << evsCamera->mHeight << " format " << static_cast<unsigned>(evsCamera->mFormat);
            }
        }
    }
    {
        std::unique_ptr<libcamera::CameraConfiguration> camCfg = evsCamera->mCamera->generateConfiguration(); // Optionally set StreamRole eg. { StreamRole::Viewfinder } .
        if (!camCfg) {
            LOG(ERROR) << __func__ << ": Failed to generate camera camCfguration";
            return nullptr;
        }

        LOG(INFO) << __func__ << ": generate camera camCfguration, with size " << camCfg->size();
        for (auto &config : *camCfg) {
            LOG(WARNING) << __func__<< ": Configuration: " << config.toString();
        }

        libcamera::StreamConfiguration cfg;
        cfg.bufferCount = 4;
        cfg.size.width = evsCamera->mWidth;
        cfg.size.height = evsCamera->mHeight;
        cfg.pixelFormat = AidlFromat2PixelFormat(evsCamera->mFormat);
        camCfg->addConfiguration(cfg);

        LOG(DEBUG) << __func__ << ": Requesting configuration from libcamera: " << cfg.toString();
        switch (camCfg->validate()) {
            case  libcamera::CameraConfiguration::Status::Adjusted:
                LOG(DEBUG) << __func__ << ": Adjusted configuration by libcamera: ";
                break;
            case  libcamera::CameraConfiguration::Status::Invalid:
                LOG(ERROR) << __func__ << ": Failed to configure camera. Libcamera rejected configuration " << cfg.toString() << " as invalid.";
                return nullptr;
            default: // libcamera::CameraConfiguration::Status::Valid
                break;
        }
        evsCamera->mNumPlanes = 1; // TODO: Convert format to number of planes.

        // The EvsV4lCamera::frameAllocate() doesn't support multiplanar FrameBuffers.
        if (evsCamera->mNumPlanes != 1) {
            LOG(ERROR) << __func__<< ": Failed to configure camera. Multiplanar format not supported. Selected configuration was: " << cfg.toString();
            return nullptr;
        }
        int ret = evsCamera->mCamera->configure(camCfg.get());
        if (ret) {
            LOG(ERROR) << __func__ << ": Failed to configure camera because: " << evsCamera->mCamera->id().c_str();
            return nullptr;
        } else {
            // Camera has been configured, keep configuration data important for buffer allocation.
            auto libCameraStreamSet = evsCamera->mCamera->streams();

            for (auto &stream : libCameraStreamSet) {
                LOG(DEBUG) << __func__<< ": Configured Stream cfg: " << stream->configuration().toString();
            }
            cfg = (*camCfg)[0];
            evsCamera->mLibcameraCamCfg = std::move(camCfg);
            evsCamera->mLibCameraStream = evsCamera->mLibcameraCamCfg->at(0).stream();
            evsCamera->mFormat = formatV4l2ToAidl(cfg.pixelFormat.fourcc());
            evsCamera->mWidth = cfg.size.width;
            evsCamera->mHeight = cfg.size.height;
            evsCamera->mState = CONFIGURED;
            if (evsCamera->mFormat != AidlPixelFormat::UNSPECIFIED) {
                return evsCamera;
            }
        }
    }
    return nullptr;
}

Result<void> EvsV4lCamera::startDumpFrames(const std::string& path) {
    struct stat info;
    if (stat(path.data(), &info) != 0) {
        return Error(::android::BAD_VALUE) << "Cannot access " << path;
    } else if (!(info.st_mode & S_IFDIR)) {
        return Error(::android::BAD_VALUE) << path << " is not a directory";
    }

    mDumpPath = path;
    mDumpFrame = true;

    return {};
}

Result<void> EvsV4lCamera::stopDumpFrames() {
    if (!mDumpFrame) {
        return Error(::android::INVALID_OPERATION) << "Device is not dumping frames";
    }

    mDumpFrame = false;
    return {};
}

}  // namespace aidl::android::hardware::automotive::evs::implementation
