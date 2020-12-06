/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ImageLogging.h"  // Must appear first

#include "nsAVIFDecoder.h"

#include "aom/aomdx.h"

#include "mozilla/gfx/Types.h"
#include "YCbCrUtils.h"

#include "SurfacePipeFactory.h"

#include "mozilla/Telemetry.h"

using namespace mozilla::gfx;

namespace mozilla {
namespace image {

using Telemetry::LABELS_AVIF_BIT_DEPTH;
using Telemetry::LABELS_AVIF_DECODE_RESULT;
using Telemetry::LABELS_AVIF_DECODER;
using Telemetry::LABELS_AVIF_YUV_COLOR_SPACE;
using Telemetry::ScalarID;

static LazyLogModule sAVIFLog("AVIFDecoder");

static const LABELS_AVIF_BIT_DEPTH gColorDepthLabel[] = {
    LABELS_AVIF_BIT_DEPTH::color_8, LABELS_AVIF_BIT_DEPTH::color_10,
    LABELS_AVIF_BIT_DEPTH::color_12, LABELS_AVIF_BIT_DEPTH::color_16,
    LABELS_AVIF_BIT_DEPTH::unknown};

static const LABELS_AVIF_YUV_COLOR_SPACE gColorSpaceLabel[static_cast<size_t>(
    gfx::YUVColorSpace::_NUM_COLORSPACE)] = {
    LABELS_AVIF_YUV_COLOR_SPACE::BT601, LABELS_AVIF_YUV_COLOR_SPACE::BT709,
    LABELS_AVIF_YUV_COLOR_SPACE::BT2020, LABELS_AVIF_YUV_COLOR_SPACE::identity,
    LABELS_AVIF_YUV_COLOR_SPACE::unknown};

class Parser {
 public:
  static Parser* Create(const Mp4parseIo* aIo) {
    MOZ_ASSERT(aIo);
    UniquePtr<Parser> p(new Parser(aIo));
    if (!p->Init()) {
      return nullptr;
    }
    MOZ_ASSERT(p->mParser);
    return p.release();
  }

  ~Parser() { MOZ_LOG(sAVIFLog, LogLevel::Debug, ("Destroy Parser=%p", this)); }

  bool Init() {
    MOZ_ASSERT(!mParser);
    Mp4parseStatus status = mp4parse_avif_new(mIo, &mParser);
    MOZ_LOG(sAVIFLog, LogLevel::Debug,
            ("[this=%p] mp4parse_avif_new status: %d", this, status));
    return status == MP4PARSE_STATUS_OK;
  }

  Mp4parseByteData* GetPrimaryItem() {
    if (mPrimaryItem.isNothing()) {
      Mp4parseByteData primaryItem = {};
      Mp4parseStatus status =
          mp4parse_avif_get_primary_item(mParser, &primaryItem);
      MOZ_LOG(sAVIFLog, LogLevel::Debug,
              ("[this=%p] mp4parse_avif_get_primary_item -> %d; length: %u",
               this, status, primaryItem.length));
      if (status != MP4PARSE_STATUS_OK) {
        return nullptr;
      }
      mPrimaryItem.emplace(primaryItem);
    }
    return mPrimaryItem.ptr();
  }

  struct AlphaByteData {
    Mp4parseByteData alphaItem;
    bool premultipliedAlpha;
  };
  AlphaByteData* GetAlphaData() {
    if (mAlphaData.isNothing()) {
      AlphaByteData alphaData = {};
      Mp4parseStatus status = mp4parse_avif_get_alpha_item(
          mParser, &(alphaData.alphaItem), &(alphaData.premultipliedAlpha));
      MOZ_LOG(sAVIFLog, LogLevel::Debug,
              ("[this=%p] mp4parse_avif_get_alpha_item -> %d; length: %u, "
               "premultipliedAlpha: %d",
               this, status, alphaData.alphaItem.length,
               alphaData.premultipliedAlpha));
      if (status != MP4PARSE_STATUS_OK) {
        return nullptr;
      }
      mAlphaData.emplace(alphaData);
    }
    return mAlphaData.ptr();
  }

 private:
  explicit Parser(const Mp4parseIo* aIo) : mIo(aIo) {
    MOZ_ASSERT(mIo);
    MOZ_LOG(sAVIFLog, LogLevel::Debug, ("Create Parser=%p", this));
  }

  const Mp4parseIo* mIo;
  Mp4parseAvifParser* mParser = nullptr;
  Maybe<Mp4parseByteData> mPrimaryItem;
  Maybe<AlphaByteData> mAlphaData;
};

// An interface to do decode and get the decoded data
class ImageDecoder {
 public:
  using Dav1dResult = nsAVIFDecoder::Dav1dResult;
  using NonAOMCodecError = nsAVIFDecoder::NonAOMCodecError;
  using AOMResult = nsAVIFDecoder::AOMResult;
  using NonDecoderResult = nsAVIFDecoder::NonDecoderResult;
  using DecodeResult = nsAVIFDecoder::DecodeResult;

