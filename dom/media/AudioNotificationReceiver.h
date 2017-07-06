/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_AUDIONOTIFICATIONRECEIVER_H_
#define MOZILLA_AUDIONOTIFICATIONRECEIVER_H_

/*
 * Architecture to send/receive default device-changed notification:
 *
 *  Chrome Process                       ContentProcess 1
 *  ------------------                   ------------------
 *
 *       AudioNotification                    AudioStream 1    AudioStream N
 *           |      ^                             |      ^         ^
 *        (6)|      |(5)                          |(1)   |(10)     .
 *           v      |                             v      |         v
 *   AudioNotificationSender                  AudioNotificationReceiver
 *     ^     |      ^                             |      ^
 *     .  (7)|      |(4)                          |(2)   |(9)
 *     .     v      |                             v      |
 *     .  (P)AudioParent 1                     (P)AudioChild 1
 *     .     |      ^                             |      ^
 *     .  (8)|      |                             |(3)   |
 *     .     |      +-----------------------------+      |
 *     .     |                                           |
 *     .     +-------------------------------------------+
 *     .
 *     .
 *     .                                 Content Process M
 *     .                                 ------------------
 *     .                                          .
 *     v                                          .
 *   (P)AudioParent M  < . . . . . . . . . . > (P)AudioChild M
 *                          PAudio IPC
 *
 * Steps
 * --------
 *  1) Register the audiostream to AudioNotificationReceiver.
 *  2) If there is no active Audio IPC child, then we create one.
 *     Otherwise, skip (2) and (3).
 *  3) Establish an Audio IPC channel if it has not been built yet.
 *  4) Register the AudioParent to AudioNotificationSender
 *  5) If there is no active AudioNotification listening the device-changed
 *     signals from the system, then we create one.
 *  6) Get device-changed notification from the system
 *  7) Call NotifyDefaultDeviceChanged to pass the notification via PAudio
 *  8) Call SendDefaultDeviceChange to notify (P)AudioChild
 *  9) Get default device-changed signal from (P)AudioParent
 * 10) Call NotifyDefaultDeviceChanged to reset the streams to the
 *      new default device.
 *
 * Notes
 * --------
 * a) There is only one AudioNotificationSender and AudioNotification
 *    in a chrome process.
 * b) There is only one AudioNotificationReceiver and might be many
 *    AudioStreams in a content process.
 * c) There might be many AudioParent in a chrome process.
 * d) There is only one AudioChild in a content process.
 * e) All the Audiostreams are registered in the AudioNotificationReceiver.
 * f) All the AudioParents are registered in the AudioNotificationSender.
 */

#include "AudioStream.h"

namespace mozilla {
namespace audio {

class AudioNotificationReceiver
{
public:
  // Add the AudioStream into the subscribers list.
  static void Register(AudioStream* aAudioStream);

  // Remove the AudioStream from the subscribers list.
  static void Unregister(AudioStream* aAudioStream);

  // Notify all the streams that the default device has been changed.
  static void NotifyDefaultDeviceChanged();
}; // AudioNotificationReceiver

} // namespace audio
} // namespace mozilla

#endif // MOZILLA_AUDIONOTIFICATIONRECEIVER_H_