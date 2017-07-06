/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AudioParent.h"
#include "mozilla/audio/AudioNotificationSender.h"

namespace mozilla {
namespace audio {

AudioParent::AudioParent()
{
  MOZ_COUNT_CTOR(AudioParent);
  nsresult r = AudioNotificationSender::Register(this);
}

AudioParent::~AudioParent()
{
  AudioNotificationSender::Unregister(this);
  MOZ_COUNT_DTOR(AudioParent);
}

bool
AudioParent::SendDefaultDeviceChange()
{
  return PAudioParent::SendDefaultDeviceChange();
}

void
AudioParent::ActorDestroy(ActorDestroyReason aWhy)
{
}

} // namespace audio
} // namespace mozilla
