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

#include "VideoCapture.h"

#include <android-base/logging.h>
#include "RouteMediactl.h"

#include <errno.h>
#include <error.h>
#include <fcntl.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cassert>
#include <iomanip>

unsigned VideoCapture::sGroupFmt = 0;
std::mutex VideoCapture::mPipelineLock;

// NOTE:  This developmental code does not properly clean up resources in case of failure
//        during the resource setup phase.  Of particular note is the potential to leak
//        the file descriptor.  This must be fixed before using this code for anything but
//        experimentation.
bool VideoCapture::open(const char* deviceName, const int32_t width, const int32_t height, int pixel_format) {
    // If we want a polling interface for getting frames, we would use O_NONBLOCK
    mDeviceFd = ::open(deviceName, O_RDWR, 0);
    if (mDeviceFd < 0) {
        PLOG(ERROR) << "failed to open device " << deviceName;
        return false;
    }

    v4l2_capability caps;
    {
        int result = ioctl(mDeviceFd, VIDIOC_QUERYCAP, &caps);
        if (result < 0) {
            PLOG(ERROR) << "failed to get device caps for " << deviceName;
            return false;
        }
    }

    // Report device properties
    LOG(INFO) << "Open Device: " << deviceName << " (fd = " << mDeviceFd << ")";
    LOG(DEBUG) << "  Driver: " << caps.driver;
    LOG(DEBUG) << "  Card: " << caps.card;
    LOG(DEBUG) << "  Version: " << ((caps.version >> 16) & 0xFF) << "."
               << ((caps.version >> 8) & 0xFF) << "." << (caps.version & 0xFF);
    LOG(DEBUG) << "  All Caps: " << std::hex << std::setw(8) << caps.capabilities;
    LOG(DEBUG) << "  Dev Caps: " << std::hex << caps.device_caps;

    // Enumerate the available capture formats (if any)
    LOG(DEBUG) << "Supported capture formats:";
    v4l2_fmtdesc formatDescriptions;
    formatDescriptions.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    for (int i = 0; true; i++) {
        formatDescriptions.index = i;
        if (ioctl(mDeviceFd, VIDIOC_ENUM_FMT, &formatDescriptions) == 0) {
            LOG(DEBUG) << "  " << std::setw(2) << i << ": " << formatDescriptions.description << " "
                       << std::hex << std::setw(8) << formatDescriptions.pixelformat << " "
                       << std::hex << formatDescriptions.flags;
        } else {
            // No more formats available
            break;
        }
    }

    // Verify we can use this device for video capture
    if (!(caps.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE) ||
        !(caps.capabilities & V4L2_CAP_STREAMING)) {
        // Can't do streaming capture.
        LOG(ERROR) << "Streaming capture not supported by " << deviceName;
        return false;
    }

    // Get capture mode
    int index = 0;
    int capturemode = 0;
    int ret = 0;
    struct v4l2_frmsizeenum vid_frmsize;
    while (ret == 0) {
        vid_frmsize.index = index++;
        vid_frmsize.pixel_format = pixel_format;
        ret = ioctl(mDeviceFd, VIDIOC_ENUM_FRAMESIZES, &vid_frmsize);
        if ((vid_frmsize.discrete.width == (uint32_t)width) &&
            (vid_frmsize.discrete.height == (uint32_t)height)
            && (ret == 0)) {
            capturemode = vid_frmsize.index;
            break;
        }
        if ((vid_frmsize.stepwise.min_width <= (uint32_t)width) &&
            (vid_frmsize.stepwise.max_width >= (uint32_t)width) &&
            (vid_frmsize.stepwise.min_height <= (uint32_t)height) &&
            (vid_frmsize.stepwise.max_height >= (uint32_t)height)
            && (ret == 0)) {
            capturemode = vid_frmsize.index;
            break;
        }
    }

    // TODO: Improve the configuration as the VIDIOC_S_PARM is
    // not supported by current i.MX 8 ISI - MAX9286 - OV10635 pipeline.
    struct v4l2_streamparm param;
    memset(&param, 0, sizeof(param));
    param.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    param.parm.capture.timeperframe.numerator   = 1;
    param.parm.capture.timeperframe.denominator = 30;
    param.parm.capture.capturemode = capturemode;
    ret = ioctl(mDeviceFd, VIDIOC_S_PARM, &param);
    if (ret < 0) {
        LOG(WARNING) << "VIDIOC_S_PARM Failed " << errno;
    }

    {
        std::lock_guard<std::mutex> lock(mPipelineLock);
        if (sGroupFmt == 0) {
            if (configure(true /* onlyIsiConfig */) < 0)
                return -EINVAL;
        }
        sGroupFmt++;
        mColourPipeline = true;
    }

    // Set our desired output format
    v4l2_format format;
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    format.fmt.pix.pixelformat = pixel_format;
    format.fmt.pix.width = width;
    format.fmt.pix.height = height;
    // TODO:  Do we need to specify this?
    format.fmt.pix_mp.field = V4L2_FIELD_ALTERNATE;
    format.fmt.pix_mp.num_planes = 1;
    LOG(INFO) << "Requesting format: " << ((char*)&format.fmt.pix.pixelformat)[0]
              << ((char*)&format.fmt.pix.pixelformat)[1] << ((char*)&format.fmt.pix.pixelformat)[2]
              << ((char*)&format.fmt.pix.pixelformat)[3] << "(" << std::hex << std::setw(8)
              << format.fmt.pix.pixelformat << ")";

    if (ioctl(mDeviceFd, VIDIOC_S_FMT, &format) < 0) {
        PLOG(ERROR) << "VIDIOC_S_FMT failed";
        return -EINVAL;
    }

    // Report the current output format
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (ioctl(mDeviceFd, VIDIOC_G_FMT, &format) == 0) {
        mFormat = format.fmt.pix.pixelformat;
        mWidth = format.fmt.pix.width;
        mHeight = format.fmt.pix.height;
        mStride = format.fmt.pix.bytesperline;

        LOG(INFO) << "Current output format:  "
                  << "fmt=0x" << std::hex << format.fmt.pix.pixelformat << ", " << std::dec
                  << format.fmt.pix.width << " x " << format.fmt.pix.height
                  << ", pitch=" << format.fmt.pix.bytesperline;
    } else {
        PLOG(ERROR) << "VIDIOC_G_FMT failed";
        return false;
    }
    // Tell the L4V2 driver to prepare our streaming buffers
    v4l2_requestbuffers bufrequest;
    memset(&bufrequest, 0, sizeof(bufrequest));
    bufrequest.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    bufrequest.memory = V4L2_MEMORY_DMABUF;
    bufrequest.count = MAX_V4L2_BUFFER_NUM;
    if (ioctl(mDeviceFd, VIDIOC_REQBUFS, &bufrequest) < 0) {
        PLOG(ERROR) << "VIDIOC_REQBUFS failed";
        return false;
    }

    // Make sure we're initialized to the STOPPED state
    mRunMode = STOPPED;

    // Ready to go!
    return true;
}

