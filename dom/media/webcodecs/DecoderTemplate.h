/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_DecoderTemplate_h
#define mozilla_dom_DecoderTemplate_h

#include <queue>

#include "mozilla/DOMEventTargetHelper.h"
#include "mozilla/DecoderAgent.h"
#include "mozilla/RefPtr.h"
#include "mozilla/Result.h"
#include "mozilla/UniquePtr.h"

namespace mozilla {

class TrackInfo;
namespace dom {

class WebCodecsErrorCallback;
enum class CodecState : uint8_t;

class ConfigureMessage;
class DecodeMessage;
class FlushMessage;

class ControlMessage {
 public:
  explicit ControlMessage(const nsACString& aTitle);
  virtual ~ControlMessage() = default;
  virtual void Cancel() = 0;
  virtual bool IsProcessing() = 0;

  virtual const nsCString& ToString() const { return mTitle; }
  virtual ConfigureMessage* AsConfigureMessage() { return nullptr; }
  virtual DecodeMessage* AsDecodeMessage() { return nullptr; }
  virtual FlushMessage* AsFlushMessage() { return nullptr; }

  const nsCString mTitle;  // Used to identify the message in the logs.
};

template <typename DecoderType>
class DecoderTemplate : public DOMEventTargetHelper {
 public:
  typedef typename DecoderType::ConfigInternal ConfigInternal;
  typedef typename DecoderType::OutputCallbackType OutputCallbackType;

  // protected ctor?
  DecoderTemplate(nsIGlobalObject* aGlobalObject,
                  RefPtr<WebCodecsErrorCallback>&& aErrorCallback,
                  RefPtr<OutputCallbackType>&& aOutputCallback);

  ~DecoderTemplate() = default;

  virtual CodecState State() const;

  virtual uint32_t DecodeQueueSize() const;

 protected:
  // DecoderTemplate can run on either main thread or worker thread.
  void AssertIsOnOwningThread() const {
    NS_ASSERT_OWNINGTHREAD(DecoderTemplate);
  }

  Result<Ok, nsresult> Reset(const nsresult& aResult);
  Result<Ok, nsresult> Close(const nsresult& aResult);

  MOZ_CAN_RUN_SCRIPT void ReportError(const nsresult& aResult);

  class ErrorRunnable;
  void ScheduleReportError(const nsresult& aResult);

  void ScheduleDequeueEvent();

  // Returns true when mAgent can be created.
  bool CreateDecoderAgent(DecoderAgent::Id aId,
                          UniquePtr<ConfigInternal>&& aConfig,
                          UniquePtr<TrackInfo>&& aInfo);
  void DestroyDecoderAgentIfAny();

  // Constant in practice, only set in ctor.
  RefPtr<WebCodecsErrorCallback> mErrorCallback;
  RefPtr<OutputCallbackType> mOutputCallback;

  CodecState mState;
  bool mKeyChunkRequired;

  bool mMessageQueueBlocked;
  std::queue<UniquePtr<ControlMessage>> mControlMessageQueue;
  UniquePtr<ControlMessage> mProcessingMessage;

  uint32_t mDecodeQueueSize;
  bool mDequeueEventScheduled;

  // A unique id tracking the ConfigureMessage and will be used as the
  // DecoderAgent's Id.
  uint32_t mLatestConfigureId;
  // Tracking how many decode data has been enqueued and this number will be
  // used as the DecodeMessage's Id.
  size_t mDecodeCounter;
  // Tracking how many flush request has been enqueued and this number will be
  // used as the FlushMessage's Id.
  size_t mFlushCounter;

  // DecoderAgent will be created every time "configure" is being processed, and
  // will be destroyed when "reset" or another "configure" is called (spec
  // allows calling two "configure" without a "reset" in between).
  RefPtr<DecoderAgent> mAgent;
  UniquePtr<ConfigInternal> mActiveConfig;

  // Used to add a nsIAsyncShutdownBlocker on main thread to block
  // xpcom-shutdown before the underlying MediaDataDecoder is created. The
  // blocker will be held until the underlying MediaDataDecoder has been shut
  // down. This blocker guarantees RemoteDecoderManagerChild's thread, where the
  // underlying RemoteMediaDataDecoder is on, outlives the
  // RemoteMediaDataDecoder, since the thread releasing, which happens on main
  // thread when getting a xpcom-shutdown signal, is blocked by the added
  // blocker. As a result, RemoteMediaDataDecoder can safely work on worker
  // thread with a holding blocker (otherwise, if RemoteDecoderManagerChild
  // releases its thread on main thread before RemoteMediaDataDecoder's
  // Shutdown() task run on worker thread, RemoteMediaDataDecoder has no thread
  // to run).
  UniquePtr<media::ShutdownBlockingTicket> mShutdownBlocker;

  // Held to make sure the dispatched tasks can be done before worker is going
  // away. As long as this worker-ref is held somewhere, the tasks dispatched to
  // the worker can be executed (otherwise the tasks would be canceled). This
  // ref should be activated as long as the underlying MediaDataDecoder is
  // alive, and should keep alive until mShutdownBlocker is dropped, so all
  // MediaDataDecoder's tasks and mShutdownBlocker-releasing task can be
  // executed.
  // TODO: Use StrongWorkerRef instead if this is always used in the same
  // thread?
  RefPtr<ThreadSafeWorkerRef> mWorkerRef;
};

}  // namespace dom
}  // namespace mozilla

#endif  // mozilla_dom_DecoderTemplate_h
