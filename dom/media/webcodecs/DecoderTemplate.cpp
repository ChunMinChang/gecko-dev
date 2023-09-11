/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "DecoderTemplate.h"
#include "mozilla/dom/VideoDecoderBinding.h"

namespace mozilla::dom {

template <typename Traits>
DecoderTemplate<Traits>::DecoderTemplate(
    nsIGlobalObject* aGlobalObject,
    RefPtr<WebCodecsErrorCallback>&& aErrorCallback,
    RefPtr<OutputCallbackType>&& aOutputCallback)
    : DOMEventTargetHelper(aGlobalObject),
      mErrorCallback(std::move(aErrorCallback)),
      mOutputCallback(std::move(aOutputCallback)),
      mState(CodecState::Unconfigured),
      mDecodeQueueSize(0) {}

template <typename Traits>
CodecState DecoderTemplate<Traits>::State() const {
  return mState;
}

template <typename Traits>
uint32_t DecoderTemplate<Traits>::DecodeQueueSize() const {
  return mDecodeQueueSize;
}

}  // namespace mozilla::dom