  virtual ~ImageDecoder() = default;

  // Set the mDecodedData if Decode() succeeds
  virtual DecodeResult Decode(bool aIsMetadataDecode) = 0;
  // Must be called after Decode() succeeds
  YCbCrAData& GetDecodedData() {
    MOZ_ASSERT(mDecodedData.isSome());
    return mDecodedData.ref();
  }

 protected:
  explicit ImageDecoder(Mp4parseIo* aIo) : mIo(aIo) { MOZ_ASSERT(mIo); }

  virtual nsAVIFDecoder::DecodeResult Init() = 0;

  bool InitParser() {
    MOZ_ASSERT(!mParser);
    mParser.reset(Parser::Create(mIo));
    return !!mParser;
  }

  inline static bool IsDecodeSuccess(const DecodeResult& aResult) {
    return nsAVIFDecoder::IsDecodeSuccess(aResult);
  }

  const Mp4parseIo* mIo;
  UniquePtr<Parser> mParser;

  // The YCbCrAData is valid after Decode() succeeds
  Maybe<YCbCrAData> mDecodedData;
};

class Dav1dDecoder : ImageDecoder {
 public:
  ~Dav1dDecoder() {
    MOZ_LOG(sAVIFLog, LogLevel::Verbose, ("Destroy Dav1dDecoder=%p", this));

    if (mPicture) {
      dav1d_picture_unref(mPicture.take().ptr());
    }

    if (mAlphaPlane) {
      dav1d_picture_unref(mAlphaPlane.take().ptr());
    }

    MOZ_ASSERT(mContext);
    dav1d_close(&mContext);
    MOZ_ASSERT(!mContext);
  }

  static DecodeResult Create(Mp4parseIo* aIo,
                             UniquePtr<ImageDecoder>& aDecoder) {
    UniquePtr<Dav1dDecoder> d(new Dav1dDecoder(aIo));
    DecodeResult r = d->Init();
    if (IsDecodeSuccess(r)) {
      MOZ_ASSERT(d->mContext);
      aDecoder.reset(d.release());
    }
    return r;
  }

  DecodeResult Decode(bool aIsMetadataDecode) override {
    MOZ_ASSERT(mParser);
    MOZ_ASSERT(mContext);
    MOZ_ASSERT(mPicture.isNothing());
    MOZ_ASSERT(mDecodedData.isNothing());

    MOZ_LOG(sAVIFLog, LogLevel::Verbose, ("[this=%p] Beginning Decode", this));

    Mp4parseByteData* primaryItem = mParser->GetPrimaryItem();
    if (!primaryItem) {
      return AsVariant(NonDecoderResult::NoPrimaryItem);
    }

    mPicture.emplace();
    Dav1dResult r = GetPicture(primaryItem, mPicture.ptr(), aIsMetadataDecode);
    if (r != 0) {
      mPicture.reset();
      return AsVariant(r);
    }

    Parser::AlphaByteData* alphaData = mParser->GetAlphaData();
    if (alphaData) {
      mAlphaPlane.emplace();
      Dav1dResult r = GetPicture(&(alphaData->alphaItem), mAlphaPlane.ptr(),
                                 aIsMetadataDecode);
      if (r != 0) {
        mAlphaPlane.reset();
        return AsVariant(r);
      }
    }

    mDecodedData.emplace(Dav1dPictureToYCbCrAData(
        mPicture.ptr(), mAlphaPlane.ptrOr(nullptr),
        alphaData ? alphaData->premultipliedAlpha : false));
    return AsVariant(r);
  }

 protected:
  DecodeResult Init() override {
    MOZ_ASSERT(!mContext);

    if (!InitParser()) {
      return AsVariant(NonDecoderResult::ParseError);
    }

    Dav1dSettings settings;
    dav1d_default_settings(&settings);
    settings.all_layers = 0;
    // TODO: tune settings a la DAV1DDecoder for AV1

    return AsVariant(dav1d_open(&mContext, &settings));
  }

 private:
  explicit Dav1dDecoder(Mp4parseIo* aIo) : ImageDecoder(aIo) {
    MOZ_LOG(sAVIFLog, LogLevel::Verbose, ("Create Dav1dDecoder=%p", this));
  }

