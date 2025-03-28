#include "MediaEncoderChangeMonitor.h"

namespace mozilla {

extern LazyLogModule sPEMLog;

#ifdef LOG_INTERNAL
#  undef LOG_INTERNAL
#endif  // LOG_INTERNAL
#define LOG_INTERNAL(level, msg, ...) \
  MOZ_LOG(sPEMLog, LogLevel::level, (msg, ##__VA_ARGS__))

#ifdef LOG
#  undef LOG
#endif  // LOG
#define LOG(msg, ...) LOG_INTERNAL(Debug, msg, ##__VA_ARGS__)

#ifdef LOGW
#  undef LOGW
#endif  // LOGE
#define LOGW(msg, ...) LOG_INTERNAL(Warning, msg, ##__VA_ARGS__)

#ifdef LOGE
#  undef LOGE
#endif  // LOGE
#define LOGE(msg, ...) LOG_INTERNAL(Error, msg, ##__VA_ARGS__)

#ifdef LOGV
#  undef LOGV
#endif  // LOGV
#define LOGV(msg, ...) LOG_INTERNAL(Verbose, msg, ##__VA_ARGS__)

RefPtr<PlatformEncoderModule::CreateEncoderPromise>
MediaEncoderChangeMonitor::Create(PlatformEncoderModule* aPEM,
                                  const EncoderConfig& aConfig,
                                  const RefPtr<TaskQueue>& aTaskQueue) {
  RefPtr<MediaEncoderChangeMonitor> monitor =
      new MediaEncoderChangeMonitor(aPEM, aConfig, aTaskQueue);
  RefPtr<CreateEncoderPromise> promise = monitor->CreateEncoder()->Then(
      GetCurrentSerialEventTarget(), __func__,
      [m = monitor](RefPtr<MediaDataEncoder> aEncoder) {
        return CreateEncoderPromise::CreateAndResolve(m, __func__);
      },
      [](const MediaResult& aError) {
        return CreateEncoderPromise::CreateAndReject(aError, __func__);
      });
  return promise;
}

RefPtr<MediaDataEncoder::InitPromise> MediaEncoderChangeMonitor::Init() {
  MOZ_ASSERT(mThread->IsOnCurrentThread());
  return InitEncoder();
}

RefPtr<MediaDataEncoder::EncodePromise> MediaEncoderChangeMonitor::Encode(
    const MediaData* aSample) {
  MOZ_ASSERT(mThread->IsOnCurrentThread());
  return EncodeSample(aSample);
}

RefPtr<MediaDataEncoder::ReconfigurationPromise>
MediaEncoderChangeMonitor::Reconfigure(
    const RefPtr<const EncoderConfigurationChangeList>& aConfigurationChanges) {
  MOZ_ASSERT(mThread->IsOnCurrentThread());
  return ReconfigureEncoder(aConfigurationChanges);
}

RefPtr<MediaDataEncoder::EncodePromise> MediaEncoderChangeMonitor::Drain() {
  MOZ_ASSERT(mThread->IsOnCurrentThread());
  return DrainEncoder();
}

RefPtr<ShutdownPromise> MediaEncoderChangeMonitor::Shutdown() {
  MOZ_ASSERT(mThread->IsOnCurrentThread());
  MOZ_ASSERT(mShutdownWhileCreationPromise.IsEmpty(),
             "Shutdown alInited in progress");

  auto r =
      MediaResult(NS_ERROR_DOM_MEDIA_CANCELED, "Canceled by encoder shutdown");

  // If the encoder creation has not been completed yet, wait until the decoder
  // being created has been shut down.
  if (mCreateRequest.Exists()) {
    MOZ_ASSERT(!mCreatePromise.IsEmpty());
    MOZ_ASSERT(!mEncoder);
    MOZ_ASSERT(mState == State::Creating);

    LOGW(
        "MediaEncoderChangeMonitor %p is shutting down during encoder "
        "creation. Rejecting the create promise and deferring shutdown until "
        "the created encoder is fully shut down.",
        this);

    mCreatePromise.Reject(r, __func__);
    SetState(State::ShuttingDown);
    return mShutdownWhileCreationPromise.Ensure(__func__);
  }

  // TODO: If shutdown is called while the old encoder shutdonw is in progress,
  // wait until the shutdown is completed.

  // If encoder creation has been completed but failed, no encoder is set.
  if (!mEncoder) {
    LOG("MediaEncoderChangeMonitor %p is shutting down with no encoder", this);
    MOZ_ASSERT(mCreatePromise.IsEmpty());
    // ~MediaEncoderChangeMonitor() will ensure the state is set to Unset.
    SetState(State::Unset);
    return ShutdownPromise::CreateAndResolve(true, __func__);
  }

  // If encoder creation has succeeded, we must have the encoder now.

  MOZ_ASSERT(mCreatePromise.IsEmpty());

  SetState(State::Unset);
  RefPtr<MediaDataEncoder> encoder = std::move(mEncoder);
  return encoder->Shutdown();
}

RefPtr<GenericPromise> MediaEncoderChangeMonitor::SetBitrate(
    uint32_t aBitsPerSec) {
  // TODO: Implement this function
  return GenericPromise::CreateAndReject(
      MediaResult(NS_ERROR_NOT_IMPLEMENTED, "Not implemented"), __func__);
}

bool MediaEncoderChangeMonitor::IsHardwareAccelerated(
    nsACString& aFailureReason) const {
  MOZ_ASSERT(mThread->IsOnCurrentThread());
  return mEncoder ? mEncoder->IsHardwareAccelerated(aFailureReason) : false;
}

nsCString MediaEncoderChangeMonitor::GetDescriptionName() const {
  // TODO: Implement this function
  return nsCString();
}

MediaEncoderChangeMonitor::MediaEncoderChangeMonitor(
    PlatformEncoderModule* aPEM, const EncoderConfig& aConfig,
    const RefPtr<TaskQueue>& aTaskQueue)
    : mThread(GetCurrentSerialEventTarget()),
      mPEM(aPEM),
      mTaskQueue(aTaskQueue),
      mConfig(aConfig),
      mEncoder(nullptr),
      mState(State::Unset) {
  MOZ_ASSERT(mThread);
  MOZ_ASSERT(mPEM);
  MOZ_ASSERT(mTaskQueue);
  LOG("MediaEncoderChangeMonitor %p created", this);
}

MediaEncoderChangeMonitor::~MediaEncoderChangeMonitor() {
  MOZ_ASSERT(mThread->IsOnCurrentThread());
  MOZ_ASSERT(mState == State::Unset);
  MOZ_ASSERT(!mEncoder, "Encoder should be null");
  LOG("MediaEncoderChangeMonitor %p destroyed", this);
}

RefPtr<PlatformEncoderModule::CreateEncoderPromise>
MediaEncoderChangeMonitor::CreateEncoder() {
  MOZ_ASSERT(mThread->IsOnCurrentThread());
  MOZ_ASSERT(mCreatePromise.IsEmpty());
  MOZ_ASSERT(!mCreateRequest.Exists());
  MOZ_ASSERT(!mEncoder);
  // TODO: Check state?

  SetState(State::Creating);
  LOG("Creating encoder with %s", mConfig.ToString().get());

  RefPtr<CreateEncoderPromise> p = mCreatePromise.Ensure(__func__);
  mPEM->AsyncCreateEncoder(mConfig, mTaskQueue)
      ->Then(
          mThread, __func__,
          [self = RefPtr{this}](RefPtr<MediaDataEncoder>&& aEncoder) {
            self->mCreateRequest.Complete();

            // If MediaEncoderChangeMonitor has been shutdown, shut the created
            // decoder down and return. mCreatePromise should be empty now.
            if (!self->mShutdownWhileCreationPromise.IsEmpty()) {
              MOZ_ASSERT(self->mState == State::ShuttingDown);
              MOZ_ASSERT(self->mCreatePromise.IsEmpty(),
                         "create promise should have been rejected");

              LOGW(
                  "MediaEncoderChangeMonitor %p has been shut down. Proceeding "
                  "to shut down the newly created encoder",
                  self.get());

              aEncoder->Shutdown()->Then(
                  self->mThread, __func__,
                  [self](const ShutdownPromise::ResolveOrRejectValue& aValue) {
                    MOZ_ASSERT(self->mState == State::ShuttingDown);

                    LOGW(
                        "MediaEncoderChangeMonitor %p, newly created encoder "
                        "shutdown has been %s",
                        self.get(),
                        aValue.IsResolve() ? "resolved" : "rejected");

                    self->SetState(State::Unset);
                    self->mShutdownWhileCreationPromise.ResolveOrReject(
                        aValue, __func__);
                  });

              return;
            }

            self->mEncoder = std::move(aEncoder);
            LOG("MediaEncoderChangeMonitor %p created encoder %p", self.get(),
                self->mEncoder.get());
            self->SetState(State::Uninit);
            self->mCreatePromise.Resolve(self, __func__);
          },
          [self = RefPtr{this}](const MediaResult& aError) {
            self->mCreateRequest.Complete();

            // If MediaEncoderChangeMonitor has been shutdown, we should resolve
            // the shutdown promise.
            if (!self->mShutdownWhileCreationPromise.IsEmpty()) {
              MOZ_ASSERT(self->mState == State::ShuttingDown);
              MOZ_ASSERT(self->mCreatePromise.IsEmpty(),
                         "create promise should have been rejected");

              LOGW(
                  "MediaEncoderChangeMonitor %p was shut down. Resolving the "
                  "shutdown promise immediately as encoder creation did not "
                  "succeed",
                  self.get());

              self->SetState(State::Unset);
              self->mShutdownWhileCreationPromise.Resolve(true, __func__);
              return;
            }

            LOGE("MediaEncoderChangeMonitor %p failed to create encoder: %s",
                 self.get(), aError.Description().get());
            self->SetState(State::Unset);
            self->mCreatePromise.Reject(aError, __func__);
          })
      ->Track(mCreateRequest);

  return p;
}

RefPtr<MediaDataEncoder::InitPromise> MediaEncoderChangeMonitor::InitEncoder() {
  MOZ_ASSERT(mThread->IsOnCurrentThread());
  MOZ_ASSERT(mEncoder);
  MOZ_ASSERT(mState == State::Uninit);

  LOG("MediaEncoderChangeMonitor %p initializing encoder %p", this,
      mEncoder.get());
  SetState(State::Initializing);

  return mEncoder->Init()->Then(
      mThread, __func__,
      [self = RefPtr{this}](const InitPromise::ResolveOrRejectValue& aValue) {
        if (aValue.IsResolve()) {
          LOG("MediaEncoderChangeMonitor %p encoder initialized", self.get());
          self->SetState(State::Inited);
        } else {
          LOGE("MediaEncoderChangeMonitor %p encoder initialization failed",
               self.get());
          self->SetState(State::Error);
        }
        return InitPromise::CreateAndResolveOrReject(aValue, __func__);
      });
}

RefPtr<MediaDataEncoder::ReconfigurationPromise>
MediaEncoderChangeMonitor::ReconfigureEncoder(
    const RefPtr<const EncoderConfigurationChangeList>& aConfigurationChanges) {
  MOZ_ASSERT(mThread->IsOnCurrentThread());
  MOZ_ASSERT(mEncoder);
  MOZ_ASSERT(mState == State::Inited);

  LOG("MediaEncoderChangeMonitor %p reconfiguring encoder %p", this,
      mEncoder.get());
  SetState(State::Reconfiguring);

  return mEncoder->Reconfigure(aConfigurationChanges)
      ->Then(
          mThread, __func__,
          [self = RefPtr{this}](
              const ReconfigurationPromise::ResolveOrRejectValue& aValue) {
            if (aValue.IsResolve()) {
              LOG("MediaEncoderChangeMonitor %p encoder reconfigured",
                  self.get());
              self->SetState(State::Inited);
            } else {
              LOGE(
                  "MediaEncoderChangeMonitor %p encoder reconfiguration failed",
                  self.get());
              self->SetState(State::Error);
            }
            return ReconfigurationPromise::CreateAndResolveOrReject(aValue,
                                                                    __func__);
          });
}

RefPtr<MediaDataEncoder::EncodePromise> MediaEncoderChangeMonitor::EncodeSample(
    const MediaData* aSample) {
  MOZ_ASSERT(mThread->IsOnCurrentThread());
  MOZ_ASSERT(mEncoder);
  MOZ_ASSERT(mState == State::Inited);

  RefPtr<const VideoData> sample(aSample->As<const VideoData>());
  if (!sample) {
    return EncodePromise::CreateAndReject(
        MediaResult(
            NS_ERROR_DOM_MEDIA_FATAL_ERR,
            nsPrintfCString("Sample(%s) is not a VideoData",
                            MediaData::EnumValueToString(aSample->mType))),
        __func__);
  }
  if (!sample->mImage) {
    return EncodePromise::CreateAndReject(
        MediaResult(
            NS_ERROR_DOM_MEDIA_FATAL_ERR,
            nsPrintfCString("%s has no image", sample->ToString().get())),
        __func__);
  }

  auto r = EncoderConfig::SampleFormat::FromImage(sample->mImage);
  if (r.isErr()) {
    MediaResult err = r.unwrapErr();
    LOGE("%s", err.Description().get());
    return EncodePromise::CreateAndReject(err, __func__);
  }

  EncoderConfig::SampleFormat sf = r.unwrap();
  if (sf != mConfig.mFormat) {
    return EncodeAfterReinit(sample);
  }

  LOG("MediaEncoderChangeMonitor %p encoding sample %p", this, aSample);
  SetState(State::Encoding);

  // EncodedData's copy ctor is implicitly deleted, so we need to have two
  // separate lambdas to handle the success and error cases.
  return mEncoder->Encode(aSample)->Then(
      mThread, __func__,
      [self = RefPtr{this}](MediaDataEncoder::EncodedData&& aData) {
        LOG("MediaEncoderChangeMonitor %p encoder encoded sample", self.get());
        self->SetState(State::Inited);
        return EncodePromise::CreateAndResolve(std::move(aData), __func__);
      },
      [self = RefPtr{this}](const MediaResult& aError) {
        LOGE("MediaEncoderChangeMonitor %p encoder encoding failed",
             self.get());
        self->SetState(State::Error);
        return EncodePromise::CreateAndReject(aError, __func__);
      });
}

RefPtr<MediaDataEncoder::EncodePromise>
MediaEncoderChangeMonitor::DrainEncoder() {
  MOZ_ASSERT(mThread->IsOnCurrentThread());
  MOZ_ASSERT(mEncoder);
  MOZ_ASSERT(mState == State::Inited);

  LOG("MediaEncoderChangeMonitor %p draining encoder %p", this, mEncoder.get());
  SetState(State::Draining);

  // EncodedData's copy ctor is implicitly deleted, so we need to have two separate
  // lambdas to handle the success and error cases.
  return mEncoder->Drain()->Then(
      mThread, __func__,
      [self = RefPtr{this}](MediaDataEncoder::EncodedData&& aData) {
        LOG("MediaEncoderChangeMonitor %p encoder drained", self.get());
        self->SetState(State::Inited);
        return EncodePromise::CreateAndResolve(std::move(aData), __func__);
      },
      [self = RefPtr{this}](const MediaResult& aError) {
        LOGE("MediaEncoderChangeMonitor %p encoder draining failed",
             self.get());
        self->SetState(State::Error);
        return EncodePromise::CreateAndReject(aError, __func__);
      });
}

RefPtr<MediaDataEncoder::EncodePromise>
MediaEncoderChangeMonitor::EncodeAfterReinit(const MediaData* aSample) {
  // TODO: Implement this function
  return EncodePromise::CreateAndReject(
      MediaResult(NS_ERROR_NOT_IMPLEMENTED, "Not implemented"), __func__);
}

void MediaEncoderChangeMonitor::SetState(State aState) {
  LOG("State changed from %s to %s", EnumValueToString(mState),
      EnumValueToString(aState));
  mState = aState;
}

#undef LOG
#undef LOGW
#undef LOGE
#undef LOGV
#undef LOG_INTERNAL

}  // namespace mozilla
