/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_AudioParent_h
#define mozilla_AudioParent_h

#include "mozilla/audio/PAudioParent.h"

namespace mozilla {
namespace audio {

class AudioParent : public PAudioParent
{
public:
  explicit AudioParent();

  virtual ~AudioParent();

  bool SendDefaultDeviceChange();

private:
  virtual void ActorDestroy(ActorDestroyReason aWhy) override;
};

} // namespace audio
} // namespace mozilla

#endif  // mozilla_AudioParent_h
