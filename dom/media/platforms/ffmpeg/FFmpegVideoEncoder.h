/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_MEDIA_PLATFORMS_FFMPEG_FFMPEGVIDEOENCODER_H_
#define DOM_MEDIA_PLATFORMS_FFMPEG_FFMPEGVIDEOENCODER_H_

#include "PlatformEncoderModule.h"
#include "FFmpegLibWrapper.h"

// This must be the last header included
#include "FFmpegLibs.h"

namespace mozilla {

static AVCodecID GetFFmpegEncoderCodecId(const nsACString& aMimeType);

template <int V, typename ConfigType>
class FFmpegVideoEncoder {};

// TODO: Bug 1860925: FFmpegDataEncoder
template <typename ConfigType>
class FFmpegVideoEncoder<LIBAV_VER, ConfigType> final
    : public MediaDataEncoder {
 public:
  FFmpegVideoEncoder(const FFmpegLibWrapper* aLib, AVCodecID aCodecID,
                     RefPtr<TaskQueue> aTaskQueue, const ConfigType& aConfig);

  /* MediaDataEncoder Methods */
  // All methods run on the task queue, except for GetDescriptionName.
  RefPtr<InitPromise> Init() override;
  RefPtr<EncodePromise> Encode(const MediaData* aSample) override;
  RefPtr<EncodePromise> Drain() override;
  RefPtr<ShutdownPromise> Shutdown() override;
  RefPtr<GenericPromise> SetBitrate(Rate aBitsPerSec) override;
  nsCString GetDescriptionName() const override;

 private:
  ~FFmpegVideoEncoder() = default;

  RefPtr<InitPromise> ProcessInit();
  void ProcessShutdown();

  // This refers to a static FFmpegLibWrapper, so raw pointer is adequate.
  const FFmpegLibWrapper* mLib;        // set in constructor
  const AVCodecID mCodecID;            // set in constructor
  const RefPtr<TaskQueue> mTaskQueue;  // set in constructor
  const ConfigType mConfig;            // set in constructor

  // mTaskQueue only.
  AVCodecContext* mCodecContext;
};

}  // namespace mozilla

#endif /* DOM_MEDIA_PLATFORMS_FFMPEG_FFMPEGVIDEOENCODER_H_ */
