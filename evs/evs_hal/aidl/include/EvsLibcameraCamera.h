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

#ifndef CPP_EVS_IMX_LIBCAMERA_AIDL_INCLUDE_EVSV4LCAMERA_H
#define CPP_EVS_IMX_LIBCAMERA_AIDL_INCLUDE_EVSV4LCAMERA_H

#include "ConfigManager.h"

#include "libcamera/camera.h"
#include "libcamera/camera_manager.h"

#include <aidl/android/hardware/automotive/evs/BnEvsCamera.h>
#include <aidl/android/hardware/automotive/evs/BufferDesc.h>
#include <aidl/android/hardware/automotive/evs/CameraDesc.h>
#include <aidl/android/hardware/automotive/evs/CameraParam.h>
#include <aidl/android/hardware/automotive/evs/EvsResult.h>
#include <aidl/android/hardware/automotive/evs/IEvsCameraStream.h>
#include <aidl/android/hardware/automotive/evs/IEvsDisplay.h>
#include <aidl/android/hardware/automotive/evs/ParameterRange.h>
#include <aidl/android/hardware/automotive/evs/Stream.h>
#include <android-base/result.h>
#include <ui/GraphicBuffer.h>

#include <functional>
#include <thread>

namespace aidl::android::hardware::automotive::evs::implementation {
namespace aidlevs = ::aidl::android::hardware::automotive::evs;

#define EVS_FAKE_PROP "vendor.evs.fake.enable"

class EvsV4lCamera : public ::aidl::android::hardware::automotive::evs::BnEvsCamera {

    using AidlPixelFormat = ::aidl::android::hardware::graphics::common::PixelFormat;

    class FrameBuffer final : public libcamera::FrameBuffer
    {
    public:
        FrameBuffer(const std::vector<Plane> &planes,
                buffer_handle_t handle)	: libcamera::FrameBuffer(planes), handle_(handle)
        {
        };
        ~FrameBuffer()
        {
            ::android::GraphicBufferAllocator& alloc(::android::GraphicBufferAllocator::get());

            alloc.free(handle_);
        };

        buffer_handle_t handle() const { return handle_; }

    private:
        const buffer_handle_t handle_;
    };

    static AidlPixelFormat formatV4l2ToAidl(int v4lFormat);
    static libcamera::PixelFormat AidlFromat2PixelFormat(AidlPixelFormat AidlFmt);

public:
    // Methods from ::android::hardware::automotive::aidlevs::IEvsCamera follow.
    ::ndk::ScopedAStatus doneWithFrame(const std::vector<aidlevs::BufferDesc>& buffers) override;
    ::ndk::ScopedAStatus forcePrimaryClient(
            const std::shared_ptr<aidlevs::IEvsDisplay>& display) override;
    ::ndk::ScopedAStatus getCameraInfo(aidlevs::CameraDesc* _aidl_return) override;
    ::ndk::ScopedAStatus getExtendedInfo(int32_t opaqueIdentifier,
                                         std::vector<uint8_t>* value) override;
    ::ndk::ScopedAStatus getIntParameter(aidlevs::CameraParam id,
                                         std::vector<int32_t>* value) override;
    ::ndk::ScopedAStatus getIntParameterRange(aidlevs::CameraParam id,
                                              aidlevs::ParameterRange* _aidl_return) override;
    ::ndk::ScopedAStatus getParameterList(std::vector<aidlevs::CameraParam>* _aidl_return) override;
    ::ndk::ScopedAStatus getPhysicalCameraInfo(const std::string& deviceId,
                                               aidlevs::CameraDesc* _aidl_return) override;
    ::ndk::ScopedAStatus importExternalBuffers(const std::vector<aidlevs::BufferDesc>& buffers,
                                               int32_t* _aidl_return) override;
    ::ndk::ScopedAStatus pauseVideoStream() override;
    ::ndk::ScopedAStatus resumeVideoStream() override;
    ::ndk::ScopedAStatus setExtendedInfo(int32_t opaqueIdentifier,
                                         const std::vector<uint8_t>& opaqueValue) override;
    ::ndk::ScopedAStatus setIntParameter(aidlevs::CameraParam id, int32_t value,
                                         std::vector<int32_t>* effectiveValue) override;
    ::ndk::ScopedAStatus setPrimaryClient() override;
    ::ndk::ScopedAStatus setMaxFramesInFlight(int32_t bufferCount) override;
    ::ndk::ScopedAStatus startVideoStream(
            const std::shared_ptr<aidlevs::IEvsCameraStream>& receiver) override;
    ::ndk::ScopedAStatus stopVideoStream() override;
    ::ndk::ScopedAStatus unsetPrimaryClient() override;

