#ifndef DOM_MEDIA_PLATFORMS_WRAPPERS_MEDIAENCODERCHANGEMONITOR_H_
#define DOM_MEDIA_PLATFORMS_WRAPPERS_MEDIAENCODERCHANGEMONITOR_H_

#include "PlatformEncoderModule.h"

namespace mozilla {

// MediaEncoderChangeMonitor is a wrapper around MediaDataEncoder that tracks
// changes in the encoder's input format. It facilitates dynamic reconfiguration
// of the encoder without requiring manual shutdown at the callsite. This is
// particularly useful in scenarios where the encoder must adapt to varying
// input formats. For instance, WebCodecs' VideoEncoder allows encoding video
// frames with different pixel formats.
//
// Some platform encoders may internally convert the input format to a
// predefined format if they only support specific formats. However,
// AppleVTEncoder can be configured to accept multiple pixel formats and create
// a memory buffer pool for inputs, minimizing the need for image copies. In
// such cases, the encoder's input format should match its configured format.
// MediaEncoderChangeMonitor manages the encoder's reconfiguration in these
// situations.
class MediaEncoderChangeMonitor final : public MediaDataEncoder {
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(MediaEncoderChangeMonitor, final);

  using CreateEncoderPromise = PlatformEncoderModule::CreateEncoderPromise;

 public:
  static RefPtr<PlatformEncoderModule::CreateEncoderPromise> Create(
      PlatformEncoderModule* aPEM, const EncoderConfig& aConfig,
      const RefPtr<TaskQueue>& aTaskQueue);

  // MediaDataEncoder interfaces
  RefPtr<InitPromise> Init() override;
  RefPtr<EncodePromise> Encode(const MediaData* aSample) override;
  RefPtr<ReconfigurationPromise> Reconfigure(
      const RefPtr<const EncoderConfigurationChangeList>& aConfigurationChanges)
      override;
  RefPtr<EncodePromise> Drain() override;
  RefPtr<ShutdownPromise> Shutdown() override;
  RefPtr<GenericPromise> SetBitrate(uint32_t aBitsPerSec) override;
  bool IsHardwareAccelerated(nsACString& aFailureReason) const override;
  nsCString GetDescriptionName() const override;

 private:
  MediaEncoderChangeMonitor(PlatformEncoderModule* aPEM,
                            const EncoderConfig& aConfig,
                            const RefPtr<TaskQueue>& aTaskQueue);
  virtual ~MediaEncoderChangeMonitor();

  RefPtr<CreateEncoderPromise> CreateEncoder();
  RefPtr<InitPromise> InitEncoder();
  RefPtr<ReconfigurationPromise> ReconfigureEncoder(
      const RefPtr<const EncoderConfigurationChangeList>&
          aConfigurationChanges);
  RefPtr<EncodePromise> EncodeSample(const MediaData* aSample);
  RefPtr<EncodePromise> DrainEncoder();

  // Dry, shutdown the old encoder, then reinit a new encoder, then encode.
  RefPtr<EncodePromise> EncodeAfterReinit(const MediaData* aSample);

  // TODO:
  // 1. Create a container to store EncodedData.
  // 2. Repeatedly call DrainEncoder() until no more EncodedData is returned,
  // accumulating all EncodedData into the container.
  // 3. Abort the process if a shutdown is initiated while it is still ongoing.
  // RefPtr<EncodePromise> DryEncoder();
  // TODO:
  // 1. Shut down the encoder and monitor the shutdown process.
  // 2. If Shutdown() is invoked while the encoder is still shutting down,
  // forward the ShutdownPromise to the Shutdown() method.
  // RefPtr<ShutdownPromise> ShutdownEncoder();

  // enum class State {
  //   Unset,
  //   Creating,
  //   Initializing,
  //   Ready,
  //   Encoding,
  //   ShuttingDown,
  //   Reinit_Shutdown,
  //   Reinit_Creating,
  //   Reinit_Initializing,
  //   Error
  // };

  MOZ_DEFINE_ENUM_CLASS_WITH_TOSTRING_AT_CLASS_SCOPE(
      State, (Unset, Creating, Uninit, Initializing, Inited, Reconfiguring,
              Encoding, Draining, Drying, ShuttingDown, Error));
  void SetState(State aState);

  const nsCOMPtr<nsISerialEventTarget> mThread;
  const RefPtr<PlatformEncoderModule> mPEM;
  const RefPtr<TaskQueue> mTaskQueue;
  EncoderConfig mConfig;
  RefPtr<MediaDataEncoder> mEncoder = nullptr;

  State mState = State::Unset;

  MozPromiseHolder<ShutdownPromise> mShutdownWhileCreationPromise;

  MozPromiseHolder<CreateEncoderPromise> mCreatePromise;
  MozPromiseRequestHolder<CreateEncoderPromise> mCreateRequest;
};

}  // namespace mozilla

#endif  // DOM_MEDIA_PLATFORMS_WRAPPERS_MEDIAENCODERCHANGEMONITOR_H_
