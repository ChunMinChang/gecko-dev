/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AudioNotificationReceiver.h"
#include "mozilla/audio/AudioChild.h" // for AudioChild
#include "mozilla/Logging.h"          // for LazyLogModule
#include "mozilla/StaticMutex.h"      // for StaticMutex
#include "mozilla/StaticPtr.h"        // for StaticAutoPtr
#include "nsAppRunner.h"              // for XRE_IsContentProcess
#include "nsHashKeys.h"               // for nsPtrHashKey
#include "nsTHashtable.h"             // for nsTHashtable

static mozilla::LazyLogModule sLogger("AudioNotificationReceiver");

#undef ANR_LOG
#define ANR_LOG(...) MOZ_LOG(sLogger, mozilla::LogLevel::Debug, (__VA_ARGS__))
#undef ANR_LOGW
#define ANR_LOGW(...) MOZ_LOG(sLogger, mozilla::LogLevel::Warning, (__VA_ARGS__))

namespace mozilla {
namespace audio {

// An IPC child to receive the device-changed notification.
static AudioChild* sChild = nullptr;


/*
 * A runnable task to create an IPC channel(IPC need to be done in worker thread).
 */
class AudioIPCRunnable : public Runnable
{
public:
  explicit AudioIPCRunnable(): Runnable("AudioIPCRunnable")
  {}

  NS_IMETHOD Run() override
  {
    ANR_LOG("Establishing an IPC channel to receive device-changed notification.");
    sChild = AudioChild::GetSingleton();
    if (!sChild) {
      ANR_LOGW("No IPC channel is built.");
    }
    return NS_OK;
  }
}; // AudioIPCRunnable


/*
 * A list containing all clients subscribering the device-changed notifications.
 */
typedef nsTHashtable<nsPtrHashKey<AudioStream>> Subscribers;
static StaticAutoPtr<Subscribers> sSubscribers;
static StaticMutex sMutex;


/*
 * AudioNotificationReceiver Implementation
 */
/* static */ void
AudioNotificationReceiver::Register(AudioStream* aAudioStream)
{
  MOZ_ASSERT(XRE_IsContentProcess());

  // Establish an IPC channel to receive to device-changed notification
  // if it's not been built yet.
  if (!sChild) {
    RefPtr<AudioIPCRunnable> runnable = new AudioIPCRunnable();
    NS_DispatchToMainThread(runnable);
  }

  if (!sSubscribers) {
    sSubscribers = new Subscribers();
  }

  StaticMutexAutoLock lock(sMutex);
  sSubscribers->PutEntry(aAudioStream);

  ANR_LOG("The AudioStream: %p is registered successfully.", aAudioStream);
}

/* static */ void
AudioNotificationReceiver::Unregister(AudioStream* aAudioStream)
{
  MOZ_ASSERT(XRE_IsContentProcess());
  MOZ_ASSERT(sSubscribers, "No subscribers registered");
  MOZ_ASSERT(sSubscribers->Contains(aAudioStream), "the audio stream is not registered");

  StaticMutexAutoLock lock(sMutex);
  sSubscribers->RemoveEntry(aAudioStream);

  ANR_LOG("The AudioStream: %p is unregistered successfully.", aAudioStream);
}

/* static */ void
AudioNotificationReceiver::NotifyDefaultDeviceChanged()
{
  MOZ_ASSERT(XRE_IsContentProcess());

  for (auto iter = sSubscribers->ConstIter(); !iter.Done(); iter.Next()) {
    AudioStream* subscriber = iter.Get()->GetKey();
    ANR_LOG("Notify the AudioStream: %p that the default device has been changed.", subscriber);
    subscriber->ResetDefaultDevice();
  }
}

} // namespace audio
} // namespace mozilla