    static std::shared_ptr<EvsV4lCamera> Create(std::shared_ptr<libcamera::Camera>);
    static std::shared_ptr<EvsV4lCamera> Create(std::shared_ptr<libcamera::Camera>,
                                                std::unique_ptr<ConfigManager::CameraInfo>& camInfo,
                                                const aidlevs::Stream* streamCfg = nullptr);
    EvsV4lCamera(const EvsV4lCamera&) = delete;
    EvsV4lCamera& operator=(const EvsV4lCamera&) = delete;

    virtual ~EvsV4lCamera() override;
    void shutdown();

    const aidlevs::CameraDesc& getDesc() { return mDescription; }

    // Dump captured frames to the filesystem
    ::android::base::Result<void> startDumpFrames(const std::string& path);
    ::android::base::Result<void> stopDumpFrames();

    // Constructors
    EvsV4lCamera(std::shared_ptr<libcamera::Camera>, std::unique_ptr<ConfigManager::CameraInfo>& camInfo);

private:
    // These three functions are expected to be called while mAccessLock is held
    bool setAvailableFrames_Locked(unsigned bufferCount);
    unsigned increaseAvailableFrames_Locked(unsigned numToAdd);
    unsigned decreaseAvailableFrames_Locked(unsigned numToRemove);

    void requestComplete(libcamera::Request *request);
    void forwardFrame(EvsV4lCamera::FrameBuffer *pBuff);
    EvsResult queueBufferToCamera(std::unique_ptr<EvsV4lCamera::FrameBuffer> &buffer);

    std::unique_ptr<EvsV4lCamera::FrameBuffer> frameBufferCreate(
        const buffer_handle_t handle, const uint32_t format, const uint32_t stride);
    std::unique_ptr<EvsV4lCamera::FrameBuffer> frameBufferCreate(buffer_handle_t &memHandle);

    unsigned frameAddAndQueue(std::unique_ptr<EvsV4lCamera::FrameBuffer> fb);

    std::unique_ptr<EvsV4lCamera::FrameBuffer> frameAllocate();
    std::unique_ptr<EvsV4lCamera::FrameBuffer> frameImport(const BufferDesc& bDesc);

    // The callback used to deliver each frame
    std::shared_ptr<aidlevs::IEvsCameraStream> mEvsStreamClient;
    libcamera::Stream *mLibCameraStream;


    // The properties of this camera
    aidlevs::CameraDesc mDescription = {};
    std::shared_ptr<libcamera::Camera> mCamera;
    std::unique_ptr<libcamera::CameraConfiguration> mLibcameraCamCfg;

    uint32_t mWidth = 0;  // Values from android_pixel_format_t
    uint32_t mHeight = 0;  // Values from android_pixel_format_t
    AidlPixelFormat mFormat = AidlPixelFormat::UNSPECIFIED;  // Values from android_pixel_format_t
    uint32_t mUsage = 0;   // Values from from Gralloc.h
    uint32_t mStride = 0;  // Pixels per row (may be greater than image width)
    uint32_t mNumPlanes = 0;

    // Graphics buffers to transfer images and their V4L buffer id's.
    int mLastCookie = 0;
    std::vector<std::unique_ptr<EvsV4lCamera::FrameBuffer>> mAllocatedBuffers_;

    enum {CONFIGURED, RUNNING, STOPPING, ACQUIRED, AVAILABLE} mState;
    // How many buffers are we currently using
    unsigned mFramesAllowed;
    // How many buffers are currently outstanding
    unsigned mFramesQueued;
    unsigned mFramesInUse;
    unsigned mDropFrames;

    std::set<uint32_t> mCameraControls;  // Available camera controls

    std::vector<std::unique_ptr<libcamera::Request>> mRequests;

    aidlevs::EvsResult doneWithFrame_impl(const aidlevs::BufferDesc& bufferDesc);
    // Synchronization necessary to deconflict the capture thread from the main service thread
    // Note that the service interface remains single threaded (ie: not reentrant)
    mutable std::mutex mAccessLock;

    // Static camera module information
    std::unique_ptr<ConfigManager::CameraInfo>& mCameraInfo;

    // Extended information
    std::unordered_map<uint32_t, std::vector<uint8_t>> mExtInfo;

    // Dump captured frames
    std::atomic<bool> mDumpFrame = false;

    // Path to store captured frames
    std::string mDumpPath;

    // Frame counter
    uint64_t mFrameCounter = 0;
};

}  // namespace aidl::android::hardware::automotive::evs::implementation

#endif  // CPP_EVS_IMX_LIBCAMERA_AIDL_INCLUDE_EVSV4LCAMERA_H
