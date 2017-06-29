#ifndef mozilla_AudioChild_h
#define mozilla_AudioChild_h

#include "mozilla/dom/ContentChild.h"
#include "mozilla/audio/PAudioChild.h"
// #include "mozilla/audio/PAudioParent.h"

namespace mozilla {
namespace audio {

class AudioChild : public PAudioChild
{
public:
  // explicit AudioChild();
  // virtual ~AudioChild();
  static AudioChild* GetSingleton();
  static void BuildAudioChannel();

private:
  virtual mozilla::ipc::IPCResult RecvDefaultDeviceChange() override;
};

} // namespace audio
} // namespace mozilla

#endif  // mozilla_AudioChild_h