  Dav1dResult GetPicture(Mp4parseByteData* aBytes, Dav1dPicture* aPicture,
                         bool aIsMetadataDecode) {
    MOZ_ASSERT(mContext);
    MOZ_ASSERT(aBytes);
    MOZ_ASSERT(aPicture);

    Dav1dData dav1dData;
    Dav1dResult r = dav1d_data_wrap(&dav1dData, aBytes->data, aBytes->length,
                                    Dav1dFreeCallback_s, nullptr);

    MOZ_LOG(sAVIFLog, r == 0 ? LogLevel::Verbose : LogLevel::Error,
            ("[this=%p] dav1d_data_wrap(%p, %zu) -> %d", this, dav1dData.data,
             dav1dData.sz, r));

    if (r != 0) {
      return r;
    }

    r = dav1d_send_data(mContext, &dav1dData);

    MOZ_LOG(sAVIFLog, r == 0 ? LogLevel::Debug : LogLevel::Error,
            ("[this=%p] dav1d_send_data -> %d", this, r));

    if (r != 0) {
      return r;
    }

    r = dav1d_get_picture(mContext, aPicture);

    MOZ_LOG(sAVIFLog, r == 0 ? LogLevel::Debug : LogLevel::Error,
            ("[this=%p] dav1d_get_picture -> %d", this, r));

    // Discard the value outside of the range of uint32
    if (!aIsMetadataDecode && std::numeric_limits<int>::digits <= 31) {
      // De-negate POSIX error code returned from DAV1D. This must be sync with
      // DAV1D_ERR macro.
      uint32_t value = r < 0 ? -r : r;
      ScalarSet(ScalarID::AVIF_DAV1D_DECODE_ERROR, value);
    }

    return r;
  }

  // A dummy callback for dav1d_data_wrap
  static void Dav1dFreeCallback_s(const uint8_t* aBuf, void* aCookie) {
    // The buf is managed by the mParser inside Dav1dDecoder itself. Do nothing
    // here.
  }

  static YCbCrAData Dav1dPictureToYCbCrAData(Dav1dPicture* aPicture,
                                             Dav1dPicture* aAlphaPlane,
                                             bool aPremultipliedAlpha);

  Dav1dContext* mContext = nullptr;

  // The pictures are allocated once Decode() succeeds and will be deallocated
  // when Dav1dDecoder is destroyed
  Maybe<Dav1dPicture> mPicture;
  Maybe<Dav1dPicture> mAlphaPlane;
};

class AOMDecoder : ImageDecoder {
 public:
  ~AOMDecoder() {
    MOZ_LOG(sAVIFLog, LogLevel::Verbose, ("Destroy AOMDecoder=%p", this));

    if (mContext.isSome()) {
      aom_codec_err_t r = aom_codec_destroy(mContext.ptr());
      MOZ_LOG(sAVIFLog, LogLevel::Debug,
              ("[this=%p] aom_codec_destroy -> %d", this, r));
    }
  }

  static DecodeResult Create(Mp4parseIo* aIo,
                             UniquePtr<ImageDecoder>& aDecoder) {
    UniquePtr<AOMDecoder> d(new AOMDecoder(aIo));
    DecodeResult r = d->Init();
    if (IsDecodeSuccess(r)) {
      MOZ_ASSERT(d->mContext);
      aDecoder.reset(d.release());
    }
    return r;
  }

