/*
 * Copyright (C) 2022 The Android Open Source Project
 * Copyright 2024 NXP
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
#ifndef CPP_EVS_SAMPLEDRIVER_AIDL_INCLUDE_VIDEOCAPTURE_H
#define CPP_EVS_SAMPLEDRIVER_AIDL_INCLUDE_VIDEOCAPTURE_H

#include <linux/videodev2.h>

#include <atomic>
#include <functional>
#include <set>
#include <thread>

#define V4L2_BUFFER_NUM 10
typedef v4l2_buffer imageBuffer;

typedef struct {
    void *start;
    size_t length;
} PixelBuffers;

class VideoCapture final {
public:
    bool open(const char* deviceName, const int32_t width = 0, const int32_t height = 0, int pixel_format = 0);
    void close();

    bool startStream(std::function<void(VideoCapture*, imageBuffer&, void*)> callback = nullptr);
    void stopStream();

    // Valid only after open()
    __u32 getWidth() { return mWidth; };
    __u32 getHeight() { return mHeight; };
    __u32 getStride() { return mStride; };
    __u32 getV4LFormat() { return mFormat; };

    bool isOpen() { return mDeviceFd >= 0; }

    int setParameter(struct v4l2_control& control);
    int getParameter(struct v4l2_control& control);
    std::set<uint32_t> enumerateCameraControls();
    bool queueFB(int index, int fd, int size); // Queue buffer to camera driver. API for EvsV4lCamera managing buffer states.

private:
    void collectFrames();

    int mDeviceFd = -1;

    __u32 mFormat = 0;
    __u32 mWidth = 0;
    __u32 mHeight = 0;
    __u32 mStride = 0;

    std::function<void(VideoCapture*, imageBuffer&, void*)> mCallback;

    std::thread mCaptureThread;  // The thread we'll use to dispatch frames
    std::atomic<int> mRunMode;   // Used to signal the frame loop (see RunModes below)

    // Careful changing these -- we're using bit-wise ops to manipulate these
    enum RunModes {
        STOPPED = 0,
        RUN = 1,
        STOPPING = 2,
    };
};

#endif  // CPP_EVS_SAMPLEDRIVER_AIDL_INCLUDE_VIDEOCAPTURE_H
