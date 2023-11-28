/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "FFmpegVideoEncoder.h"

#include "FFmpegLog.h"
#include "FFmpegRuntimeLinker.h"
#include "VPXDecoder.h"
#include "nsPrintfCString.h"

namespace mozilla {

#if LIBAVCODEC_VERSION_MAJOR > 55
#  define AVCODEC_PIX_FMT_NONE AVPixelFormat::AV_PIX_FMT_NONE
#  define AVCODEC_PIX_FMT_RGBA AVPixelFormat::AV_PIX_FMT_RGBA
#  define AVCODEC_PIX_FMT_BGRA AVPixelFormat::AV_PIX_FMT_BGRA
#  define AVCODEC_PIX_FMT_RGB24 AVPixelFormat::AV_PIX_FMT_RGB24
#  define AVCODEC_PIX_FMT_BGR24 AVPixelFormat::AV_PIX_FMT_BGR24
#  define AVCODEC_PIX_FMT_GRAY8 AVPixelFormat::AV_PIX_FMT_GRAY8
#  define AVCODEC_PIX_FMT_YUV444P AVPixelFormat::AV_PIX_FMT_YUV444P
#  define AVCODEC_PIX_FMT_YUV422P AVPixelFormat::AV_PIX_FMT_YUV422P
#  define AVCODEC_PIX_FMT_YUV420P AVPixelFormat::AV_PIX_FMT_YUV420P
#  define AVCODEC_PIX_FMT_NV12 AVPixelFormat::AV_PIX_FMT_NV12
#  define AVCODEC_PIX_FMT_NV21 AVPixelFormat::AV_PIX_FMT_NV21
#else
#  define AVCODEC_PIX_FMT_NONE PixelFormat::PIX_FMT_NONE
#  define AVCODEC_PIX_FMT_RGBA PixelFormat::PIX_FMT_RGBA
#  define AVCODEC_PIX_FMT_BGRA PixelFormat::PIX_FMT_BGRA
#  define AVCODEC_PIX_FMT_RGB24 PixelFormat::PIX_FMT_RGB24
#  define AVCODEC_PIX_FMT_BGR24 PixelFormat::PIX_FMT_BGR24
#  define AVCODEC_PIX_FMT_GRAY8 PixelFormat::PIX_FMT_GRAY8
#  define AVCODEC_PIX_FMT_YUV444P PixelFormat::PIX_FMT_YUV444P
#  define AVCODEC_PIX_FMT_YUV422P PixelFormat::PIX_FMT_YUV422P
#  define AVCODEC_PIX_FMT_YUV420P PixelFormat::PIX_FMT_YUV420P
#  define AVCODEC_PIX_FMT_NV12 PixelFormat::PIX_FMT_NV12
#  define AVCODEC_PIX_FMT_NV21 PixelFormat::PIX_FMT_NV21
#endif

static AVPixelFormat ToAVPixelFormat(
    const MediaDataEncoder::PixelFormat& aFormat) {
  switch (aFormat) {
    case MediaDataEncoder::PixelFormat::RGBA32:
      return AVCODEC_PIX_FMT_RGBA;
    case MediaDataEncoder::PixelFormat::BGRA32:
      return AVCODEC_PIX_FMT_BGRA;
    case MediaDataEncoder::PixelFormat::RGB24:
      return AVCODEC_PIX_FMT_RGB24;
    case MediaDataEncoder::PixelFormat::BGR24:
      return AVCODEC_PIX_FMT_BGR24;
    case MediaDataEncoder::PixelFormat::GRAY8:
      return AVCODEC_PIX_FMT_GRAY8;
    case MediaDataEncoder::PixelFormat::YUV444P:
      return AVCODEC_PIX_FMT_YUV444P;
    case MediaDataEncoder::PixelFormat::YUV422P:
      return AVCODEC_PIX_FMT_YUV422P;
    case MediaDataEncoder::PixelFormat::YUV420P:
      return AVCODEC_PIX_FMT_YUV420P;
    case MediaDataEncoder::PixelFormat::YUV420SP_NV12:
      return AVCODEC_PIX_FMT_NV12;
    case MediaDataEncoder::PixelFormat::YUV420SP_NV21:
      return AVCODEC_PIX_FMT_NV21;
    case MediaDataEncoder::PixelFormat::HSV:
    case MediaDataEncoder::PixelFormat::Lab:
    case MediaDataEncoder::PixelFormat::DEPTH:
    case MediaDataEncoder::PixelFormat::EndGuard_:
      break;
  }
  return AVCODEC_PIX_FMT_NONE;
}

/* static */
AVCodecID GetFFmpegEncoderCodecId(const nsACString& aMimeType) {
#if LIBAVCODEC_VERSION_MAJOR >= 54
  if (VPXDecoder::IsVP8(aMimeType)) {
    return AV_CODEC_ID_VP8;
  }
#endif

#if LIBAVCODEC_VERSION_MAJOR >= 55
  if (VPXDecoder::IsVP9(aMimeType)) {
    return AV_CODEC_ID_VP9;
  }
#endif
  return AV_CODEC_ID_NONE;
}

template <typename ConfigType>
FFmpegVideoEncoder<LIBAV_VER, ConfigType>::FFmpegVideoEncoder(
    const FFmpegLibWrapper* aLib, AVCodecID aCodecID,
    RefPtr<TaskQueue> aTaskQueue, const ConfigType& aConfig)
    : mLib(aLib),
      mCodecID(aCodecID),
      mTaskQueue(aTaskQueue),
      mConfig(aConfig),
      mCodecContext(nullptr) {
  MOZ_ASSERT(mLib);
  MOZ_ASSERT(mTaskQueue);
};

template <typename ConfigType>
RefPtr<MediaDataEncoder::InitPromise>
FFmpegVideoEncoder<LIBAV_VER, ConfigType>::Init() {
  FFMPEGV_LOG("Init");
  return InvokeAsync(mTaskQueue, this, __func__,
                     &FFmpegVideoEncoder::ProcessInit);
}

template <typename ConfigType>
RefPtr<MediaDataEncoder::EncodePromise>
FFmpegVideoEncoder<LIBAV_VER, ConfigType>::Encode(const MediaData* aSample) {
  FFMPEGV_LOG("Encode");
  return EncodePromise::CreateAndReject(NS_ERROR_NOT_IMPLEMENTED, __func__);
}

template <typename ConfigType>
RefPtr<MediaDataEncoder::EncodePromise>
FFmpegVideoEncoder<LIBAV_VER, ConfigType>::Drain() {
  FFMPEGV_LOG("Drain");
  return EncodePromise::CreateAndReject(NS_ERROR_NOT_IMPLEMENTED, __func__);
}

template <typename ConfigType>
RefPtr<ShutdownPromise> FFmpegVideoEncoder<LIBAV_VER, ConfigType>::Shutdown() {
  FFMPEGV_LOG("Shutdown");

  RefPtr<FFmpegVideoEncoder<LIBAV_VER, ConfigType>> self = this;
  return InvokeAsync(mTaskQueue, __func__, [self]() {
    self->ProcessShutdown();
    return self->mTaskQueue->BeginShutdown();
  });
}

template <typename ConfigType>
RefPtr<GenericPromise> FFmpegVideoEncoder<LIBAV_VER, ConfigType>::SetBitrate(
    Rate aBitsPerSec) {
  FFMPEGV_LOG("SetBitrate");
  return GenericPromise::CreateAndReject(NS_ERROR_NOT_IMPLEMENTED, __func__);
}

template <typename ConfigType>
nsCString FFmpegVideoEncoder<LIBAV_VER, ConfigType>::GetDescriptionName()
    const {
  const char* linker =
#ifdef USING_MOZFFVPX
      "ffvpx";
#else
      "ffmpeg";
#endif
  return nsPrintfCString("%s video encoder (%s)", linker,
                         FFmpegRuntimeLinker::LinkStatusLibraryName());
}

template <typename ConfigType>
RefPtr<MediaDataEncoder::InitPromise>
FFmpegVideoEncoder<LIBAV_VER, ConfigType>::ProcessInit() {
  MOZ_ASSERT(mTaskQueue->IsOnCurrentThread());

  AVCodec* codec = mLib->avcodec_find_encoder(mCodecID);
  if (!codec) {
    FFMPEGV_LOG("failed to find ffmpeg encoder for codec id %d", mCodecID);
    return InitPromise::CreateAndReject(
        MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                    RESULT_DETAIL("Unable to find codec")),
        __func__);
  }
  FFMPEGV_LOG("find codec: %s", codec->name);

  MOZ_ASSERT(!mCodecContext);
  if (!(mCodecContext = mLib->avcodec_alloc_context3(codec))) {
    FFMPEGV_LOG("failed to allocate ffmpeg context for codec %s", codec->name);
    return InitPromise::CreateAndReject(
        MediaResult(NS_ERROR_OUT_OF_MEMORY,
                    RESULT_DETAIL("Failed to initialize ffmpeg context")),
        __func__);
  }

  // TODO: setting mCodecContext.

  FFMPEGV_LOG("%s has been initialized", codec->name);
  return InitPromise::CreateAndResolve(TrackInfo::kVideoTrack, __func__);
}

template <typename ConfigType>
void FFmpegVideoEncoder<LIBAV_VER, ConfigType>::ProcessShutdown() {
  MOZ_ASSERT(mTaskQueue->IsOnCurrentThread());

  if (mCodecContext) {
    mLib->av_freep(&mCodecContext);
  }
}

template class FFmpegVideoEncoder<LIBAV_VER, MediaDataEncoder::VP8Config>;
template class FFmpegVideoEncoder<LIBAV_VER, MediaDataEncoder::VP9Config>;

}  // namespace mozilla