  DecodeResult Decode(bool aIsMetadataDecode) override {
    MOZ_ASSERT(mParser);
    MOZ_ASSERT(mContext.isSome());
    MOZ_ASSERT(mDecodedData.isNothing());

    Mp4parseByteData* primaryItem = mParser->GetPrimaryItem();
    if (!primaryItem) {
      return AsVariant(NonDecoderResult::NoPrimaryItem);
    }

    aom_codec_err_t r = aom_codec_decode(mContext.ptr(), primaryItem->data,
                                         primaryItem->length, nullptr);

    MOZ_LOG(sAVIFLog, r == AOM_CODEC_OK ? LogLevel::Verbose : LogLevel::Error,
            ("[this=%p] aom_codec_decode -> %d", this, r));

    if (!aIsMetadataDecode) {
      uint32_t value = static_cast<uint32_t>(r);
      ScalarSet(ScalarID::AVIF_AOM_DECODE_ERROR, value);
    }

    if (r != AOM_CODEC_OK) {
      AOMResult res = AsVariant(r);
      return AsVariant(res);
    }

    aom_codec_iter_t iter = nullptr;
    const aom_image_t* img = aom_codec_get_frame(mContext.ptr(), &iter);

    MOZ_LOG(sAVIFLog, img == nullptr ? LogLevel::Error : LogLevel::Verbose,
            ("[this=%p] aom_codec_get_frame -> %p", this, img));

    if (img == nullptr) {
      AOMResult res = AsVariant(NonAOMCodecError::NoFrame);
      return AsVariant(res);
    }

    const CheckedInt<int> decoded_width = img->d_w;
    const CheckedInt<int> decoded_height = img->d_h;

    if (!decoded_height.isValid() || !decoded_width.isValid()) {
      MOZ_LOG(sAVIFLog, LogLevel::Debug,
              ("[this=%p] image dimensions can't be stored in int: d_w: %u, "
               "d_h: %u",
               this, img->d_w, img->d_h));
      AOMResult res = AsVariant(NonAOMCodecError::SizeOverflow);
      return AsVariant(res);
    }

    MOZ_ASSERT(img->stride[AOM_PLANE_Y] == img->stride[AOM_PLANE_ALPHA]);
    MOZ_ASSERT(img->stride[AOM_PLANE_Y] >=
               aom_img_plane_width(img, AOM_PLANE_Y));
    MOZ_ASSERT(img->stride[AOM_PLANE_U] == img->stride[AOM_PLANE_V]);
    MOZ_ASSERT(img->stride[AOM_PLANE_U] >=
               aom_img_plane_width(img, AOM_PLANE_U));
    MOZ_ASSERT(img->stride[AOM_PLANE_V] >=
               aom_img_plane_width(img, AOM_PLANE_V));
    MOZ_ASSERT(aom_img_plane_width(img, AOM_PLANE_U) ==
               aom_img_plane_width(img, AOM_PLANE_V));
    MOZ_ASSERT(aom_img_plane_height(img, AOM_PLANE_U) ==
               aom_img_plane_height(img, AOM_PLANE_V));

    mDecodedData.emplace();
    mDecodedData->mYChannel = img->planes[AOM_PLANE_Y];
    mDecodedData->mYStride = img->stride[AOM_PLANE_Y];
    mDecodedData->mYSize = gfx::IntSize(aom_img_plane_width(img, AOM_PLANE_Y),
                                        aom_img_plane_height(img, AOM_PLANE_Y));
    mDecodedData->mYSkip =
        img->stride[AOM_PLANE_Y] - aom_img_plane_width(img, AOM_PLANE_Y);
    mDecodedData->mCbChannel = img->planes[AOM_PLANE_U];
    mDecodedData->mCrChannel = img->planes[AOM_PLANE_V];
    mDecodedData->mCbCrStride = img->stride[AOM_PLANE_U];
    mDecodedData->mCbCrSize =
        gfx::IntSize(aom_img_plane_width(img, AOM_PLANE_U),
                     aom_img_plane_height(img, AOM_PLANE_U));
    mDecodedData->mCbSkip =
        img->stride[AOM_PLANE_U] - aom_img_plane_width(img, AOM_PLANE_U);
    mDecodedData->mCrSkip =
        img->stride[AOM_PLANE_V] - aom_img_plane_width(img, AOM_PLANE_V);
    mDecodedData->mPicX = 0;
    mDecodedData->mPicY = 0;
    mDecodedData->mPicSize =
        gfx::IntSize(decoded_width.value(), decoded_height.value());
    mDecodedData->mStereoMode = StereoMode::MONO;
    mDecodedData->mColorDepth = ColorDepthForBitDepth(img->bit_depth);

    switch (img->mc) {
      case AOM_CICP_MC_BT_601:
        mDecodedData->mYUVColorSpace = gfx::YUVColorSpace::BT601;
        break;
      case AOM_CICP_MC_BT_709:
        mDecodedData->mYUVColorSpace = gfx::YUVColorSpace::BT709;
        break;
      case AOM_CICP_MC_BT_2020_NCL:
        mDecodedData->mYUVColorSpace = gfx::YUVColorSpace::BT2020;
        break;
      case AOM_CICP_MC_BT_2020_CL:
        mDecodedData->mYUVColorSpace = gfx::YUVColorSpace::BT2020;
        break;
      case AOM_CICP_MC_IDENTITY:
        mDecodedData->mYUVColorSpace = gfx::YUVColorSpace::Identity;
        break;
      case AOM_CICP_MC_CHROMAT_NCL:
      case AOM_CICP_MC_CHROMAT_CL:
      case AOM_CICP_MC_UNSPECIFIED:  // MIAF specific
        switch (img->cp) {
          case AOM_CICP_CP_BT_601:
            mDecodedData->mYUVColorSpace = gfx::YUVColorSpace::BT601;
            break;
          case AOM_CICP_CP_BT_709:
            mDecodedData->mYUVColorSpace = gfx::YUVColorSpace::BT709;
            break;
          case AOM_CICP_CP_BT_2020:
            mDecodedData->mYUVColorSpace = gfx::YUVColorSpace::BT2020;
            break;
          default:
            mDecodedData->mYUVColorSpace = gfx::YUVColorSpace::UNKNOWN;
            break;
        }
        break;
      default:
        MOZ_LOG(sAVIFLog, LogLevel::Debug,
                ("[this=%p] unsupported aom_matrix_coefficients value: %u",
                 this, img->mc));
        mDecodedData->mYUVColorSpace = gfx::YUVColorSpace::UNKNOWN;
    }

    if (mDecodedData->mYUVColorSpace == gfx::YUVColorSpace::UNKNOWN) {
      // MIAF specific: UNKNOWN color space should be treated as BT601
      mDecodedData->mYUVColorSpace = gfx::YUVColorSpace::BT601;
    }

    switch (img->range) {
      case AOM_CR_STUDIO_RANGE:
        mDecodedData->mColorRange = gfx::ColorRange::LIMITED;
        break;
      case AOM_CR_FULL_RANGE:
        mDecodedData->mColorRange = gfx::ColorRange::FULL;
        break;
      default:
        MOZ_ASSERT_UNREACHABLE("unknown color range");
    }

    AOMResult res = AsVariant(r);
    return AsVariant(res);
  }

