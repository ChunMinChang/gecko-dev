/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AudioNotificationSender.h"
#include "mozilla/Logging.h"      // for LazyLogModule
#include "mozilla/StaticMutex.h"  // for StaticMutex
#include "mozilla/StaticPtr.h"    // for StaticAutoPtr
#include "nsAppRunner.h"          // for XRE_IsParentProcess
#include "nsHashKeys.h"           // for nsPtrHashKey
#include "nsTHashtable.h"         // for nsTHashtable
#include <mmdeviceapi.h>          // for IMMNotificationClient interface

static mozilla::LazyLogModule sLogger("AudioNotificationSender");

#undef ANS_LOG
#define ANS_LOG(...) MOZ_LOG(sLogger, mozilla::LogLevel::Debug, (__VA_ARGS__))
#undef ANS_LOGW
#define ANS_LOGW(...) MOZ_LOG(sLogger, mozilla::LogLevel::Warning, (__VA_ARGS__))

namespace mozilla {
namespace audio {

/*
 * A list containing all IPC clients subscribering the device-changed
 * notifications.
 */
typedef nsTHashtable<nsPtrHashKey<AudioParent>> Subscribers;
static StaticAutoPtr<Subscribers> sSubscribers;
static StaticMutex sMutex;


/*
 * A runnable task to notify the audio device-changed event.
 */
class AudioDeviceChangedRunnable : public Runnable
{
public:
  explicit AudioDeviceChangedRunnable(): Runnable("AudioDeviceChangedRunnable")
  {}

  NS_IMETHOD Run() override
  {
    for (auto iter = sSubscribers->ConstIter(); !iter.Done(); iter.Next()) {
      AudioParent* subscriber = iter.Get()->GetKey();
      ANS_LOG("Notify the AudioParent: %p that "
              "the default device has been changed.", subscriber);
      subscriber->SendDefaultDeviceChange();
    }
    return NS_OK;
  }
}; // class AudioDeviceChangedRunnable


/*
 * An observer for receiving audio device events from Windows.
 */
typedef void (* DefaultDeviceChangedCallback)();
class AudioNotification: public IMMNotificationClient
{
public:
  AudioNotification(DefaultDeviceChangedCallback aCallback)
    : mRefCt(1)
    , mIsRegistered(false)
    , mCallback(aCallback)
  {
    MOZ_ASSERT(mCallback);
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator),
                                  NULL, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&mDeviceEnumerator));

    if (FAILED(hr)) {
      ANS_LOGW("Cannot create an IMMDeviceEnumerator instance.");
      mDeviceEnumerator = nullptr;
      return;
    }

    hr = mDeviceEnumerator->RegisterEndpointNotificationCallback(this);
    if (FAILED(hr)) {
      ANS_LOGW("Cannot register notification callback.");
      mDeviceEnumerator->Release();
      mDeviceEnumerator = nullptr;
      return;
    }