void VideoCapture::close() {
    LOG(DEBUG) << __FUNCTION__;
    // Stream should be stopped first!
    assert(mRunMode == STOPPED);

    if (isOpen()) {
        LOG(DEBUG) << "closing video device file handle " << mDeviceFd;
        ::close(mDeviceFd);
        mDeviceFd = -1;
    }
    sGroupFmt -= mColourPipeline;
    mColourPipeline = false;
}

bool VideoCapture::startStream(std::function<void(VideoCapture*, imageBuffer&, void*)> callback) {
    // Set the state of our background thread
    int prevRunMode = mRunMode.fetch_or(RUN);
    if (prevRunMode & RUN) {
        // The background thread is already running, so we can't start a new stream
        LOG(ERROR) << "Already in RUN state, so we can't start a new streaming thread";
        return false;
    }

    // Start the video stream
    const int type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (ioctl(mDeviceFd, VIDIOC_STREAMON, &type) < 0) {
        PLOG(ERROR) << "VIDIOC_STREAMON failed";
        return false;
    }

    // Remember who to tell about new frames as they arrive
    mCallback = callback;

    // Fire up a thread to receive and dispatch the video frames
    mCaptureThread = std::thread([this]() { collectFrames(); });

    LOG(DEBUG) << "Stream started.";
    return true;
}

