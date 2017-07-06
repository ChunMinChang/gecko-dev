/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AudioChild.h"
#include "mozilla/audio/AudioNotificationReceiver.h"
#include "mozilla/dom/ContentChild.h"

namespace mozilla {
namespace audio {

static AudioChild* sChild;

/* static */ AudioChild*
AudioChild::GetSingleton()
{
  if (!sChild) {
    dom::ContentChild* contentChild = dom::ContentChild::GetSingleton();
    sChild = static_cast<AudioChild*>(contentChild->SendPAudioConstructor());
  }
  return sChild;
}

mozilla::ipc::IPCResult
AudioChild::RecvDefaultDeviceChange()
{
  AudioNotificationReceiver::NotifyDefaultDeviceChanged();
  return IPC_OK();
}

} // namespace audio
} // namespace mozilla