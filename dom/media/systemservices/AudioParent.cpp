#include "AudioParent.h"

namespace mozilla {
namespace audio {

AudioParent::AudioParent()
{
  MOZ_COUNT_CTOR(AudioParent);
  printf_stderr("~~~~~~> AudioParent::AudioParent\n");
}

AudioParent::~AudioParent()
{
  printf_stderr("~~~~~~> AudioParent::~AudioParent\n");
  MOZ_COUNT_DTOR(AudioParent);
}

bool
AudioParent::SendDefaultDeviceChange()
{
  printf_stderr("~~~~~~> AudioParent::SendDefaultDeviceChange\n");
  return PAudioParent::SendDefaultDeviceChange();
}

void
AudioParent::ActorDestroy(ActorDestroyReason aWhy)
{
  printf_stderr("~~~~~~> AudioParent::ActorDestroy\n");
}

} // namespace audio
} // namespace mozilla