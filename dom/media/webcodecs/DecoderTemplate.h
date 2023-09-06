/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_DecoderTemplate_h
#define mozilla_dom_DecoderTemplate_h

namespace mozilla::dom {

class WebCodecsErrorCallback;

template <typename Traits>
class DecoderTemplate {
 public: // protected?
  typedef typename Traits::OutputCallbackType OutputCallbackType;

  DecoderTemplate(RefPtr<WebCodecsErrorCallback>&& aErrorCallback,
                  RefPtr<OutputCallbackType>&& aOutputCallback);

  ~DecoderTemplate() = default;

 protected:
  // Constant in practice, only set in ctor.
  RefPtr<WebCodecsErrorCallback> mErrorCallback;
  RefPtr<OutputCallbackType> mOutputCallback;
};

}  // namespace mozilla::dom

#endif  // mozilla_dom_DecoderTemplate_h
