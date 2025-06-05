/*
 * Copyright (C) 2023 The Android Open Source Project
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

#ifndef IMX_CHRE_CONNECTION_H_
#define IMX_CHRE_CONNECTION_H_

#include "chre_connection.h"
#include "chre_connection_callback.h"
#include "chre_host/fragmented_load_transaction.h"
#include "chre_host/host_protocol_host.h"
#include "chre_host/log.h"
#include "chre_host/log_message_parser.h"

#include <unistd.h>
#include <cassert>
#include <future>
#include <queue>
#include <thread>

namespace aidl::android::hardware::contexthub {

using namespace ::android::hardware::contexthub::common::implementation;
using ::android::chre::HostProtocolHost;

/** A class handling message transmission between context hub HAL and CHRE. */
// TODO(b/267188769): We should add comments explaining how IPI works.
class ImxChreConnection : public ChreConnection {
 public:
  ImxChreConnection(ChreConnectionCallback *callback)
      : mCallback(callback) {
    mPayload = std::make_unique<uint8_t[]>(kMaxReceivingPayloadBytes);
  };

  ~ImxChreConnection() override {
    // TODO(b/264308286): Need a decent way to terminate the listener thread.
    close(mChreFileDescriptor);
    if (mMessageListener.joinable()) {
      mMessageListener.join();
    }
    if (mMessageSender.joinable()) {
      mMessageSender.join();
    }
    if (mStateListener.joinable()) {
      mStateListener.join();
    }
  }

  static void handleMessageFromChre(ImxChreConnection *chreConnection,
                                    const unsigned char *messageBuffer,
                                    size_t messageLen);

  bool init() override;

  bool sendMessage(void *data, size_t length) override;

  void waitChreBackOnline(std::chrono::milliseconds timeoutMs) {
    flatbuffers::FlatBufferBuilder builder(48);
    HostProtocolHost::encodePulseRequest(builder);

    std::unique_lock<std::mutex> lock(mChrePulseMutex);
    // reset mIsChreRecovered before sending a PulseRequest message
    mIsChreBackOnline = false;
    sendMessage(builder.GetBufferPointer(), builder.GetSize());
    mChrePulseCondition.wait_for(
        lock, timeoutMs,
        [&isChreBackOnline = mIsChreBackOnline] { return isChreBackOnline; });
  }

  void notifyChreBackOnline() {
    {
      std::unique_lock<std::mutex> lock(mChrePulseMutex);
      mIsChreBackOnline = true;
    }
    mChrePulseCondition.notify_all();
  }

  inline ChreConnectionCallback *getCallback() {
    return mCallback;
  }

 private:
  // The wakelock used to keep device awake while handleUsfMsgAsync() is being
  // called.
  static constexpr char kWakeLock[] = "imx_chre_hal_wakelock";

  // Max payload size that can be sent to CHRE
  static constexpr uint32_t kMaxSendingPayloadBytes = 0x8000;  // 32K

  // Max payload size that can be received from CHRE
  static constexpr uint32_t kMaxReceivingPayloadBytes = 0x8000;  // 32K

  // Max overhead of the nanoapp binary payload caused by the fbs encapsulation
  static constexpr uint32_t kMaxPayloadOverheadBytes = 1024;

  // The path to CHRE file descriptor
  static constexpr char kChreFileDescriptorPath[] = "/dev/rpmsg_chre";

  // Max queue size for sending messages to CHRE
  static constexpr size_t kMaxSynchronousMessageQueueSize = 64;

  // Wrapper for a message sent to CHRE
  struct ChreConnectionMessage {
    uint32_t payloadSize = 0;
    uint8_t payload[kMaxSendingPayloadBytes];

    ChreConnectionMessage(void *data, size_t length) {
      assert(length <= kMaxSendingPayloadBytes);
      memcpy(payload, data, length);
      payloadSize = static_cast<uint32_t>(length);
    }
  };

  // A queue suitable for multiple producers and a single consumer.
  class SynchronousMessageQueue {
   public:
    bool emplace(void *data, size_t length) {
      std::unique_lock<std::mutex> lock(mMutex);
      if (mQueue.size() >= kMaxSynchronousMessageQueueSize) {
        LOGE("Message queue from HAL to CHRE is full!");
        return false;
      }
      mQueue.emplace(data, length);
      mCv.notify_all();
      return true;
    }

    void pop() {
      std::unique_lock<std::mutex> lock(mMutex);
      mQueue.pop();
    }

    ChreConnectionMessage &front() {
      std::unique_lock<std::mutex> lock(mMutex);
      return mQueue.front();
    }

    void waitForMessage() {
      std::unique_lock<std::mutex> lock(mMutex);
      mCv.wait(lock, [&]() { return !mQueue.empty(); });
    }

   private:
    std::mutex mMutex;
    std::condition_variable mCv;
    std::queue<ChreConnectionMessage> mQueue;
  };

  // The task receiving message from CHRE
  [[noreturn]] static void messageListenerTask(
      ImxChreConnection *chreConnection);

  // The task sending message to CHRE
  [[noreturn]] static void messageSenderTask(
      ImxChreConnection *chreConnection);

  // The task receiving CHRE state update
  [[noreturn]] static void chreStateMonitorTask(
      ImxChreConnection *chreConnection);

  [[nodiscard]] inline int getChreFileDescriptor() const {
    return mChreFileDescriptor;
  }

  // The file descriptor for communication with CHRE
  int mChreFileDescriptor;

  // The calback function that should be implemented by HAL
  ChreConnectionCallback *mCallback;

  // the message listener thread that receives messages from CHRE
  std::thread mMessageListener;
  // the message sender thread that sends messages to CHRE
  std::thread mMessageSender;
  // the status listener thread that hosts chreStateMonitorTask
  std::thread mStateListener;

  // Payload received from CHRE
  std::unique_ptr<uint8_t[]> mPayload;

  // For messages sent to CHRE
  SynchronousMessageQueue mQueue;

  // Mutex and CV are used to get PulseResponse from CHRE synchronously.
  std::mutex mChrePulseMutex;
  std::condition_variable mChrePulseCondition;
  bool mIsChreBackOnline =
      false;  // set to true after CHRE recovers from a restart
};
}  // namespace aidl::android::hardware::contexthub

#endif  // IMX_CHRE_CONNECTION_H_
