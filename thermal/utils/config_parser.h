/*
 * Copyright (C) 2018 The Android Open Source Project
 * Copyright 2023 NXP
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

#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/strings.h>

#include <json/reader.h>
#include <json/value.h>

#include <aidl/android/hardware/thermal/BnThermal.h>

namespace aidl::android::hardware::thermal::impl::imx {

constexpr size_t kThrottlingSeverityCount =
        std::distance(::ndk::enum_range<ThrottlingSeverity>().begin(),
                      ::ndk::enum_range<ThrottlingSeverity>().end());
using ThrottlingArray = std::array<float, static_cast<size_t>(kThrottlingSeverityCount)>;
using ::android::base::ReadFileToString;

struct SensorInfo {
    TemperatureType type;
    ThrottlingArray hot_thresholds;
    ThrottlingArray cold_thresholds;
    ThrottlingArray hot_hysteresis;
    ThrottlingArray cold_hysteresis;
    float multiplier;
    bool is_monitor;
};

std::map<std::string, SensorInfo> ParseSensorInfo(std::string_view config_path);
std::map<std::string, CoolingType> ParseCoolingDevice(std::string_view config_path);
std::vector<std::string> ParseHotplugCPUInfo(std::string_view config_path);
} // namespace aidl::android::hardware::thermal::impl::imx
