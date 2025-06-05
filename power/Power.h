/*
 * Copyright (C) 2021 The Android Open Source Project
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

#pragma once

#include <aidl/android/hardware/power/BnPower.h>
#include <perfmgr/HintManager.h>
#include <fmq/AidlMessageQueue.h>

#include <atomic>
#include <memory>
#include <thread>

#include "AdpfTypes.h"
#include "PowerHintSession.h"

namespace aidl {
namespace android {
namespace hardware {
namespace power {
namespace impl {

using ::aidl::android::hardware::power::Boost;
using ::aidl::android::hardware::power::IPowerHintSession;
using ::aidl::android::hardware::power::Mode;
using ::aidl::android::hardware::common::fmq::SynchronizedReadWrite;
using ::android::perfmgr::HintManager;
using ::android::AidlMessageQueue;
using namespace std::chrono_literals;

class Power : public ::aidl::android::hardware::power::BnPower {
public:
    Power(HintManager *hm);
    ndk::ScopedAStatus setMode(Mode type, bool enabled) override;
    ndk::ScopedAStatus isModeSupported(Mode type, bool* _aidl_return) override;
    ndk::ScopedAStatus setBoost(Boost type, int32_t durationMs) override;
    ndk::ScopedAStatus isBoostSupported(Boost type, bool* _aidl_return) override;
    ndk::ScopedAStatus getCpuHeadroom(const CpuHeadroomParams& params,
                                      CpuHeadroomResult* _aidl_return) override;
    ndk::ScopedAStatus getGpuHeadroom(const GpuHeadroomParams& params,
                                      GpuHeadroomResult* _aidl_return) override;
    binder_status_t dump(int fd, const char** args, uint32_t numArgs) override;
    ndk::ScopedAStatus createHintSession(int32_t tgid, int32_t uid,
                                         const std::vector<int32_t>& threadIds,
                                         int64_t durationNanos,
                                         std::shared_ptr<IPowerHintSession>* _aidl_return) override;
    ndk::ScopedAStatus createHintSessionWithConfig(
            int32_t tgid, int32_t uid, const std::vector<int32_t>& threadIds, int64_t durationNanos,
            SessionTag tag, SessionConfig* config,
            std::shared_ptr<IPowerHintSession>* _aidl_return) override;
    ndk::ScopedAStatus getHintSessionPreferredRate(int64_t* outNanoseconds) override;
    ndk::ScopedAStatus getSessionChannel(int32_t tgid, int32_t uid,
                                         ChannelConfig* _aidl_return) override;
    ndk::ScopedAStatus closeSessionChannel(int32_t tgid, int32_t uid) override;
    ndk::ScopedAStatus getSupportInfo(SupportInfo *_aidl_return);
    ndk::ScopedAStatus sendCompositionData(const std::vector<CompositionData> &in_data) override;
    ndk::ScopedAStatus sendCompositionUpdate(const CompositionUpdate &in_update) override;

private:
    HintManager *mHintManager;
    void initSupportStatus();
    std::atomic<bool> mVRModeOn;
    std::atomic<bool> mSustainedPerfModeOn;
    std::vector<std::shared_ptr<IPowerHintSession>> mPowerHintSessions;
    int32_t mServiceVersion;
    SupportInfo mSupportInfo;
};

} // namespace impl
} // namespace power
} // namespace hardware
} // namespace android
} // namespace aidl
