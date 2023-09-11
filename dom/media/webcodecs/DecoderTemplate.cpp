/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "DecoderTemplate.h"
#include "mozilla/dom/VideoDecoderBinding.h"
#include "mozilla/dom/Event.h"
#include "nsGkAtoms.h"
#include "nsString.h"

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

template <typename Traits>
DecoderTemplate<Traits>::DecoderTemplate(
    nsIGlobalObject* aGlobalObject,
    RefPtr<WebCodecsErrorCallback>&& aErrorCallback,
    RefPtr<OutputCallbackType>&& aOutputCallback)
    : DOMEventTargetHelper(aGlobalObject),
      mErrorCallback(std::move(aErrorCallback)),
      mOutputCallback(std::move(aOutputCallback)),
      mState(CodecState::Unconfigured),
      mKeyChunkRequired(true),
      mDecodeQueueSize(0),
      mDequeueEventScheduled(false) {}

template <typename Traits>
CodecState DecoderTemplate<Traits>::State() const {
  return mState;
}

template <typename Traits>
uint32_t DecoderTemplate<Traits>::DecodeQueueSize() const {
  return mDecodeQueueSize;
}

template <typename Traits>
void DecoderTemplate<Traits>::ScheduleDequeueEvent() {
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

#undef LOG
#undef LOGW
#undef LOGE
#undef LOGV
#undef LOG_INTERNAL

}  // namespace mozilla::dom


