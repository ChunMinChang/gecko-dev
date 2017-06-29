#ifndef mozilla_AudioParent_h
#define mozilla_AudioParent_h

#include "mozilla/dom/ContentParent.h"
#include "mozilla/audio/PAudioParent.h"

namespace mozilla {
namespace audio {

class AudioParent : public PAudioParent
{
public:
  explicit AudioParent();

  virtual ~AudioParent();

  bool SendDefaultDeviceChange();

private:
  virtual void ActorDestroy(ActorDestroyReason aWhy) override;
};

} // namespace audio
} // namespace mozilla

#endif  // mozilla_AudioParent_h