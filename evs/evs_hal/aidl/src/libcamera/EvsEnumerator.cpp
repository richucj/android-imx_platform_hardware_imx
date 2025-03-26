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

#include "EvsLibcameraEnumerator.h"

#include "ConfigManager.h"
#include "EvsGlDisplay.h"
#include "EvsLibcameraCamera.h"

#include <aidl/android/hardware/automotive/evs/DeviceStatusType.h>
#include <aidl/android/hardware/automotive/evs/EvsResult.h>
#include <aidl/android/hardware/automotive/evs/Rotation.h>
#include <aidl/android/hardware/graphics/common/BufferUsage.h>
#include <aidl/android/hardware/graphics/common/PixelFormat.h>
#include <android-base/file.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>
#include <cutils/android_filesystem_config.h>
#include <cutils/properties.h>

#include <dirent.h>
#include <sys/epoll.h>
#include <sys/inotify.h>

#include <string_view>

namespace {

using ::aidl::android::frameworks::automotive::display::ICarDisplayProxy;
using ::aidl::android::hardware::automotive::evs::DeviceStatusType;
using ::aidl::android::hardware::automotive::evs::EvsResult;
using ::aidl::android::hardware::automotive::evs::Rotation;
using ::aidl::android::hardware::graphics::common::BufferUsage;
using ::android::base::EqualsIgnoreCase;
using ::android::base::StringPrintf;
using ::android::base::WriteStringToFd;
using ::ndk::ScopedAStatus;
using std::chrono_literals::operator""s;

// Constants
constexpr std::chrono::seconds kEnumerationTimeout = 10s;
constexpr uint64_t kInvalidDisplayId = std::numeric_limits<uint64_t>::max();
const std::set<uid_t> kAllowedUids = {AID_AUTOMOTIVE_EVS, AID_SYSTEM, AID_ROOT};

#define HWC_PATH_LENGTH 64
#define BUFFER_SIZE 512
#define EPOLL_MAX_EVENTS 8
#define MEDIA_FILE_PATH "/dev"
#define EVS_VIDEO_READY "vendor.evs.video.ready"
#define EVS_VIDEO_DEV   "vendor.evs.video.dev"
#define EVS_ISI_NAME    "vendor.evs.isi.name"
#define EVS_FAKE_SENSOR "mxc_isi.0.capture"
#define EVS_FAKE_LOGIC_CAMERA "group0"
#define EVS_FAKE_NAME   "fake.camera"
#define EVS_FAKE_LOGIC_NAME   "fake.logic.camera"
#define FAKE_CAMERA_WIDTH 1920
#define FAKE_CAMERA_HEIGHT 1024

}  // namespace