 protected:
  DecodeResult Init() override {
    MOZ_ASSERT(mContext.isNothing());

    if (!InitParser()) {
      return AsVariant(NonDecoderResult::ParseError);
    }

    aom_codec_iface_t* iface = aom_codec_av1_dx();
    mContext.emplace();
    aom_codec_err_t r = aom_codec_dec_init(
        mContext.ptr(), iface, /* cfg = */ nullptr, /* flags = */ 0);

    MOZ_LOG(sAVIFLog, r == AOM_CODEC_OK ? LogLevel::Verbose : LogLevel::Error,
            ("[this=%p] aom_codec_dec_init -> %d, name = %s", this, r,
             mContext->name));

    if (r != AOM_CODEC_OK) {
      mContext.reset();
    }

    AOMResult res = AsVariant(r);
    return AsVariant(res);
  }

 private:
  explicit AOMDecoder(Mp4parseIo* aIo) : ImageDecoder(aIo) {
    MOZ_LOG(sAVIFLog, LogLevel::Verbose, ("Create AOMDecoder=%p", this));
  }

  Maybe<aom_codec_ctx_t> mContext;
};

class ImageDecoderderFactory {
 public:
  // The aDecoder will be a decoder that is initialized successfully if result
  // is good. Otherwise aDecoder stays the same.
  static ImageDecoder::DecodeResult Create(bool aDav1d, Mp4parseIo* aIo,
                                           UniquePtr<ImageDecoder>& aDecoder) {
    return aDav1d ? Dav1dDecoder::Create(aIo, aDecoder)
                  : AOMDecoder::Create(aIo, aDecoder);
  }
};

/* static */
YCbCrAData Dav1dDecoder::Dav1dPictureToYCbCrAData(Dav1dPicture* aPicture,
                                                  Dav1dPicture* aAlphaPlane,
                                                  bool aPremultipliedAlpha) {
  MOZ_ASSERT(aPicture);

  static_assert(std::is_same<int, decltype(aPicture->p.w)>::value);
  static_assert(std::is_same<int, decltype(aPicture->p.h)>::value);

  YCbCrAData data;

  data.mYChannel = static_cast<uint8_t*>(aPicture->data[0]);
  data.mYStride = aPicture->stride[0];
  data.mYSize = gfx::IntSize(aPicture->p.w, aPicture->p.h);
  data.mYSkip = aPicture->stride[0] - aPicture->p.w;
  data.mCbChannel = static_cast<uint8_t*>(aPicture->data[1]);
  data.mCrChannel = static_cast<uint8_t*>(aPicture->data[2]);
  data.mCbCrStride = aPicture->stride[1];

  switch (aPicture->p.layout) {
    case DAV1D_PIXEL_LAYOUT_I400:  // Monochrome, so no Cb or Cr channels
      data.mCbCrSize = gfx::IntSize(0, 0);
      break;
    case DAV1D_PIXEL_LAYOUT_I420:
      data.mCbCrSize =
          gfx::IntSize((aPicture->p.w + 1) / 2, (aPicture->p.h + 1) / 2);
      break;
    case DAV1D_PIXEL_LAYOUT_I422:
      data.mCbCrSize = gfx::IntSize((aPicture->p.w + 1) / 2, aPicture->p.h);
      break;
    case DAV1D_PIXEL_LAYOUT_I444:
      data.mCbCrSize = gfx::IntSize(aPicture->p.w, aPicture->p.h);
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("Unknown pixel layout");
  }

  data.mCbSkip = aPicture->stride[1] - aPicture->p.w;
  data.mCrSkip = aPicture->stride[1] - aPicture->p.w;
  data.mPicX = 0;
  data.mPicY = 0;
  data.mPicSize = data.mYSize;
  data.mStereoMode = StereoMode::MONO;
  data.mColorDepth = ColorDepthForBitDepth(aPicture->p.bpc);

  switch (aPicture->seq_hdr->mtrx) {
    case DAV1D_MC_BT601:
      data.mYUVColorSpace = gfx::YUVColorSpace::BT601;
      break;
    case DAV1D_MC_BT709:
      data.mYUVColorSpace = gfx::YUVColorSpace::BT709;
      break;
    case DAV1D_MC_BT2020_NCL:
      data.mYUVColorSpace = gfx::YUVColorSpace::BT2020;
      break;
    case DAV1D_MC_BT2020_CL:
      data.mYUVColorSpace = gfx::YUVColorSpace::BT2020;
      break;
    case DAV1D_MC_IDENTITY:
      data.mYUVColorSpace = gfx::YUVColorSpace::Identity;
      break;
    case DAV1D_MC_CHROMAT_NCL:
    case DAV1D_MC_CHROMAT_CL:
    case DAV1D_MC_UNKNOWN:  // MIAF specific
      switch (aPicture->seq_hdr->pri) {
        case DAV1D_COLOR_PRI_BT601:
          data.mYUVColorSpace = gfx::YUVColorSpace::BT601;
          break;
        case DAV1D_COLOR_PRI_BT709:
          data.mYUVColorSpace = gfx::YUVColorSpace::BT709;
          break;
        case DAV1D_COLOR_PRI_BT2020:
          data.mYUVColorSpace = gfx::YUVColorSpace::BT2020;
          break;
        default:
          data.mYUVColorSpace = gfx::YUVColorSpace::UNKNOWN;
          break;
      }
      break;
    default:
      MOZ_LOG(sAVIFLog, LogLevel::Debug,
              ("unsupported color matrix value: %u", aPicture->seq_hdr->mtrx));
      data.mYUVColorSpace = gfx::YUVColorSpace::UNKNOWN;
  }
  if (data.mYUVColorSpace == gfx::YUVColorSpace::UNKNOWN) {
    // MIAF specific: UNKNOWN color space should be treated as BT601
    data.mYUVColorSpace = gfx::YUVColorSpace::BT601;
  }

  data.mColorRange = aPicture->seq_hdr->color_range ? gfx::ColorRange::FULL
                                                    : gfx::ColorRange::LIMITED;

  if (aAlphaPlane) {
    data.mAlphaChannel = static_cast<uint8_t*>(aAlphaPlane->data[0]);
    data.mAlphaStride = aAlphaPlane->stride[0];
    data.mAlphaPicSize = gfx::IntSize(aAlphaPlane->p.w, aAlphaPlane->p.h);
    data.mAlphaColorDepth = ColorDepthForBitDepth(aAlphaPlane->p.bpc);
    data.mAlphaColorRange = aAlphaPlane->seq_hdr->color_range
                                ? gfx::ColorRange::FULL
                                : gfx::ColorRange::LIMITED;
    data.mPremultipliedAlpha = aPremultipliedAlpha;
  }

  return data;
}

// Wrapper to allow rust to call our read adaptor.
intptr_t nsAVIFDecoder::ReadSource(uint8_t* aDestBuf, uintptr_t aDestBufSize,
                                   void* aUserData) {
  MOZ_ASSERT(aDestBuf);
  MOZ_ASSERT(aUserData);

  MOZ_LOG(sAVIFLog, LogLevel::Verbose,
          ("AVIF ReadSource, aDestBufSize: %zu", aDestBufSize));

  auto* decoder = reinterpret_cast<nsAVIFDecoder*>(aUserData);

  MOZ_ASSERT(decoder->mReadCursor);

  size_t bufferLength = decoder->mBufferedData.end() - decoder->mReadCursor;
  size_t n_bytes = std::min(aDestBufSize, bufferLength);

  MOZ_LOG(
      sAVIFLog, LogLevel::Verbose,
      ("AVIF ReadSource, %zu bytes ready, copying %zu", bufferLength, n_bytes));

  memcpy(aDestBuf, decoder->mReadCursor, n_bytes);
  decoder->mReadCursor += n_bytes;

  return n_bytes;
}

nsAVIFDecoder::nsAVIFDecoder(RasterImage* aImage) : Decoder(aImage) {
  MOZ_LOG(sAVIFLog, LogLevel::Debug,
          ("[this=%p] nsAVIFDecoder::nsAVIFDecoder", this));
}

nsAVIFDecoder::~nsAVIFDecoder() {
  MOZ_LOG(sAVIFLog, LogLevel::Debug,
          ("[this=%p] nsAVIFDecoder::~nsAVIFDecoder", this));
}

LexerResult nsAVIFDecoder::DoDecode(SourceBufferIterator& aIterator,
                                    IResumable* aOnResume) {
  DecodeResult result = Decode(aIterator, aOnResume);

  RecordDecodeResultTelemetry(result);

  if (result.is<NonDecoderResult>()) {
    NonDecoderResult r = result.as<NonDecoderResult>();
    if (r == NonDecoderResult::NeedMoreData) {
      return LexerResult(Yield::NEED_MORE_DATA);
    }
    return r == NonDecoderResult::MetadataOk
               ? LexerResult(TerminalState::SUCCESS)
               : LexerResult(TerminalState::FAILURE);
  }

  MOZ_ASSERT(result.is<Dav1dResult>() || result.is<AOMResult>());
  return LexerResult(IsDecodeSuccess(result) ? TerminalState::SUCCESS
                                             : TerminalState::FAILURE);
}

nsAVIFDecoder::DecodeResult nsAVIFDecoder::Decode(
    SourceBufferIterator& aIterator, IResumable* aOnResume) {
  MOZ_LOG(sAVIFLog, LogLevel::Debug,
          ("[this=%p] nsAVIFDecoder::DoDecode", this));

  // Since the SourceBufferIterator doesn't guarantee a contiguous buffer,
  // but the current mp4parse-rust implementation requires it, always buffer
  // locally. This keeps the code simpler at the cost of some performance, but
  // this implementation is only experimental, so we don't want to spend time
  // optimizing it prematurely.
  while (!mReadCursor) {
    SourceBufferIterator::State state =
        aIterator.AdvanceOrScheduleResume(SIZE_MAX, aOnResume);

    MOZ_LOG(sAVIFLog, LogLevel::Debug,
            ("[this=%p] After advance, iterator state is %d", this, state));

    switch (state) {
      case SourceBufferIterator::WAITING:
        return AsVariant(NonDecoderResult::NeedMoreData);

      case SourceBufferIterator::COMPLETE:
        mReadCursor = mBufferedData.begin();
        break;

      case SourceBufferIterator::READY: {  // copy new data to buffer
        MOZ_LOG(sAVIFLog, LogLevel::Debug,
                ("[this=%p] SourceBufferIterator ready, %zu bytes available",
                 this, aIterator.Length()));

        bool appendSuccess =
            mBufferedData.append(aIterator.Data(), aIterator.Length());

        if (!appendSuccess) {
          MOZ_LOG(sAVIFLog, LogLevel::Error,
                  ("[this=%p] Failed to append %zu bytes to buffer", this,
                   aIterator.Length()));
        }

        break;
      }

      default:
        MOZ_ASSERT_UNREACHABLE("unexpected SourceBufferIterator state");
    }
  }

  Mp4parseIo io = {nsAVIFDecoder::ReadSource, this};
  UniquePtr<ImageDecoder> decoder;
  DecodeResult r = ImageDecoderderFactory::Create(
      StaticPrefs::image_avif_use_dav1d(), &io, decoder);

  MOZ_LOG(sAVIFLog, LogLevel::Debug,
          ("[this=%p] Create %sDecoder %ssuccessfully", this,
           StaticPrefs::image_avif_use_dav1d() ? "Dav1d" : "AOM",
           IsDecodeSuccess(r) ? "" : "un"));

  if (!IsDecodeSuccess(r)) {
    return r;
  }

  r = decoder->Decode(IsMetadataDecode());
  MOZ_LOG(sAVIFLog, LogLevel::Debug,
          ("[this=%p] Decoder%s->Decode() %s", this,
           StaticPrefs::image_avif_use_dav1d() ? "Dav1d" : "AOM",
           IsDecodeSuccess(r) ? "succeeds" : "fails"));

  if (!IsDecodeSuccess(r)) {
    return r;
  }

  YCbCrAData& decodedData = decoder->GetDecodedData();

  PostSize(decodedData.mPicSize.width, decodedData.mPicSize.height);

  const bool hasAlpha = decodedData.hasAlpha();
  if (hasAlpha) {
    PostHasTransparency();
  }

  if (IsMetadataDecode()) {
    return AsVariant(NonDecoderResult::MetadataOk);
  }

  // These data must be recorded after metadata has been decoded
  // (IsMetadataDecode()=false) or else they would be double-counted.
  AccumulateCategorical(
      gColorSpaceLabel[static_cast<size_t>(decodedData.mYUVColorSpace)]);
  AccumulateCategorical(
      gColorDepthLabel[static_cast<size_t>(decodedData.mColorDepth)]);

  const IntSize intrinsicSize = Size();
  IntSize rgbSize = intrinsicSize;

  // Get suggested format and size. Note that GetYCbCrToRGBDestFormatAndSize
  // force format to be B8G8R8X8 if it's not.
  gfx::SurfaceFormat format = SurfaceFormat::OS_RGBX;
  gfx::GetYCbCrToRGBDestFormatAndSize(decodedData, format, rgbSize);
  if (hasAlpha) {
    format = SurfaceFormat::OS_RGBA;
  }

  const int bytesPerPixel = BytesPerPixel(format);

  const CheckedInt rgbStride = CheckedInt<int>(rgbSize.width) * bytesPerPixel;
  const CheckedInt rgbBufLength = rgbStride * rgbSize.height;

  if (!rgbStride.isValid() || !rgbBufLength.isValid()) {
    MOZ_LOG(sAVIFLog, LogLevel::Debug,
            ("[this=%p] overflow calculating rgbBufLength: rbgSize.width: %d, "
             "rgbSize.height: %d, "
             "bytesPerPixel: %u",
             this, rgbSize.width, rgbSize.height, bytesPerPixel));
    return AsVariant(NonDecoderResult::SizeOverflow);
  }

  UniquePtr<uint8_t[]> rgbBuf = MakeUnique<uint8_t[]>(rgbBufLength.value());
  const uint8_t* endOfRgbBuf = {rgbBuf.get() + rgbBufLength.value()};

  if (!rgbBuf) {
    MOZ_LOG(sAVIFLog, LogLevel::Debug,
            ("[this=%p] allocation of %u-byte rgbBuf failed", this,
             rgbBufLength.value()));
    return AsVariant(NonDecoderResult::OutOfMemory);
  }

  if (hasAlpha) {
    MOZ_ASSERT(decodedData.mAlphaStride == decodedData.mYStride);
    MOZ_ASSERT(decodedData.mAlphaPicSize == decodedData.mPicSize);
    MOZ_ASSERT(decodedData.mAlphaColorDepth == decodedData.mColorDepth);
    MOZ_ASSERT(bytesPerPixel == 4);

    MOZ_LOG(sAVIFLog, LogLevel::Debug,
            ("[this=%p] calling gfx::ConvertYCbCrAToARGB", this));
    gfx::ConvertYCbCrAToARGB(decodedData, format, rgbSize, rgbBuf.get(),
                             rgbStride.value());
  } else {
    MOZ_LOG(sAVIFLog, LogLevel::Debug,
            ("[this=%p] calling gfx::ConvertYCbCrToRGB", this));
    gfx::ConvertYCbCrToRGB(decodedData, format, rgbSize, rgbBuf.get(),
                           rgbStride.value());
  }

  SurfacePipeFlags pipeFlags = SurfacePipeFlags();
  if (hasAlpha && decodedData.mPremultipliedAlpha) {
    pipeFlags |= SurfacePipeFlags::PREMULTIPLY_ALPHA;
  }
  MOZ_LOG(sAVIFLog, LogLevel::Debug,
          ("[this=%p] calling SurfacePipeFactory::CreateSurfacePipe", this));
  Maybe<SurfacePipe> pipe = SurfacePipeFactory::CreateSurfacePipe(
      this, rgbSize, OutputSize(), FullFrame(), format, format, Nothing(),
      nullptr, pipeFlags);

  if (!pipe) {
    MOZ_LOG(sAVIFLog, LogLevel::Debug,
            ("[this=%p] could not initialize surface pipe", this));
    return AsVariant(NonDecoderResult::PipeInitError);
  }

  MOZ_LOG(sAVIFLog, LogLevel::Debug, ("[this=%p] writing to surface", this));
  WriteState writeBufferResult = WriteState::NEED_MORE_DATA;
  for (uint8_t* rowPtr = rgbBuf.get(); rowPtr < endOfRgbBuf;
       rowPtr += rgbStride.value()) {
    writeBufferResult = pipe->WriteBuffer(reinterpret_cast<uint32_t*>(rowPtr));

    Maybe<SurfaceInvalidRect> invalidRect = pipe->TakeInvalidRect();
    if (invalidRect) {
      PostInvalidation(invalidRect->mInputSpaceRect,
                       Some(invalidRect->mOutputSpaceRect));
    }

    if (writeBufferResult == WriteState::FAILURE) {
      MOZ_LOG(sAVIFLog, LogLevel::Debug,
              ("[this=%p] error writing rowPtr to surface pipe", this));

    } else if (writeBufferResult == WriteState::FINISHED) {
      MOZ_ASSERT(rowPtr + rgbStride.value() == endOfRgbBuf);
    }
  }

  MOZ_LOG(sAVIFLog, LogLevel::Debug,
          ("[this=%p] writing to surface complete", this));

  if (writeBufferResult == WriteState::FINISHED) {
    PostFrameStop(hasAlpha ? Opacity::SOME_TRANSPARENCY
                           : Opacity::FULLY_OPAQUE);
    PostDecodeDone();
    return r;
  }

  return AsVariant(NonDecoderResult::WriteBufferError);
}

/* static */
bool nsAVIFDecoder::IsDecodeSuccess(const DecodeResult& aResult) {
  if (aResult.is<Dav1dResult>() || aResult.is<AOMResult>()) {
    return aResult == DecodeResult(Dav1dResult(0)) ||
           aResult == DecodeResult(AOMResult(AOM_CODEC_OK));
  }
  return false;
}

void nsAVIFDecoder::RecordDecodeResultTelemetry(
    const nsAVIFDecoder::DecodeResult& aResult) {
  if (aResult.is<NonDecoderResult>()) {
    switch (aResult.as<NonDecoderResult>()) {
      case NonDecoderResult::NeedMoreData:
        break;
      case NonDecoderResult::MetadataOk:
        break;
      case NonDecoderResult::ParseError:
        AccumulateCategorical(LABELS_AVIF_DECODE_RESULT::parse_error);
        break;
      case NonDecoderResult::NoPrimaryItem:
        AccumulateCategorical(LABELS_AVIF_DECODE_RESULT::no_primary_item);
        break;
      case NonDecoderResult::SizeOverflow:
        AccumulateCategorical(LABELS_AVIF_DECODE_RESULT::size_overflow);
        break;
      case NonDecoderResult::OutOfMemory:
        AccumulateCategorical(LABELS_AVIF_DECODE_RESULT::out_of_memory);
        break;
      case NonDecoderResult::PipeInitError:
        AccumulateCategorical(LABELS_AVIF_DECODE_RESULT::pipe_init_error);
        break;
      case NonDecoderResult::WriteBufferError:
        AccumulateCategorical(LABELS_AVIF_DECODE_RESULT::write_buffer_error);
        break;
      default:
        MOZ_ASSERT_UNREACHABLE("unknown result");
        break;
    }
  } else {
    MOZ_ASSERT(aResult.is<Dav1dResult>() || aResult.is<AOMResult>());
    AccumulateCategorical(aResult.is<Dav1dResult>() ? LABELS_AVIF_DECODER::dav1d
                                                    : LABELS_AVIF_DECODER::aom);
    AccumulateCategorical(IsDecodeSuccess(aResult)
                              ? LABELS_AVIF_DECODE_RESULT::success
                              : LABELS_AVIF_DECODE_RESULT::decode_error);
  }
}

}  // namespace image
}  // namespace mozilla
