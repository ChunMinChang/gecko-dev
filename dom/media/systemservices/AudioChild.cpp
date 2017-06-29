#include "AudioChild.h"
#include "mozilla/dom/ContentChild.h"

namespace mozilla {
namespace audio {

// MOZ_IMPLICIT AudioChild::AudioChild()
// {
//   MOZ_COUNT_CTOR(AudioChild);
// }

// AudioChild::~AudioChild()
// {
//   MOZ_COUNT_DTOR(AudioChild);
// }

static AudioChild* sChild;

class AudioIPCRunnable : public Runnable
{
public:
  explicit AudioIPCRunnable(): Runnable("AudioIPCRunnable")
  {}

  NS_IMETHOD Run() override
  {
    AudioChild::GetSingleton();
    return NS_OK;
  }
};

/* static */ void
AudioChild::BuildAudioChannel()
{
  MOZ_ASSERT(XRE_IsContentProcess());
  MOZ_ASSERT(NS_IsMainThread());

  if (sChild) {
    return;
  }

  RefPtr<AudioIPCRunnable> runnable = new AudioIPCRunnable();
  NS_DispatchToMainThread(runnable);
}

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
  printf_stderr("~~~~~~> AudioChild::RecvDefaultDeviceChange\n");
  return IPC_OK();
}

} // namespace audio
} // namespace mozilla