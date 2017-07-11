/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_AudioChild_h
#define mozilla_AudioChild_h

#include "mozilla/audio/PAudioChild.h"

namespace mozilla {
namespace audio {

class AudioChild : public PAudioChild
{
public:
  static AudioChild* GetSingleton();

  bool SendPing();

private:
  virtual mozilla::ipc::IPCResult RecvPong() override;
};

} // namespace audio
} // namespace mozilla

#endif // mozilla_AudioChild_h