void VideoCapture::stopStream() {
    // Tell the background thread to stop
    int prevRunMode = mRunMode.fetch_or(STOPPING);
    if (prevRunMode == STOPPED) {
        // The background thread wasn't running, so set the flag back to STOPPED
        mRunMode = STOPPED;
    } else if (prevRunMode & STOPPING) {
        LOG(ERROR) << "stopStream called while stream is already stopping.  "
                   << "Reentrancy is not supported!";
        return;
    } else {
        // Block until the background thread is stopped

        // Stop the underlying video stream (automatically empties the buffer queue)
        const int type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        if (ioctl(mDeviceFd, VIDIOC_STREAMOFF, &type) < 0) {
            PLOG(ERROR) << "VIDIOC_STREAMOFF failed";
        }

        if (mCaptureThread.joinable()) {
            mCaptureThread.join();
        }
    }

    // Tell the L4V2 driver to release our streaming buffers
    v4l2_requestbuffers bufrequest;

    bufrequest.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    bufrequest.memory = V4L2_MEMORY_DMABUF;
    bufrequest.count = 0;
    ioctl(mDeviceFd, VIDIOC_REQBUFS, &bufrequest);

    // Drop our reference to the frame delivery callback interface
    mCallback = nullptr;
}

bool VideoCapture::queueFB(int index, int fd, int size) {
    struct v4l2_buffer buf;
    struct v4l2_plane planes;
    memset(&buf, 0, sizeof(buf));
    memset(&planes, 0, sizeof(struct v4l2_plane));

    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buf.memory = V4L2_MEMORY_DMABUF;
    buf.m.planes = &planes;
    buf.index = index;
    buf.length = 1;
    buf.m.planes->length = size;
    buf.m.planes->m.fd = fd;


    // Requeue the buffer to capture the next available frame
    if (ioctl(mDeviceFd, VIDIOC_QBUF, &buf) < 0) {
        PLOG(ERROR) << "VIDIOC_QBUF failed";
        return false;
    }

    return true;
}

// This runs on a background thread to receive and dispatch video frames
void VideoCapture::collectFrames() {
    // Run until our atomic signal is cleared
    while (mRunMode == RUN) {
        struct v4l2_buffer buf;
        struct v4l2_plane planes;

        memset(&buf, 0, sizeof(buf));
        memset(&planes, 0, sizeof(struct v4l2_plane));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_DMABUF;
        buf.m.planes = &planes;
        buf.length = 1;

        // Wait for a buffer to be ready
        if (ioctl(mDeviceFd, VIDIOC_DQBUF, &buf) < 0) {
          PLOG(ERROR) << "VIDIOC_DQBUF failed";
          break;
        }

        // If a callback was requested per frame, do that now
        if (mCallback) {
            mCallback(this, buf, NULL);
        }
    }

    // Mark ourselves stopped
    LOG(DEBUG) << "VideoCapture thread ending";
    mRunMode = STOPPED;
}

int VideoCapture::setParameter(v4l2_control& control) {
    int status = ioctl(mDeviceFd, VIDIOC_S_CTRL, &control);
    if (status < 0) {
        PLOG(ERROR) << "Failed to program a parameter value "
                    << "id = " << std::hex << control.id;
    }

    return status;
}

int VideoCapture::getParameter(v4l2_control& control) {
    int status = ioctl(mDeviceFd, VIDIOC_G_CTRL, &control);
    if (status < 0) {
        PLOG(ERROR) << "Failed to read a parameter value"
                    << " fd = " << std::hex << mDeviceFd << " id = " << control.id;
    }

    return status;
}

std::set<uint32_t> VideoCapture::enumerateCameraControls() {
    // Retrieve available camera controls
    struct v4l2_queryctrl ctrl = {.id = V4L2_CTRL_FLAG_NEXT_CTRL};

    std::set<uint32_t> ctrlIDs;
    while (0 == ioctl(mDeviceFd, VIDIOC_QUERYCTRL, &ctrl)) {
        if (!(ctrl.flags & V4L2_CTRL_FLAG_DISABLED)) {
            ctrlIDs.insert(ctrl.id);
        }

        ctrl.id |= V4L2_CTRL_FLAG_NEXT_CTRL;
    }

    if (errno != EINVAL) {
        PLOG(WARNING) << "Failed to run VIDIOC_QUERYCTRL";
    }

    return std::move(ctrlIDs);
}
