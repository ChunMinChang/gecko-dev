/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_AUDIONOTIFICATIONSENDER_H_
#define MOZILLA_AUDIONOTIFICATIONSENDER_H_

#include "mozilla/audio/AudioParent.h"
#include "nsError.h" // for nsresult

namespace mozilla {
namespace audio {

// Please see the architecture figure in AudioNotificationReceiver.h.
class AudioNotificationSender
{
public:
  // Add the AudioParent into the subscribers list.
  static nsresult Register(AudioParent* aAudioParent);

  // Remove the AudioParent from the subscribers list.
  static void Unregister(AudioParent* aAudioParent);

private:
  // Send the device-changed notification from chrome process to content process
  // via PAudio protocol.
  static void NotifyDefaultDeviceChanged();
}; // AudioNotificationSender

} // namespace audio
} // namespace mozilla

#endif // MOZILLA_AUDIONOTIFICATIONSENDER_H_
