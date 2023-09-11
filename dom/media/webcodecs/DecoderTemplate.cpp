/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "DecoderTemplate.h"
#include "MediaInfo.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/Unused.h"
#include "mozilla/dom/DOMException.h"
#include "mozilla/dom/Event.h"
#include "mozilla/dom/VideoDecoderBinding.h"
#include "mozilla/dom/WorkerCommon.h"
#include "mozilla/dom/WorkerRef.h"
#include "mozilla/media/MediaUtils.h"
#include "nsGkAtoms.h"
#include "nsString.h"
#include "nsThreadUtils.h"

// mozilla::LazyLogModule gWebCodecsLog("WebCodecs");
extern mozilla::LazyLogModule gWebCodecsLog;

namespace mozilla::dom {

#ifdef LOG_INTERNAL
#  undef LOG_INTERNAL
#endif  // LOG_INTERNAL
#define LOG_INTERNAL(level, msg, ...) \
  MOZ_LOG(gWebCodecsLog, LogLevel::level, (msg, ##__VA_ARGS__))

#ifdef LOG
#  undef LOG
#endif  // LOG
#define LOG(msg, ...) LOG_INTERNAL(Debug, msg, ##__VA_ARGS__)

#ifdef LOGW
#  undef LOGW
#endif  // LOGW
#define LOGW(msg, ...) LOG_INTERNAL(Warning, msg, ##__VA_ARGS__)

#ifdef LOGE
#  undef LOGE
#endif  // LOGE
#define LOGE(msg, ...) LOG_INTERNAL(Error, msg, ##__VA_ARGS__)

#ifdef LOGV
#  undef LOGV
#endif  // LOGV
#define LOGV(msg, ...) LOG_INTERNAL(Verbose, msg, ##__VA_ARGS__)

static nsresult FireEvent(DOMEventTargetHelper* aEventTarget,
                          nsAtom* aTypeWithOn, const nsAString& aEventType) {
  MOZ_ASSERT(aEventTarget);

  if (aTypeWithOn && !aEventTarget->HasListenersFor(aTypeWithOn)) {
    LOGV("EventTarget %p has no %s event listener", aEventTarget,
         NS_ConvertUTF16toUTF8(aEventType).get());
    return NS_ERROR_ABORT;
  }

  LOGV("Dispatch %s event to EventTarget %p",
       NS_ConvertUTF16toUTF8(aEventType).get(), aEventTarget);
  RefPtr<Event> event = new Event(aEventTarget, nullptr, nullptr);
  event->InitEvent(aEventType, true, true);
  event->SetTrusted(true);
  aEventTarget->DispatchEvent(*event);
  return NS_OK;
}

template <typename DecoderType>
DecoderTemplate<DecoderType>::DecoderTemplate(
    nsIGlobalObject* aGlobalObject,
    RefPtr<WebCodecsErrorCallback>&& aErrorCallback,
    RefPtr<OutputCallbackType>&& aOutputCallback)
    : DOMEventTargetHelper(aGlobalObject),
      mErrorCallback(std::move(aErrorCallback)),
      mOutputCallback(std::move(aOutputCallback)),
      mState(CodecState::Unconfigured),
      mKeyChunkRequired(true),
      mMessageQueueBlocked(false),
      mDecodeQueueSize(0),
      mDequeueEventScheduled(false),
      mLatestConfigureId(0),  // ConfigureMessage::NoId
      mDecodeCounter(0),
      mFlushCounter(0) {}

template <typename DecoderType>
CodecState DecoderTemplate<DecoderType>::State() const {
  return mState;
}

template <typename DecoderType>
uint32_t DecoderTemplate<DecoderType>::DecodeQueueSize() const {
  return mDecodeQueueSize;
}

template <typename DecoderType>
Result<Ok, nsresult> DecoderTemplate<DecoderType>::Reset(const nsresult& aResult) {
  AssertIsOnOwningThread();

  if (mState == CodecState::Closed) {
    return Err(NS_ERROR_DOM_INVALID_STATE_ERR);
  }

  mState = CodecState::Unconfigured;
  mDecodeCounter = 0;
  mFlushCounter = 0;

  CancelPendingControlMessages(aResult);
  DestroyDecoderAgentIfAny();

  if (mDecodeQueueSize > 0) {
    mDecodeQueueSize = 0;
    ScheduleDequeueEvent();
  }

  LOG("VideoDecoder %p now has its message queue unblocked", this);
  mMessageQueueBlocked = false;

  return Ok();
}

template <typename DecoderType>
Result<Ok, nsresult> DecoderTemplate<DecoderType>::Close(const nsresult& aResult) {
  AssertIsOnOwningThread();

  MOZ_TRY(Reset(aResult));
  mState = CodecState::Closed;
  if (aResult != NS_ERROR_DOM_ABORT_ERR) {
    LOGE("VideoDecoder %p Close on error: 0x%08" PRIx32, this,
         static_cast<uint32_t>(aResult));
    ScheduleReportError(aResult);
  }
  return Ok();
}

template <typename DecoderType>
void DecoderTemplate<DecoderType>::ReportError(const nsresult& aResult) {
  AssertIsOnOwningThread();

  RefPtr<DOMException> e = DOMException::Create(aResult);
  RefPtr<WebCodecsErrorCallback> cb(mErrorCallback);
  cb->Call(*e);
}

template <typename DecoderType>
class DecoderTemplate<DecoderType>::ErrorRunnable final : public DiscardableRunnable {
 public:
  ErrorRunnable(DecoderTemplate<DecoderType>* aVideoDecoder, const nsresult& aError)
      : DiscardableRunnable("VideoDecoder ErrorRunnable"),
        mVideoDecoder(aVideoDecoder),
        mError(aError) {
    MOZ_ASSERT(mVideoDecoder);
  }
  ~ErrorRunnable() = default;

  // MOZ_CAN_RUN_SCRIPT_BOUNDARY until Runnable::Run is MOZ_CAN_RUN_SCRIPT.
  // See bug 1535398.
  MOZ_CAN_RUN_SCRIPT_BOUNDARY NS_IMETHOD Run() override {
    LOGE("VideoDecoder %p report error: 0x%08" PRIx32, mVideoDecoder.get(),
         static_cast<uint32_t>(mError));
    RefPtr<DecoderTemplate<DecoderType>> d = std::move(mVideoDecoder);
    d->ReportError(mError);
    return NS_OK;
  }

 private:
  RefPtr<DecoderTemplate<DecoderType>> mVideoDecoder;
  const nsresult mError;
};

template <typename DecoderType>
void DecoderTemplate<DecoderType>::ScheduleReportError(const nsresult& aResult) {
  LOGE("VideoDecoder %p, schedule to report error: 0x%08" PRIx32, this,
       static_cast<uint32_t>(aResult));
  MOZ_ALWAYS_SUCCEEDS(
      NS_DispatchToCurrentThread(MakeAndAddRef<ErrorRunnable>(this, aResult)));
}


template <typename DecoderType>
void DecoderTemplate<DecoderType>::ScheduleDequeueEvent() {
  AssertIsOnOwningThread();

  if (mDequeueEventScheduled) {
    return;
  }
  mDequeueEventScheduled = true;

  auto dispatcher = [self = RefPtr{this}] {
    FireEvent(self.get(), nsGkAtoms::ondequeue, u"dequeue"_ns);
    self->mDequeueEventScheduled = false;
  };
  nsISerialEventTarget* target = GetCurrentSerialEventTarget();

  if (NS_IsMainThread()) {
    MOZ_ALWAYS_SUCCEEDS(target->Dispatch(NS_NewRunnableFunction(
        "ScheduleDequeueEvent Runnable (main)", dispatcher)));
    return;
  }

  MOZ_ALWAYS_SUCCEEDS(target->Dispatch(NS_NewCancelableRunnableFunction(
      "ScheduleDequeueEvent Runnable (worker)", dispatcher)));
}

// CreateDecoderAgent will create an DecoderAgent paired with a xpcom-shutdown
// blocker and a worker-reference. Besides the needs mentioned in the header
// file, the blocker and the worker-reference also provides an entry point for
// us to clean up the resources. Other than ~VideoDecoder, Reset(), or
// Close(), the resources should be cleaned up in the following situations:
// 1. VideoDecoder on window, closing document
// 2. VideoDecoder on worker, closing document
// 3. VideoDecoder on worker, terminating worker
//
// In case 1, the entry point to clean up is in the mShutdownBlocker's
// ShutdownpPomise-resolver. In case 2, the entry point is in mWorkerRef's
// shutting down callback. In case 3, the entry point is in mWorkerRef's
// shutting down callback.

template <typename DecoderType>
bool DecoderTemplate<DecoderType>::CreateDecoderAgent(
    DecoderAgent::Id aId, UniquePtr<ConfigInternal>&& aConfig,
    UniquePtr<TrackInfo>&& aInfo) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mState == CodecState::Configured);
  MOZ_ASSERT(!mAgent);
  MOZ_ASSERT(!mActiveConfig);
  MOZ_ASSERT(!mShutdownBlocker);
  MOZ_ASSERT_IF(!NS_IsMainThread(), !mWorkerRef);

  auto resetOnFailure = MakeScopeExit([&]() {
    mAgent = nullptr;
    mActiveConfig = nullptr;
    mShutdownBlocker = nullptr;
    mWorkerRef = nullptr;
  });

  // If VideoDecoder is on worker, get a worker reference.
  if (!NS_IsMainThread()) {
    WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();
    if (NS_WARN_IF(!workerPrivate)) {
      return false;
    }

    // Clean up all the resources when worker is going away.
    RefPtr<StrongWorkerRef> workerRef = StrongWorkerRef::Create(
        workerPrivate, "VideoDecoder::SetupDecoder", [self = RefPtr{this}]() {
          LOG("VideoDecoder %p, worker is going away", self.get());
          Unused << self->Reset(NS_ERROR_DOM_ABORT_ERR);
        });
    if (NS_WARN_IF(!workerRef)) {
      return false;
    }

    mWorkerRef = new ThreadSafeWorkerRef(workerRef);
  }

  mAgent = MakeRefPtr<DecoderAgent>(aId, std::move(aInfo));
  mActiveConfig = std::move(aConfig);

  // ShutdownBlockingTicket requires an unique name to register its own
  // nsIAsyncShutdownBlocker since each blocker needs a distinct name.
  // To do that, we use DecodeAgent's unique id to create a unique name.
  nsAutoString uniqueName;
  uniqueName.AppendPrintf(
      "Blocker for DecodeAgent #%d (codec: %s) @ %p", mAgent->mId,
      NS_ConvertUTF16toUTF8(mActiveConfig->mCodec).get(), mAgent.get());

  mShutdownBlocker = media::ShutdownBlockingTicket::Create(
      uniqueName, NS_LITERAL_STRING_FROM_CSTRING(__FILE__), __LINE__);
  if (!mShutdownBlocker) {
    LOGE("VideoDecoder %p failed to create %s", this,
         NS_ConvertUTF16toUTF8(uniqueName).get());
    return false;
  }

  // Clean up all the resources when xpcom-will-shutdown arrives since the page
  // is going to be closed.
  mShutdownBlocker->ShutdownPromise()->Then(
      GetCurrentSerialEventTarget(), __func__,
      [self = RefPtr{this}, id = mAgent->mId,
       ref = mWorkerRef](bool /* aUnUsed*/) {
        LOG("VideoDecoder %p gets xpcom-will-shutdown notification for "
            "DecodeAgent #%d",
            self.get(), id);
        Unused << self->Reset(NS_ERROR_DOM_ABORT_ERR);
      },
      [self = RefPtr{this}, id = mAgent->mId,
       ref = mWorkerRef](bool /* aUnUsed*/) {
        LOG("VideoDecoder %p removes shutdown-blocker #%d before getting any "
            "notification. DecodeAgent #%d should have been dropped",
            self.get(), id, id);
        MOZ_ASSERT(!self->mAgent || self->mAgent->mId != id);
      });

  LOG("VideoDecoder %p creates DecodeAgent #%d @ %p and its shutdown-blocker",
      this, mAgent->mId, mAgent.get());

  resetOnFailure.release();
  return true;
}

template <typename DecoderType>
void DecoderTemplate<DecoderType>::DestroyDecoderAgentIfAny() {
  AssertIsOnOwningThread();

  if (!mAgent) {
    LOG("VideoDecoder %p has no DecodeAgent to destroy", this);
    return;
  }

  MOZ_ASSERT(mActiveConfig);
  MOZ_ASSERT(mShutdownBlocker);
  MOZ_ASSERT_IF(!NS_IsMainThread(), mWorkerRef);

  LOG("VideoDecoder %p destroys DecodeAgent #%d @ %p", this, mAgent->mId,
      mAgent.get());
  mActiveConfig = nullptr;
  RefPtr<DecoderAgent> agent = std::move(mAgent);
  // mShutdownBlocker should be kept alive until the shutdown is done.
  // mWorkerRef is used to ensure this task won't be discarded in worker.
  agent->Shutdown()->Then(
      GetCurrentSerialEventTarget(), __func__,
      [self = RefPtr{this}, id = agent->mId, ref = std::move(mWorkerRef),
       blocker = std::move(mShutdownBlocker)](
          const ShutdownPromise::ResolveOrRejectValue& aResult) {
        LOG("VideoDecoder %p, DecoderAgent #%d's shutdown has been %s. "
            "Drop its shutdown-blocker now",
            self.get(), id, aResult.IsResolve() ? "resolved" : "rejected");
      });
}


#undef LOG
#undef LOGW
#undef LOGE
#undef LOGV
#undef LOG_INTERNAL

}  // namespace mozilla::dom


