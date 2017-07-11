/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AudioParent.h"
#include "nsTArray.h"
#include "mozilla/RefPtr.h"
#include "mozilla/StaticMutex.h"

namespace mozilla {
namespace audio {

AudioParent::AudioParent()
{
  MOZ_COUNT_CTOR(AudioParent);
  printf(">>> PAudio is constructed on parent side.\n");
}

AudioParent::~AudioParent()
{
  MOZ_COUNT_DTOR(AudioParent);
}

bool
AudioParent::SendPong()
{
  printf(">>> AudioParent::SendPong\n");
  return PAudioParent::SendPong();
}

mozilla::ipc::IPCResult
AudioParent::RecvPing()
{
  printf(">>> AudioParent::RecvPing\n");
  SendPong();
  return IPC_OK();
}

void
AudioParent::ActorDestroy(ActorDestroyReason aWhy)
{
}

} // namespace audio
} // namespace mozilla