/*
 * Copyright (C) 2020 The Android Open Source Project
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

#define LOG_TAG "android.hardware.power-service.imx"

#include "Power.h"

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android-base/strings.h>
#include <fmq/AidlMessageQueue.h>
#include <fmq/EventFlag.h>
#include <perfmgr/HintManager.h>
#include <utils/Log.h>

#include <cstdint>
#include <memory>
#include <optional>

#include "AdpfTypes.h"
#include "ChannelManager.h"
#include "PowerHintSession.h"
#include "PowerSessionManager.h"
#include "SupportManager.h"

#define BOOSTWIDEVINE 0xff

namespace aidl {
namespace android {
namespace hardware {
namespace power {
namespace impl {
using ::android::perfmgr::HintManager;

constexpr char kPowerHalStateProp[] = "vendor.powerhal.state";
constexpr char kPowerHalAudioProp[] = "vendor.powerhal.audio";
constexpr char kPowerHalRenderingProp[] = "vendor.powerhal.rendering";
static struct timespec s_previous_boost_timespec;
static int s_previous_duration;

#define USINSEC 1000000L
#define NSINUS 1000L

Power::Power(HintManager *hm)
    : mHintManager(hm),
      mVRModeOn(false),
      mSustainedPerfModeOn(false) {

    std::string state = ::android::base::GetProperty(kPowerHalStateProp, "");
    if (state == "SUSTAINED_PERFORMANCE") {
        LOG(INFO) << "Initialize with SUSTAINED_PERFORMANCE on";
        mHintManager->DoHint("SUSTAINED_PERFORMANCE");
        mSustainedPerfModeOn = true;
    } else if (state == "VR") {
        LOG(INFO) << "Initialize with VR on";
        mHintManager->DoHint(state);
        mVRModeOn = true;
    } else if (state == "VR_SUSTAINED_PERFORMANCE") {
        LOG(INFO) << "Initialize with SUSTAINED_PERFORMANCE and VR on";
        mHintManager->DoHint("VR_SUSTAINED_PERFORMANCE");
        mSustainedPerfModeOn = true;
        mVRModeOn = true;
    } else {
        LOG(INFO) << "Initialize PowerHAL";
    }

    state = ::android::base::GetProperty(kPowerHalAudioProp, "");
    if (state == "AUDIO_STREAMING_LOW_LATENCY") {
        LOG(INFO) << "Initialize with AUDIO_LOW_LATENCY on";
        mHintManager->DoHint(state);
    }

    state = ::android::base::GetProperty(kPowerHalRenderingProp, "");
    if (state == "EXPENSIVE_RENDERING") {
        LOG(INFO) << "Initialize with EXPENSIVE_RENDERING on";
        mHintManager->DoHint("EXPENSIVE_RENDERING");
    }

    auto status = this->getInterfaceVersion(&mServiceVersion);
    LOG(INFO) << "PowerHAL InterfaceVersion:" << mServiceVersion << " isOK: " << status.isOk();

    mSupportInfo = SupportManager::makeSupportInfo();
}

ndk::ScopedAStatus Power::setMode(Mode type, bool enabled) {
    LOG(DEBUG) << "Power setMode: " << toString(type) << " to: " << enabled;
    if (mHintManager->IsAdpfSupported()) {
        PowerSessionManager<>::getInstance()->updateHintMode(toString(type), enabled);
    }
    switch (type) {
        case Mode::LOW_POWER:
            if (enabled) {
                mHintManager->DoHint(toString(type));
            } else {
                mHintManager->EndHint(toString(type));
            }
            break;
        case Mode::SUSTAINED_PERFORMANCE:
            if (enabled && !mSustainedPerfModeOn) {
                if (!mVRModeOn) {  // Sustained mode only.
                    mHintManager->DoHint("SUSTAINED_PERFORMANCE");
                } else {  // Sustained + VR mode.
                    mHintManager->EndHint("VR");
                    mHintManager->DoHint("VR_SUSTAINED_PERFORMANCE");
                }
                mSustainedPerfModeOn = true;
            } else if (!enabled && mSustainedPerfModeOn) {
                mHintManager->EndHint("VR_SUSTAINED_PERFORMANCE");
                mHintManager->EndHint("SUSTAINED_PERFORMANCE");
                if (mVRModeOn) {  // Switch back to VR Mode.
                    mHintManager->DoHint("VR");
                }
                mSustainedPerfModeOn = false;
            }
            break;
        case Mode::VR:
            if (enabled && !mVRModeOn) {
                if (!mSustainedPerfModeOn) {  // VR mode only.
                    mHintManager->DoHint("VR");
                } else {  // Sustained + VR mode.
                    mHintManager->EndHint("SUSTAINED_PERFORMANCE");
                    mHintManager->DoHint("VR_SUSTAINED_PERFORMANCE");
                }
                mVRModeOn = true;
            } else if (!enabled && mVRModeOn) {
                mHintManager->EndHint("VR_SUSTAINED_PERFORMANCE");
                mHintManager->EndHint("VR");
                if (mSustainedPerfModeOn) {  // Switch back to sustained Mode.
                    mHintManager->DoHint("SUSTAINED_PERFORMANCE");
                }
                mVRModeOn = false;
            }
            break;
        case Mode::LAUNCH:
            if (mVRModeOn || mSustainedPerfModeOn) {
                break;
            }
            [[fallthrough]];
        case Mode::DOUBLE_TAP_TO_WAKE:
            [[fallthrough]];
        case Mode::FIXED_PERFORMANCE:
            [[fallthrough]];
        case Mode::EXPENSIVE_RENDERING:
            [[fallthrough]];
        case Mode::INTERACTIVE:
            [[fallthrough]];
        case Mode::DEVICE_IDLE:
            [[fallthrough]];
        case Mode::DISPLAY_INACTIVE:
            [[fallthrough]];
        case Mode::AUDIO_STREAMING_LOW_LATENCY:
            [[fallthrough]];
        case Mode::CAMERA_STREAMING_SECURE:
            [[fallthrough]];
        case Mode::CAMERA_STREAMING_LOW:
            [[fallthrough]];
        case Mode::CAMERA_STREAMING_MID:
            [[fallthrough]];
        case Mode::CAMERA_STREAMING_HIGH:
            [[fallthrough]];
        case Mode::GAME_LOADING:
            [[fallthrough]];
        default:
            if (enabled) {
                mHintManager->DoHint(toString(type));
            } else {
                mHintManager->EndHint(toString(type));
            }
            break;
    }

    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Power::isModeSupported(Mode type, bool *_aidl_return) {
    bool supported = supportFromBitset(mSupportInfo.modes, type);
    LOG(INFO) << "Power Mode " << toString(type) << " isModeSupported: " << supported;
    *_aidl_return = supported;
    return ndk::ScopedAStatus::ok();
}

static long long calc_timespan_us(struct timespec start, struct timespec end) {
    long long diff_in_us = 0;
    diff_in_us += (end.tv_sec - start.tv_sec) * USINSEC;
    diff_in_us += (end.tv_nsec - start.tv_nsec) / NSINUS;
    return diff_in_us;
}

ndk::ScopedAStatus Power::setBoost(Boost type, int32_t durationMs) {
    LOG(DEBUG) << "Power setBoost: " << toString(type) << " duration: " << durationMs;
    switch (type) {
        case Boost::INTERACTION:
            if (mVRModeOn || mSustainedPerfModeOn) {
                break;
            } else {
                int duration = 1500; // 1.5s by default
                if (durationMs) {
                        int input_duration = durationMs + 750;
                        if (input_duration > duration) {
                                duration = (input_duration > 5750) ? 5750 : input_duration;
                        }
                }
                struct timespec cur_boost_timespec;
                clock_gettime(CLOCK_MONOTONIC, &cur_boost_timespec);

                long long elapsed_time =
                        calc_timespan_us(s_previous_boost_timespec, cur_boost_timespec);
                // don't hint if previous hint's duration covers this hint's duration
                if (((long long)s_previous_duration * 1000) > (elapsed_time + duration * 1000)) {
                        break;
                }

                s_previous_boost_timespec = cur_boost_timespec;
                s_previous_duration = duration;

                mHintManager->DoHint("INTERACTION", std::chrono::seconds(1));
            }
            break;
        case Boost::DISPLAY_UPDATE_IMMINENT:
            [[fallthrough]];
        case Boost::ML_ACC:
            [[fallthrough]];
        case Boost::AUDIO_LAUNCH:
            [[fallthrough]];
        case Boost::CAMERA_LAUNCH:
            [[fallthrough]];
        case Boost::CAMERA_SHOT:
            [[fallthrough]];
        default:
            if (mSustainedPerfModeOn) {
                break;
            }
            std::string strType;
            if (static_cast<int>(type) == BOOSTWIDEVINE)
                strType = "BOOSTWIDEVINE";
            else
                strType = toString(type);

            if (durationMs > 0) {
                mHintManager->DoHint(strType, std::chrono::milliseconds(durationMs));
            } else if (durationMs == 0) {
                mHintManager->DoHint(strType);
            } else {
                mHintManager->EndHint(strType);
            }
            break;
    }

    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Power::isBoostSupported(Boost type, bool *_aidl_return) {
    bool supported = supportFromBitset(mSupportInfo.boosts, type);
    LOG(INFO) << "Power oost " << toString(type) << " isBoostSupported: " << supported;
    *_aidl_return = supported;
    return ndk::ScopedAStatus::ok();
}

constexpr const char *boolToString(bool b) {
    return b ? "true" : "false";
}

binder_status_t Power::dump(int fd, const char **, uint32_t) {
    std::string buf(::android::base::StringPrintf(
            "HintManager Running: %s\n"
            "VRMode: %s\n"
            "SustainedPerformanceMode: %s\n",
            boolToString(mHintManager->IsRunning()), boolToString(mVRModeOn),
            boolToString(mSustainedPerfModeOn)));
    // Dump nodes through libperfmgr
    mHintManager->DumpToFd(fd);
    PowerSessionManager<>::getInstance()->dumpToFd(fd);
    if (!::android::base::WriteStringToFd(buf, fd)) {
        PLOG(ERROR) << "Failed to dump state to fd";
    }
    fsync(fd);
    return STATUS_OK;
}

ndk::ScopedAStatus Power::getCpuHeadroom(const CpuHeadroomParams &_, CpuHeadroomResult *) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus Power::getGpuHeadroom(const GpuHeadroomParams &_, GpuHeadroomResult *) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus Power::createHintSession(int32_t tgid, int32_t uid,
                                            const std::vector<int32_t> &threadIds,
                                            int64_t durationNanos,
                                            std::shared_ptr<IPowerHintSession> *_aidl_return) {
    SessionConfig config;
    return createHintSessionWithConfig(tgid, uid, threadIds, durationNanos, SessionTag::OTHER,
                                       &config, _aidl_return);
}

ndk::ScopedAStatus Power::getHintSessionPreferredRate(int64_t *outNanoseconds) {
    *outNanoseconds = mHintManager->IsAdpfSupported()
                              ? mHintManager->GetAdpfProfile()->mReportingRateLimitNs
                              : 0;
    if (*outNanoseconds <= 0) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }

    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Power::createHintSessionWithConfig(
        int32_t tgid, int32_t uid, const std::vector<int32_t> &threadIds, int64_t durationNanos,
        SessionTag tag, SessionConfig *config, std::shared_ptr<IPowerHintSession> *_aidl_return) {
    if (!mHintManager->IsAdpfSupported()) {
        *_aidl_return = nullptr;
        return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }
    if (threadIds.size() == 0) {
        LOG(ERROR) << "Error: threadIds.size() shouldn't be " << threadIds.size();
        *_aidl_return = nullptr;
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    std::shared_ptr<PowerHintSession<>> session =
            ndk::SharedRefBase::make<PowerHintSession<>>(tgid, uid, threadIds, durationNanos, tag);

    *_aidl_return = session;
    session->getSessionConfig(config);
    PowerSessionManager<>::getInstance()->registerSession(session, config->id);

    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Power::getSessionChannel(int32_t tgid, int32_t uid,
                                            ChannelConfig *_aidl_return) {
    if (ChannelManager<>::getInstance()->getChannelConfig(tgid, uid, _aidl_return)) {
        return ndk::ScopedAStatus::ok();
    }
    return ndk::ScopedAStatus::fromStatus(EX_ILLEGAL_STATE);
}

ndk::ScopedAStatus Power::closeSessionChannel(int32_t tgid, int32_t uid) {
    ChannelManager<>::getInstance()->closeChannel(tgid, uid);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Power::getSupportInfo(SupportInfo *_aidl_return) {
    // Copy the support object into the binder
    *_aidl_return = mSupportInfo;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Power::sendCompositionData(const std::vector<CompositionData> &) {
    LOG(INFO) << "Composition data received!";
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Power::sendCompositionUpdate(const CompositionUpdate &) {
    LOG(INFO) << "Composition update received!";
    return ndk::ScopedAStatus::ok();
}

}  // namespace impl
}  // namespace power
}  // namespace hardware
}  // namespace android
}  // namespace aidl