    ANS_LOG("Register notification callback successfully.");
    mIsRegistered = true;
  }

  ~AudioNotification()
  {
    // If mDeviceEnumerator exists, it must be registered.
    MOZ_ASSERT(!!mIsRegistered == !!mDeviceEnumerator);
    if (!mDeviceEnumerator) {
      ANS_LOG("No device enumerator in use.");
      return;
    }

    HRESULT hr = mDeviceEnumerator->UnregisterEndpointNotificationCallback(this);
    if (FAILED(hr)) {
      // We can't really do anything here, so we just add a log for debugging.
      ANS_LOGW("Unregister notification failed.");
    } else {
      ANS_LOG("Unregister notification callback successfully.");
    }

    mDeviceEnumerator->Release();
    mDeviceEnumerator = nullptr;

    mIsRegistered = false;
  }


  // True whenever the notification server is set to report events to this object.
  bool IsRegistered()
  {
    return mIsRegistered;
  }

  // IMMNotificationClient Implementation
  HRESULT STDMETHODCALLTYPE
  OnDefaultDeviceChanged(EDataFlow aFlow, ERole aRole, LPCWSTR aDeviceId) override
  {
    ANS_LOG("Default device has changed: flow %d, role: %d\n", aFlow, aRole);
    mCallback();
    return S_OK;
  }

  // The remaining methods are not implemented. they simply log when called
  // (if log is enabled), for debugging.
  HRESULT STDMETHODCALLTYPE
  OnDeviceAdded(LPCWSTR aDeviceId) override
  {
    ANS_LOG("Audio device added.");
    return S_OK;
  };

  HRESULT STDMETHODCALLTYPE
  OnDeviceRemoved(LPCWSTR aDeviceId) override
  {
    ANS_LOG("Audio device removed.");
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE
  OnDeviceStateChanged(LPCWSTR aDeviceId, DWORD aNewState) override
  {
    ANS_LOG("Audio device state changed.");
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE
  OnPropertyValueChanged(LPCWSTR aDeviceId, const PROPERTYKEY aKey) override
  {
    ANS_LOG("Audio device property value changed.");
    return S_OK;
  }

  // IUnknown Implementation
  ULONG STDMETHODCALLTYPE
  AddRef() override
  {
    return InterlockedIncrement(&mRefCt);
  }

  ULONG STDMETHODCALLTYPE
  Release() override
  {
    ULONG ulRef = InterlockedDecrement(&mRefCt);
    if (0 == ulRef) {
      delete this;
    }
    return ulRef;
  }

  HRESULT STDMETHODCALLTYPE
  QueryInterface(REFIID riid, VOID **ppvInterface) override
  {
    if (__uuidof(IUnknown) == riid) {
      AddRef();
      *ppvInterface = (IUnknown*)this;
    } else if (__uuidof(IMMNotificationClient) == riid) {
      AddRef();
      *ppvInterface = (IMMNotificationClient*)this;
    } else {
      *ppvInterface = NULL;
      return E_NOINTERFACE;
    }
    return S_OK;
  }

private:
  DefaultDeviceChangedCallback mCallback;
  IMMDeviceEnumerator* mDeviceEnumerator;
  LONG mRefCt;
  bool mIsRegistered;
}; // class AudioNotification


/*
 * A singleton observer for audio device changed events.
 */
static AudioNotification* sAudioNotification = nullptr;


/*
 * AudioNotificationSender Implementation
 */
/* static */ nsresult
AudioNotificationSender::Register(AudioParent* aAudioParent)
{
  MOZ_ASSERT(XRE_IsParentProcess());

  if (!sAudioNotification) {
    sAudioNotification = new AudioNotification(NotifyDefaultDeviceChanged);

    if (!sAudioNotification->IsRegistered()) {
      ANS_LOGW("The notification sender cannot be initialized.");
      sAudioNotification->Release();
      sAudioNotification = nullptr;
      return NS_ERROR_FAILURE;
    }
  }

  if (!sSubscribers) {
    sSubscribers = new Subscribers();
  }

  StaticMutexAutoLock lock(sMutex);
  sSubscribers->PutEntry(aAudioParent);

  ANS_LOG("The AudioParent: %p is registered successfully.", aAudioParent);
  return NS_OK;
}

/* static */ void
AudioNotificationSender::Unregister(AudioParent* aAudioParent)
{
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(sSubscribers, "No subscribers registered");
  MOZ_ASSERT(sSubscribers->Contains(aAudioParent),
             "the AudioParent: %p is not registered", aAudioParent);

  StaticMutexAutoLock lock(sMutex);
  sSubscribers->RemoveEntry(aAudioParent);

  ANS_LOG("The AudioParent: %p is unregistered successfully.", aAudioParent);
}

/* static */ void
AudioNotificationSender::NotifyDefaultDeviceChanged()
{
  ANS_LOG("Notify the default device-changed event.");

  RefPtr<AudioDeviceChangedRunnable> runnable = new AudioDeviceChangedRunnable();
  NS_DispatchToMainThread(runnable);
}

} // namespace audio
} // namespace mozilla
