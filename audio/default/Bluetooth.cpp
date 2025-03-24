/*
 * Copyright (C) 2022 The Android Open Source Project
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

#define LOG_TAG "AHAL_Bluetooth"
#include <android-base/logging.h>

#include "core-impl/Bluetooth.h"

using aidl::android::hardware::audio::core::VendorParameter;
using aidl::android::media::audio::common::Boolean;
using aidl::android::media::audio::common::Float;
using aidl::android::media::audio::common::Int;

namespace aidl::android::hardware::audio::core {

Bluetooth::Bluetooth() {
    mScoConfig.isEnabled = Boolean{false};
    mScoConfig.isNrecEnabled = Boolean{false};
    mScoConfig.mode = ScoConfig::Mode::SCO;
    mHfpConfig.isEnabled = Boolean{false};
    mHfpConfig.sampleRate = Int{8000};
    mHfpConfig.volume = Float{HfpConfig::VOLUME_MAX};

    pcm_config_hfp = {
        .channels = 1,
        .rate = 16000,
        .period_size = 256,
        .period_count = 4,
        .format = PCM_FORMAT_S16_LE,
        .start_threshold = 0,
        .avail_min = 0,
    };
}

ndk::ScopedAStatus Bluetooth::setScoConfig(const ScoConfig& in_config, ScoConfig* _aidl_return) {
    if (in_config.isEnabled.has_value()) {
        mScoConfig.isEnabled = in_config.isEnabled;
    }
    if (in_config.isNrecEnabled.has_value()) {
        mScoConfig.isNrecEnabled = in_config.isNrecEnabled;
    }
    if (in_config.mode != ScoConfig::Mode::UNSPECIFIED) {
        mScoConfig.mode = in_config.mode;
    }
    if (in_config.debugName.has_value()) {
        mScoConfig.debugName = in_config.debugName;
    }
    *_aidl_return = mScoConfig;
    LOG(DEBUG) << __func__ << ": received " << in_config.toString() << ", returning "
               << _aidl_return->toString();
    return ndk::ScopedAStatus::ok();
}

int Bluetooth::openPcmForDevice(const audio_devices_t& audioDevice, unsigned int flags,
        struct pcm_config *config, struct pcm** pcm)
{
    if (!config || !pcm) {
        return -EINVAL;
    }
    auto card = AudioCardManager::getCardForDevice(audioDevice);
    if (!card) {
        LOG(ERROR) << "card is not found";
        return -EINVAL;
    }

    *pcm = pcm_open(card->card, 0, flags, config);
    if (!*pcm) {
        LOG(ERROR) << "pcm_open() failed: " << pcm_get_error(*pcm);
        return -EBUSY;
    }
    if (!pcm_is_ready(*pcm)) {
        LOG(ERROR) << "pcm_is_ready() failed: " << pcm_get_error(*pcm);
        pcm_close(*pcm);
        *pcm = NULL;
        return -EBUSY;
    }

    LOG(INFO) << "  channels: " << config->channels;
    LOG(INFO) << "  rate: " << config->rate;
    LOG(INFO) << "  period_size: " << config->period_size;
    LOG(INFO) << "  period_count: " << config->period_count;
    LOG(INFO) << "  format: " << config->format;
    return 0;
}

void Bluetooth::stopHfp() {
    LOG(DEBUG) << __func__;
    if (tid_uplink) {
        uplink_running = false;
        pthread_join(tid_uplink, NULL);
        tid_uplink = 0;
    }

    if (tid_downlink) {
        downlink_running = false;
        pthread_join(tid_downlink, NULL);
        tid_downlink = 0;
    }

    if (pcm_speaker_out) {
        pcm_close(pcm_speaker_out);
        pcm_speaker_out = NULL;
    }

    if (pcm_mic_in) {
        pcm_close(pcm_mic_in);
        pcm_mic_in = NULL;
    }

    if (pcm_sco_out) {
        pcm_close(pcm_sco_out);
        pcm_sco_out = NULL;
    }

    if (pcm_sco_in) {
        pcm_close(pcm_sco_in);
        pcm_sco_in = NULL;
    }
}

void* Bluetooth::uplink_task(void* arg) {
    Bluetooth* instance = static_cast<Bluetooth*>(arg);
    instance->uplink_task_impl();
    return nullptr;
}

void *Bluetooth::uplink_task_impl()
{
    int ret = 0;
    size_t size = pcm_config_hfp.period_size;
    void *buffer = malloc(size);

    if (!buffer) {
        LOG(ERROR) << __func__ << "Failed to alloc " << size << " bytes";
        return NULL;
    }

    LOG(ERROR) << __func__ << " start";
    while (uplink_running) {
        ret = pcm_read(pcm_mic_in, buffer, size);
        if (ret) {
            LOG(ERROR) << __func__ << "pcm read failed: " << pcm_get_error(pcm_mic_in);
            continue;
        }
        ret = pcm_write(pcm_sco_out, buffer, size);
        if (ret) {
            LOG(ERROR) << __func__ << "pcm write failed: " << pcm_get_error(pcm_sco_out);
            continue;
        }
    }

    free(buffer);
    LOG(ERROR) << __func__ << " stop";
    return NULL;
}

void* Bluetooth::downlink_task(void* arg) {
    Bluetooth* instance = static_cast<Bluetooth*>(arg);
    instance->downlink_task_impl();
    return nullptr;
}

void *Bluetooth::downlink_task_impl()
{
    int ret = 0;
    size_t size = pcm_config_hfp.period_size;
    void *buffer = malloc(size);

    if (!buffer) {
        LOG(ERROR) << __func__ << "Failed to alloc " << size << " bytes";
        return NULL;
    }

    LOG(ERROR) << __func__ << " start";
    while (downlink_running) {
        ret = pcm_read(pcm_sco_in, buffer, size);
        if (ret) {
            LOG(ERROR) << __func__ << "pcm read failed: " << pcm_get_error(pcm_sco_in);
            continue;
        }
        ret = pcm_write(pcm_speaker_out, buffer, size);
        if (ret) {
            LOG(ERROR) << __func__ << "pcm write failed: " << pcm_get_error(pcm_speaker_out);
            continue;
        }
    }

    free(buffer);
    LOG(ERROR) << __func__ << " stop";
    return NULL;
}

void Bluetooth::startHfp() {
    LOG(DEBUG) << __func__;
    int ret = 0;
    ret = openPcmForDevice(AUDIO_DEVICE_OUT_SPEAKER, PCM_OUT,
                &pcm_config_hfp, &pcm_speaker_out);
    if (ret)
        goto error;

    ret = openPcmForDevice(AUDIO_DEVICE_IN_BUILTIN_MIC, PCM_IN,
                &pcm_config_hfp, &pcm_mic_in);
    if (ret)
        goto error;

    ret = openPcmForDevice(AUDIO_DEVICE_OUT_BLUETOOTH_SCO, PCM_OUT,
                &pcm_config_hfp, &pcm_sco_out);
    if (ret)
        goto error;

    ret = openPcmForDevice(AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET, PCM_IN,
                &pcm_config_hfp, &pcm_sco_in);
    if (ret)
        goto error;

    pthread_attr_t attr;
    struct sched_param schParam;

    pthread_attr_init(&attr);
    pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
    schParam.sched_priority = 3;
    pthread_attr_setschedparam(&attr, &schParam);

    uplink_running = true;
    ret = pthread_create(&tid_uplink, &attr, &uplink_task, this);
    if (ret) {
        goto error;
    }

    downlink_running = true;
    ret = pthread_create(&tid_downlink, &attr, &downlink_task, this);
    if (ret) {
        goto error;
    }

    return;
error:
    stopHfp();
    return;
}

ndk::ScopedAStatus Bluetooth::setHfpConfig(const HfpConfig& in_config, HfpConfig* _aidl_return) {
    if (in_config.sampleRate.has_value() && in_config.sampleRate.value().value <= 0) {
        LOG(ERROR) << __func__ << ": invalid sample rate: " << in_config.sampleRate.value().value;
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    if (in_config.volume.has_value() && (in_config.volume.value().value < HfpConfig::VOLUME_MIN ||
                                         in_config.volume.value().value > HfpConfig::VOLUME_MAX)) {
        LOG(ERROR) << __func__ << ": invalid volume: " << in_config.volume.value().value;
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }

    if (in_config.isEnabled.has_value()) {
        mHfpConfig.isEnabled = in_config.isEnabled;
        if (mHfpConfig.isEnabled == Boolean{true})
            startHfp();
        else
            stopHfp();

    }
    if (in_config.sampleRate.has_value()) {
        mHfpConfig.sampleRate = in_config.sampleRate;
        pcm_config_hfp.rate = mHfpConfig.sampleRate.value().value;
    }
    if (in_config.volume.has_value()) {
        mHfpConfig.volume = in_config.volume;
    }
    *_aidl_return = mHfpConfig;
    LOG(DEBUG) << __func__ << ": received " << in_config.toString() << ", returning "
               << _aidl_return->toString();
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus BluetoothA2dp::isEnabled(bool* _aidl_return) {
    *_aidl_return = mEnabled;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus BluetoothA2dp::setEnabled(bool in_enabled) {
    mEnabled = in_enabled;
    LOG(DEBUG) << __func__ << ": " << mEnabled;
    if (mHandler) return mHandler();
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus BluetoothA2dp::supportsOffloadReconfiguration(bool* _aidl_return) {
    *_aidl_return = false;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus BluetoothA2dp::reconfigureOffload(
        const std::vector<::aidl::android::hardware::audio::core::VendorParameter>& in_parameters
                __unused) {
    LOG(DEBUG) << __func__ << ": " << ::android::internal::ToString(in_parameters);
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus BluetoothLe::isEnabled(bool* _aidl_return) {
    *_aidl_return = mEnabled;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus BluetoothLe::setEnabled(bool in_enabled) {
    mEnabled = in_enabled;
    if (mHandler) return mHandler();
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus BluetoothLe::supportsOffloadReconfiguration(bool* _aidl_return) {
    *_aidl_return = false;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus BluetoothLe::reconfigureOffload(
        const std::vector<::aidl::android::hardware::audio::core::VendorParameter>& in_parameters
                __unused) {
    LOG(DEBUG) << __func__ << ": " << ::android::internal::ToString(in_parameters);
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

}  // namespace aidl::android::hardware::audio::core
