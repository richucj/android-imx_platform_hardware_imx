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

#include <algorithm>
#include <string>
#include <string_view>
#include <unordered_map>
#include <queue>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/strings.h>

namespace aidl::android::hardware::thermal::impl::imx {

class ThermalFiles {
  public:
    ThermalFiles() = default;
    ~ThermalFiles() = default;
    ThermalFiles(const ThermalFiles &) = delete;
    void operator=(const ThermalFiles &) = delete;

    std::string getThermalFilePath(std::string_view thermal_name) const;
    // Returns true if add was successful, false otherwise.
    bool addThermalFile(std::string_view thermal_name, std::string_view path);
    // If thermal_name is not found in the thermal names to path map, this will set
    // data to empty and return false. If the thermal_name is found and its content
    // is read, this function will fill in data accordingly then return true.
    bool readThermalFile(std::string_view thermal_name, std::string *data) const;
    size_t getNumThermalFiles() const { return thermal_name_to_path_map_.size(); }

  private:
    std::unordered_map<std::string, std::string> thermal_name_to_path_map_;
};

} // namespace aidl::android::hardware::thermal::impl::imx
