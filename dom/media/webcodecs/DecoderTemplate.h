/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_DecoderTemplate_h
#define mozilla_dom_DecoderTemplate_h

#include "mozilla/DOMEventTargetHelper.h"

namespace mozilla::dom {

class WebCodecsErrorCallback;
enum class CodecState : uint8_t;

template <typename DecoderType>
class DecoderTemplate : public DOMEventTargetHelper {
 public:
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

  void ScheduleDequeueEvent();

  // Constant in practice, only set in ctor.
  RefPtr<WebCodecsErrorCallback> mErrorCallback;
  RefPtr<OutputCallbackType> mOutputCallback;

  CodecState mState;
  bool mKeyChunkRequired;

  bool mMessageQueueBlocked;

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
};

}  // namespace mozilla::dom

#endif  // mozilla_dom_DecoderTemplate_h