namespace aidl::android::hardware::automotive::evs::implementation {

// NOTE:  All members values are static so that all clients operate on the same state
//        That is to say, this is effectively a singleton despite the fact that HIDL
//        constructs a new instance for each client.
std::list<EvsEnumerator::CameraRecord> EvsEnumerator::sOpenCameraList;
std::mutex EvsEnumerator::sLock;
std::unique_ptr<ConfigManager> EvsEnumerator::sConfigManager;
std::shared_ptr<ICarDisplayProxy> EvsEnumerator::sDisplayProxy;
std::unordered_map<uint8_t, uint64_t> EvsEnumerator::sDisplayPortList;

std::unique_ptr<libcamera::CameraManager> EvsEnumerator::cameraManager_ = nullptr;

EvsEnumerator::ActiveDisplays& EvsEnumerator::mutableActiveDisplays() {
    static ActiveDisplays active_displays;
    return active_displays;
}

bool EvsEnumerator::filterVideoFromConfigure(char *deviceName) {
    if (sConfigManager == nullptr)
        return true;

    std::vector<std::string>::iterator index;
    std::vector<std::string> cameraList =
        sConfigManager->getCameraIdList();
    index = find(cameraList.begin(), cameraList.end(), deviceName);
    if(index != cameraList.end())
        return true;
    else
        return false;
}

EvsEnumerator::EvsEnumerator(const std::shared_ptr<ICarDisplayProxy>& proxyService) {
    LOG(DEBUG) << "EvsEnumerator is created.";

    if (!sConfigManager) {
        /* loads and initializes ConfigManager in a separate thread */
        sConfigManager = ConfigManager::Create();
    }

    if (!sDisplayProxy) {
        /* sets a car-window service handle */
        sDisplayProxy = proxyService;
    }

    /* Enumerate existing devices */
    enumerateCameras();
    mInternalDisplayId = enumerateDisplays();
}

bool EvsEnumerator::checkPermission() {
    const auto uid = AIBinder_getCallingUid();
    if (kAllowedUids.find(uid) == kAllowedUids.end()) {
        LOG(ERROR) << "EVS access denied: "
                   << "pid = " << AIBinder_getCallingPid() << ", uid = " << uid;
        return false;
    }

    return true;
}

bool EvsEnumerator::enumerateCameras() {
    if (sConfigManager == nullptr) {
        /* loads and initializes ConfigManager in a separate thread */
        sConfigManager = ConfigManager::Create();
    }
    if (cameraManager_ == nullptr) {
        cameraManager_ = std::make_unique<libcamera::CameraManager>();
    }

    int ret = cameraManager_->start(); /* TODO: Program goes through here everytime EVS app opens. Skip this call. */
    if (ret) {
        ALOGE("%s: Failed to start camera manager, ret %d", __func__, ret);
        cameraManager_.reset(); // Reset the unique_ptr.
        return false;
    }

    if (cameraManager_->cameras().empty()) {
        LOG(DEBUG) << "No cameras were identified on the system." ;
        cameraManager_->stop();
        return false;
    }
    if (property_set(EVS_VIDEO_READY, "1") < 0) {
        ALOGE("Can not set property %s", EVS_VIDEO_READY);
    }
    return true;
}

uint64_t EvsEnumerator::enumerateDisplays() {
    LOG(INFO) << __FUNCTION__ << ": Starting display enumeration";
    uint64_t internalDisplayId = kInvalidDisplayId;
    if (!sDisplayProxy) {
        LOG(ERROR) << "ICarDisplayProxy is not available!";
        return internalDisplayId;
    }

    std::vector<int64_t> displayIds;
    if (auto status = sDisplayProxy->getDisplayIdList(&displayIds); !status.isOk()) {
        LOG(ERROR) << "Failed to retrieve a display id list"
                   << ::android::statusToString(status.getStatus());
        return internalDisplayId;
    }

    if (displayIds.size() > 0) {
        // The first entry of the list is the internal display.  See
        // SurfaceFlinger::getPhysicalDisplayIds() implementation.
        internalDisplayId = displayIds[0];
        for (const auto& id : displayIds) {
            const auto port = id & 0xFF;
            LOG(INFO) << "Display " << std::hex << id << " is detected on the port, " << port;
            sDisplayPortList.insert_or_assign(port, id);
        }
    }

    LOG(INFO) << "Found " << sDisplayPortList.size() << " displays";
    return internalDisplayId;
}

// Methods from ::android::hardware::automotive::evs::IEvsEnumerator follow.
ScopedAStatus EvsEnumerator::getCameraList(std::vector<CameraDesc>* _aidl_return) {
    LOG(DEBUG) << __FUNCTION__;
    if (!checkPermission()) {
        return ScopedAStatus::fromServiceSpecificError(static_cast<int>(EvsResult::PERMISSION_DENIED));
    }
    if (!cameraManager_) {
        return ScopedAStatus::fromServiceSpecificError(static_cast<int>(EvsResult::UNDERLYING_SERVICE_ERROR));
    }
    unsigned numCameras = cameraManager_->cameras().size();
    if (numCameras == 0) {
        LOG(WARNING) << "No camera devices available.";
    }
    _aidl_return->reserve(numCameras);

    if (sConfigManager == nullptr) {
        LOG(WARNING) << "Config Manager not available.";

        for (auto &camera : cameraManager_->cameras()) {
            LOG(DEBUG) << "\t" << camera->id().c_str(); // Eg. "/base/soc/bus@42000000/i2c@42530000/max96724@27/i2c-mux/i2c@3/mx95mbcam@40"

            CameraDesc a{camera->id().c_str()};
            _aidl_return->emplace_back(camera->id().c_str());
        }
    } else {
        for (auto &camera : cameraManager_->cameras()) {
            LOG(DEBUG) << "\t" << camera->id().c_str();

            // Check if camera is known by config XML (see mConfigFilePath).
            std::unique_ptr<ConfigManager::CameraInfo> &camCfg = sConfigManager->getCameraInfo(camera->id().c_str());
            if (!camCfg) {
                continue; // Skip unknown camera.
            } else {
                uint8_t* ptr = reinterpret_cast<uint8_t*>(camCfg->characteristics);
                const size_t len = get_camera_metadata_size(camCfg->characteristics);

                _aidl_return->emplace_back(camera->id().c_str());
                // App requires metadata from config. Having constructed a descriptor fill in the metadata.
                _aidl_return->back().metadata.insert(_aidl_return->back().metadata.end(), ptr, ptr + len);
            }
        }
    }
    LOG(DEBUG) << "Reporting " << _aidl_return->size() << " cameras available";
    return ScopedAStatus::ok();
}

ScopedAStatus EvsEnumerator::getStreamList(const CameraDesc& desc,
        std::vector<Stream>* _aidl_return) {
    using AidlPixelFormat = ::aidl::android::hardware::graphics::common::PixelFormat;

    camera_metadata_t* pMetadata = const_cast<camera_metadata_t*>(
                                       reinterpret_cast<const camera_metadata_t*>(desc.metadata.data()));
    camera_metadata_entry_t streamConfig;
    if (!find_camera_metadata_entry(pMetadata, ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS,
                                    &streamConfig)) {
        const unsigned numStreamConfigs = streamConfig.count / sizeof(StreamConfiguration);
        _aidl_return->resize(numStreamConfigs);
        const StreamConfiguration* pCurrentConfig =
            reinterpret_cast<StreamConfiguration*>(streamConfig.data.i32);
        for (unsigned i = 0; i < numStreamConfigs; ++i, ++pCurrentConfig) {
            // Build ::aidl::android::hardware::automotive::evs::Stream from
            // StreamConfiguration.
            Stream current = {
                .id = pCurrentConfig->id,
                .streamType = (pCurrentConfig->type == ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_INPUT)
                    ? StreamType::INPUT
                    : StreamType::OUTPUT,
                .width = pCurrentConfig->width,
                .height = pCurrentConfig->height,
                .format = static_cast<AidlPixelFormat>(pCurrentConfig->format),
                .usage = BufferUsage::CAMERA_INPUT,
                .rotation = Rotation::ROTATION_0,
            };

            (*_aidl_return)[i] = std::move(current);
        }
    }

    return ScopedAStatus::ok();
}

ScopedAStatus EvsEnumerator::openCamera(const std::string& id, const Stream& cfg,
                                        std::shared_ptr<IEvsCamera>* obj) {
    // Function creates two references to IEvsCamera shared_ptr, 1. shared_ptr `*obj` and 2. EvsEnumerator's weak_ptr.

    LOG(DEBUG) << __FUNCTION__;
    if (!checkPermission()) {
        return ScopedAStatus::fromServiceSpecificError(static_cast<int>(EvsResult::PERMISSION_DENIED));
    }
    if (!cameraManager_) {
        return ScopedAStatus::fromServiceSpecificError(static_cast<int>(EvsResult::UNDERLYING_SERVICE_ERROR));
    }
    // This function inherited this "Close camera and give it to next client" code from original EvsEnumerator.
    // Code gets called because evs_app never calls EvsEnumerator::closeCamera().
    auto it = std::find_if( std::begin( sOpenCameraList ), std::end( sOpenCameraList ), [&]( const CameraRecord &rec ) {
        return id == ( rec.name );
    } ); // TODO: Need for mutex?
    if (it != sOpenCameraList.end()) {
        LOG(INFO) << "Requested camera " << id << " already has a record.";
        std::shared_ptr<EvsV4lCamera> pActiveCamera = it->activeInstance.lock(); // Call weak_ptr.lock() to check if the camera still exists.
        if (pActiveCamera) {
            LOG(WARNING) << "Closing previous camera because of new caller";
            closeCamera(pActiveCamera); // The closeCamera() method uses this only to find (weak_ptr)activeInstance again.
        }
        sOpenCameraList.erase(it);
    }

    // Check we can obtain camera from Libcamera.
    std::shared_ptr<libcamera::Camera> pCamera = cameraManager_->get(id);
    if (!pCamera) {
        LOG(ERROR) << "Failed to open camera. Camera " << id << " is not available.";
        return ScopedAStatus::fromServiceSpecificError(static_cast<int>(EvsResult::OWNERSHIP_LOST));
    }

    // Construct camera instance for this client.
    std::shared_ptr<EvsV4lCamera> pActiveCamera =
        (sConfigManager) ?
        EvsV4lCamera::Create(pCamera, sConfigManager->getCameraInfo(id), &cfg) : // With `sConfigManager`
        EvsV4lCamera::Create(pCamera);                                            // Without `sConfigManager` - currently unsupported.

    if (!pActiveCamera) {
        LOG(ERROR) << "Failed to create new EvsV4lCamera object for " << id;
        return ScopedAStatus::fromServiceSpecificError(static_cast<int>(EvsResult::UNDERLYING_SERVICE_ERROR));
    } else {
        // Return the camera to the client and also create a record for it via weak_ptr
        // leaving owership of this shared_ptr only to the client.
        auto &client = sOpenCameraList.emplace_back(id.c_str(), pActiveCamera); // TODO: Potentially needs mutex.
        *obj = std::move(pActiveCamera);
        return ScopedAStatus::ok();
    }
}

ScopedAStatus EvsEnumerator::closeCamera(const std::shared_ptr<IEvsCamera>& cameraObj) {
    // Client is closing the camera. We want to drop reference from CaeraRecord.
    LOG(DEBUG) << __FUNCTION__;

    if (!cameraObj) {
        LOG(ERROR) << "Ignoring call to closeCamera with null camera ptr";
        return ScopedAStatus::fromServiceSpecificError(static_cast<int>(EvsResult::INVALID_ARG));
    }

    // Find and remove CameraRecord.
    auto it = std::find_if( std::begin( sOpenCameraList ),
                            std::end( sOpenCameraList ),
    [&]( auto &rec ) {
        // Look for matching control block of shared and weak ptr.
        return ( !rec.activeInstance.owner_before(cameraObj)
                 && !cameraObj.owner_before(rec.activeInstance) ) ;
    });
    if (it != sOpenCameraList.end()) {
        sOpenCameraList.erase(it); // Destructor will call `camera->shutdown()`.
        LOG(DEBUG) << "closeCamera() removed couple cameras!";
    }
    else {
        LOG(ERROR) << "Asked to close a camera that we don't have record for.";
        ((EvsV4lCamera *)cameraObj.get())->shutdown(); // Try shutdown the active camera anyway.
    }

    return ScopedAStatus::ok();
}

ScopedAStatus EvsEnumerator::openDisplay(int32_t id, std::shared_ptr<IEvsDisplay>* displayObj) {
    LOG(DEBUG) << __FUNCTION__;
    if (!checkPermission()) {
        return ScopedAStatus::fromServiceSpecificError(
                   static_cast<int>(EvsResult::PERMISSION_DENIED));
    }

    auto& displays = mutableActiveDisplays();

    if (auto existing_display_search = displays.popDisplay(id)) {
        // If we already have a display active, then we need to shut it down so we can
        // give exclusive access to the new caller.
        std::shared_ptr<EvsGlDisplay> pActiveDisplay = existing_display_search->displayWeak.lock();
        if (pActiveDisplay) {
            LOG(WARNING) << "Killing previous display because of new caller";
            pActiveDisplay->forceShutdown();
        }
    }

    // Create a new display interface and return it.
    uint64_t targetDisplayId = mInternalDisplayId;
    auto it = sDisplayPortList.find(id);
    if (it != sDisplayPortList.end()) {
        targetDisplayId = it->second;
    } else {
        LOG(WARNING) << "No display is available on the port " << static_cast<int32_t>(id)
                     << ". The main display " << mInternalDisplayId << " will be used instead";
    }

    // Create a new display interface and return it.
    std::shared_ptr<EvsGlDisplay> pActiveDisplay =
        ndk::SharedRefBase::make<EvsGlDisplay>(sDisplayProxy, targetDisplayId);

    if (auto insert_result = displays.tryInsert(id, pActiveDisplay); !insert_result) {
        LOG(ERROR) << "Display ID " << id << " has been used by another caller.";
        pActiveDisplay->forceShutdown();
        return ScopedAStatus::fromServiceSpecificError(static_cast<int>(EvsResult::RESOURCE_BUSY));
    }

    LOG(DEBUG) << "Returning new EvsGlDisplay object " << pActiveDisplay.get();
    *displayObj = pActiveDisplay;
    return ScopedAStatus::ok();
}

ScopedAStatus EvsEnumerator::closeDisplay(const std::shared_ptr<IEvsDisplay>& obj) {
    LOG(DEBUG) << __FUNCTION__;

    auto& displays = mutableActiveDisplays();
    const auto display_search = displays.popDisplay(obj);

    if (!display_search) {
        LOG(WARNING) << "Ignoring close of previously orphaned display - why did a client steal?";
        return ScopedAStatus::ok();
    }

    auto pActiveDisplay = display_search->displayWeak.lock();

    if (!pActiveDisplay) {
        LOG(ERROR) << "Somehow a display is being destroyed "
                   << "when the enumerator didn't know one existed";
        return ScopedAStatus::fromServiceSpecificError(static_cast<int>(EvsResult::OWNERSHIP_LOST));
    }

    pActiveDisplay->forceShutdown();
    return ScopedAStatus::ok();
}

ScopedAStatus EvsEnumerator::getDisplayState(DisplayState* state) {
    LOG(DEBUG) << __FUNCTION__;
    return getDisplayStateImpl(std::nullopt, state);
}

ScopedAStatus EvsEnumerator::getDisplayStateById(int32_t displayId, DisplayState* state) {
    LOG(DEBUG) << __FUNCTION__;
    return getDisplayStateImpl(displayId, state);
}

ScopedAStatus EvsEnumerator::getDisplayStateImpl(std::optional<int32_t> displayId,
        DisplayState* state) {
    if (!checkPermission()) {
        *state = DisplayState::DEAD;
        return ScopedAStatus::fromServiceSpecificError(
                   static_cast<int>(EvsResult::PERMISSION_DENIED));
    }

    const auto& all_displays = mutableActiveDisplays().getAllDisplays();

    const auto display_search = displayId ? all_displays.find(*displayId) : all_displays.begin();

    if (display_search == all_displays.end()) {
        *state = DisplayState::NOT_OPEN;
        return ScopedAStatus::fromServiceSpecificError(static_cast<int>(EvsResult::OWNERSHIP_LOST));
    }

    std::shared_ptr<IEvsDisplay> pActiveDisplay = display_search->second.displayWeak.lock();
    if (pActiveDisplay) {
        return pActiveDisplay->getDisplayState(state);
    } else {
        *state = DisplayState::NOT_OPEN;
        return ScopedAStatus::fromServiceSpecificError(static_cast<int>(EvsResult::OWNERSHIP_LOST));
    }
}

ScopedAStatus EvsEnumerator::getDisplayIdList(std::vector<uint8_t>* list) {
    std::vector<uint8_t>& output = *list;
    if (sDisplayPortList.size() > 0) {
        output.resize(sDisplayPortList.size());
        unsigned i = 0;
        output[i++] = mInternalDisplayId & 0xFF;
        for (const auto& [port, id] : sDisplayPortList) {
            if (mInternalDisplayId != id) {
                output[i++] = port;
            }
        }
    }

    return ScopedAStatus::ok();
}

ScopedAStatus EvsEnumerator::isHardware(bool* flag) {
    *flag = true;
    return ScopedAStatus::ok();
}

void EvsEnumerator::notifyDeviceStatusChange(const std::string_view& deviceName,
        DeviceStatusType type) {
    std::lock_guard lock(sLock);
    if (!mCallback) {
        return;
    }

    std::vector<DeviceStatus> status{{.id = std::string(deviceName), .status = type}};
    if (!mCallback->deviceStatusChanged(status).isOk()) {
        LOG(WARNING) << "Failed to notify a device status change, name = " << deviceName
                     << ", type = " << static_cast<int>(type);
    }
}

ScopedAStatus EvsEnumerator::registerStatusCallback(
    const std::shared_ptr<IEvsEnumeratorStatusCallback>& callback) {
    std::lock_guard lock(sLock);
    if (mCallback) {
        LOG(INFO) << "Replacing an existing device status callback";
    }
    mCallback = callback;
    return ScopedAStatus::ok();
}

std::optional<EvsEnumerator::ActiveDisplays::DisplayInfo> EvsEnumerator::ActiveDisplays::popDisplay(
    int32_t id) {
    std::lock_guard lck(mMutex);
    const auto search = mIdToDisplay.find(id);
    if (search == mIdToDisplay.end()) {
        return std::nullopt;
    }
    const auto display_info = search->second;
    mIdToDisplay.erase(search);
    mDisplayToId.erase(display_info.internalDisplayRawAddr);
    return display_info;
}

std::optional<EvsEnumerator::ActiveDisplays::DisplayInfo> EvsEnumerator::ActiveDisplays::popDisplay(
    std::shared_ptr<IEvsDisplay> display) {
    const auto display_ptr_val = reinterpret_cast<uintptr_t>(display.get());
    std::lock_guard lck(mMutex);
    const auto display_to_id_search = mDisplayToId.find(display_ptr_val);
    if (display_to_id_search == mDisplayToId.end()) {
        LOG(ERROR) << "Unknown display.";
        return std::nullopt;
    }
    const auto id = display_to_id_search->second;
    const auto id_to_display_search = mIdToDisplay.find(id);
    mDisplayToId.erase(display_to_id_search);
    if (id_to_display_search == mIdToDisplay.end()) {
        LOG(ERROR) << "No correspsonding ID for the display, probably orphaned.";
        return std::nullopt;
    }
    const auto display_info = id_to_display_search->second;
    mIdToDisplay.erase(id);
    return display_info;
}

std::unordered_map<int32_t, EvsEnumerator::ActiveDisplays::DisplayInfo>
EvsEnumerator::ActiveDisplays::getAllDisplays() {
    std::lock_guard lck(mMutex);
    const auto id_to_display_map_copy = mIdToDisplay;
    return id_to_display_map_copy;
}

bool EvsEnumerator::ActiveDisplays::tryInsert(int32_t id, std::shared_ptr<EvsGlDisplay> display) {
    std::lock_guard lck(mMutex);
    const auto display_ptr_val = reinterpret_cast<uintptr_t>(display.get());

    auto id_to_display_insert_result =
        mIdToDisplay.emplace(
            id,
            DisplayInfo{
                .id = id,
                .displayWeak = display,
                .internalDisplayRawAddr = display_ptr_val,
            });
    if (!id_to_display_insert_result.second) {
        return false;
    }
    auto display_to_id_insert_result = mDisplayToId.emplace(display_ptr_val, id);
    if (!display_to_id_insert_result.second) {
        mIdToDisplay.erase(id);
        return false;
    }
    return true;
}

ScopedAStatus EvsEnumerator::getUltrasonicsArrayList(
    [[maybe_unused]] std::vector<UltrasonicsArrayDesc>* list) {
    // TODO(b/149874793): Add implementation for EVS Manager and Sample driver
    return ScopedAStatus::ok();
}

ScopedAStatus EvsEnumerator::openUltrasonicsArray(
    [[maybe_unused]] const std::string& id,
    [[maybe_unused]] std::shared_ptr<IEvsUltrasonicsArray>* obj) {
    // TODO(b/149874793): Add implementation for EVS Manager and Sample driver
    return ScopedAStatus::ok();
}

ScopedAStatus EvsEnumerator::closeUltrasonicsArray(
    [[maybe_unused]] const std::shared_ptr<IEvsUltrasonicsArray>& obj) {
    // TODO(b/149874793): Add implementation for EVS Manager and Sample driver
    return ScopedAStatus::ok();
}

binder_status_t EvsEnumerator::dump(int fd, const char** args, uint32_t numArgs) {
    std::vector<std::string> options(args, args + numArgs);
    return parseCommand(fd, options);
}

binder_status_t EvsEnumerator::parseCommand(int fd, const std::vector<std::string>& options) {
    if (options.size() < 1) {
        WriteStringToFd("No option is given.\n", fd);
        cmdHelp(fd);
        return STATUS_BAD_VALUE;
    }

    const std::string command = options[0];
    if (EqualsIgnoreCase(command, "--help")) {
        cmdHelp(fd);
        return STATUS_OK;
    } else if (EqualsIgnoreCase(command, "--dump")) {
        return cmdDump(fd, options);
    } else {
        WriteStringToFd(StringPrintf("Invalid option: %s\n", command.data()), fd);
        return STATUS_INVALID_OPERATION;
    }
}

void EvsEnumerator::cmdHelp(int fd) {
    WriteStringToFd("--help: shows this help.\n"
                    "--dump [id] [start|stop] [directory]\n"
                    "\tDump camera frames to a target directory\n",
                    fd);
}

binder_status_t EvsEnumerator::cmdDump(int fd, const std::vector<std::string>& options) {
    if (options.size() < 3) {
        WriteStringToFd("Necessary argument is missing\n", fd);
        cmdHelp(fd);
        return STATUS_BAD_VALUE;
    }

    auto pRecord = std::find_if( std::begin( sOpenCameraList ),
                                 std::end( sOpenCameraList ),
                                 [&]( const CameraRecord &rec ) {
                                     return options[1] == ( rec.name );
                                 } );
    if (pRecord == sOpenCameraList.end()) {
        WriteStringToFd(StringPrintf("%s is not active\n", options[1].data()), fd);
        return STATUS_BAD_VALUE;
    }

    auto device = pRecord->activeInstance.lock();
    if (device == nullptr) {
        WriteStringToFd(StringPrintf("%s seems dead\n", options[1].data()), fd);
        return STATUS_DEAD_OBJECT;
    }

    const std::string command = options[2];
    if (EqualsIgnoreCase(command, "start")) {
        // --dump [device id] start [path]
        if (options.size() < 4) {
            WriteStringToFd("Necessary argument is missing\n", fd);
            cmdHelp(fd);
            return STATUS_BAD_VALUE;
        }

        const std::string path = options[3];
        auto ret = device->startDumpFrames(path);
        if (!ret.ok()) {
            WriteStringToFd(StringPrintf("Failed to start storing frames: %s\n",
                                         ret.error().message().data()),
                            fd);
            return STATUS_FAILED_TRANSACTION;
        }
    } else if (EqualsIgnoreCase(command, "stop")) {
        // --dump [device id] stop
        auto ret = device->stopDumpFrames();
        if (!ret.ok()) {
            WriteStringToFd(StringPrintf("Failed to stop storing frames: %s\n",
                                         ret.error().message().data()),
                            fd);
            return STATUS_FAILED_TRANSACTION;
        }
    } else {
        WriteStringToFd(StringPrintf("Unknown command: %s", command.data()), fd);
        cmdHelp(fd);
    }

    return STATUS_OK;
}

}  // namespace aidl::android::hardware::automotive::evs::implementation
