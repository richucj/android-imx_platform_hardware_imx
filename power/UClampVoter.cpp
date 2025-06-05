/*
 * Copyright 2023 The Android Open Source Project
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

#include "UClampVoter.h"

namespace aidl {
namespace android {
namespace hardware {
namespace power {
namespace impl {

static void confine(UclampRange &uclampRange, const CpuVote &cpu_vote,
                    std::chrono::steady_clock::time_point t) {
    if (!cpu_vote.isTimeInRange(t)) {
        return;
    }
    uclampRange.uclampMin = std::max(uclampRange.uclampMin, cpu_vote.mUclampRange.uclampMin);
    uclampRange.uclampMax = std::min(uclampRange.uclampMax, cpu_vote.mUclampRange.uclampMax);
}

std::ostream &operator<<(std::ostream &o, const UclampRange &uc) {
    o << "[" << uc.uclampMin << "," << uc.uclampMax << "]";
    return o;
}

Votes::Votes() {}

void Votes::add(int id, CpuVote const &vote) {
        mCpuVotes[id] = vote;
}

void Votes::updateDuration(int voteId, std::chrono::nanoseconds durationNs) {
    auto const voteItr = mCpuVotes.find(voteId);
    if (voteItr != mCpuVotes.end()) {
        voteItr->second.updateDuration(durationNs);
    }
}

void Votes::getUclampRange(UclampRange &uclampRange,
                           std::chrono::steady_clock::time_point t) const {
    for (auto it = mCpuVotes.begin(); it != mCpuVotes.end(); it++) {
        auto timings_it = mCpuVotes.find(it->first);
        confine(uclampRange, it->second, t);
    }
}

bool Votes::anyTimedOut(std::chrono::steady_clock::time_point t) const {
    for (const auto &v : mCpuVotes) {
        if (!v.second.isTimeInRange(t)) {
            return true;
        }
    }
    return false;
}

bool Votes::allTimedOut(std::chrono::steady_clock::time_point t) const {
    for (const auto &v : mCpuVotes) {
        if (v.second.isTimeInRange(t)) {
            return false;
        }
    }
    return true;
}

bool Votes::remove(int voteId) {
    auto const it = mCpuVotes.find(voteId);
    if (it != mCpuVotes.end()) {
        mCpuVotes.erase(it);
        return true;
    }

    return false;
}

bool Votes::setUseVote(int voteId, bool active) {
    auto const itr = mCpuVotes.find(voteId);
    if (itr == mCpuVotes.end()) {
        return false;
    }
    itr->second.setActive(active);
    return true;
}

size_t Votes::size() const {
    return mCpuVotes.size();
}

bool Votes::voteIsActive(int voteId) const {
    auto const itr = mCpuVotes.find(voteId);
    if (itr == mCpuVotes.end()) {
        return false;
    }
    return itr->second.active();
}

std::chrono::steady_clock::time_point Votes::voteTimeout(int voteId) const {
    auto const itr = mCpuVotes.find(voteId);
    if (itr == mCpuVotes.end()) {
        return std::chrono::steady_clock::time_point{};
    }
    return itr->second.startTime() + itr->second.durationNs();
}

}  // namespace impl
}  // namespace power
}  // namespace hardware
}  // namespace android
}  // namespace aidl
