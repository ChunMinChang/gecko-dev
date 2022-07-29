/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/VideoFrame.h"
#include "mozilla/dom/VideoFrameBinding.h"

#include <math.h>
#include <limits>

#include "ImageContainer.h"
#include "VideoColorSpace.h"
#include "mozilla/Maybe.h"
#include "mozilla/Result.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/dom/DOMRect.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/UnionTypes.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/Swizzle.h"

namespace mozilla::dom {

// Only needed for refcounted objects.
NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(VideoFrame, mParent)
NS_IMPL_CYCLE_COLLECTING_ADDREF(VideoFrame)
NS_IMPL_CYCLE_COLLECTING_RELEASE(VideoFrame)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(VideoFrame)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

/*
 * The below are helpers to operate ArrayBuffer or ArrayBufferView.
 */

static void InitSharedArrayBuffer(
    const MaybeSharedArrayBufferViewOrMaybeSharedArrayBuffer& aBuffer) {
  if (aBuffer.IsArrayBufferView()) {
    aBuffer.GetAsArrayBufferView().ComputeState();
    return;
  }
  MOZ_ASSERT(aBuffer.IsArrayBuffer());
  aBuffer.GetAsArrayBuffer().ComputeState();
}

template <class T>
static CheckedInt<size_t> ByteLength(const T& aBuffer) {
  return CheckedInt<size_t>(sizeof(typename T::element_type)) *
         aBuffer.Length();
}

static CheckedInt<size_t> ByteLength(
    const MaybeSharedArrayBufferViewOrMaybeSharedArrayBuffer& aBuffer) {
  if (aBuffer.IsArrayBufferView()) {
    return ByteLength(aBuffer.GetAsArrayBufferView());
  }

  MOZ_ASSERT(aBuffer.IsArrayBuffer());
  return ByteLength(aBuffer.GetAsArrayBuffer());
}

static uint8_t* GetSharedArrayBufferData(
    const MaybeSharedArrayBufferViewOrMaybeSharedArrayBuffer& aBuffer) {
  if (aBuffer.IsArrayBufferView()) {
    return aBuffer.GetAsArrayBufferView().Data();
  }
  MOZ_ASSERT(aBuffer.IsArrayBuffer());
  return aBuffer.GetAsArrayBuffer().Data();
}

/*
 * The following are utilities to convert the VideoColorSpace's member values to
 * gfx's values
 */

static gfx::YUVColorSpace ToColorSpace(VideoMatrixCoefficients aMatrix) {
  switch (aMatrix) {
    case VideoMatrixCoefficients::Rgb:
      return gfx::YUVColorSpace::Identity;
    case VideoMatrixCoefficients::Bt709:
      return gfx::YUVColorSpace::BT709;
    case VideoMatrixCoefficients::Bt470bg:
    case VideoMatrixCoefficients::Smpte170m:
      return gfx::YUVColorSpace::BT601;
    default:
      MOZ_ASSERT_UNREACHABLE("unsupported VideoMatrixCoefficients");
  }
  return gfx::YUVColorSpace::Default;
}

static gfx::TransferFunction ToTransferFunction(
    VideoTransferCharacteristics aTransfer) {
  switch (aTransfer) {
    case VideoTransferCharacteristics::Bt709:
    case VideoTransferCharacteristics::Smpte170m:
      return gfx::TransferFunction::BT709;
    case VideoTransferCharacteristics::Iec61966_2_1:
      return gfx::TransferFunction::SRGB;
    default:
      MOZ_ASSERT_UNREACHABLE("unsupported VideoTransferCharacteristics");
  }
  return gfx::TransferFunction::Default;
}

/*
 * The following are helpers to read the image data from the given buffer and
 * the format. The data layout is illustrated in the comments for
 * `VideoFrame::Format` below.
 */

class I420BufferReader {
 public:
  I420BufferReader(const uint8_t* aBuffer, int32_t aWidth, int32_t aHeight)
      : mWidth(aWidth),
        mHeight(aHeight),
        mStrideY(aWidth),
        mStrideU((aWidth + 1) / 2),
        mStrideV((aWidth + 1) / 2),
        mBuffer(aBuffer) {}

  const uint8_t* DataY() const { return mBuffer; }
  const uint8_t* DataU() const {
    CheckedInt<size_t> offset = CheckedInt<size_t>(mStrideY) * mHeight;
    return DataY() + offset.value();
  }
  const uint8_t* DataV() const {
    CheckedInt<size_t> offset =
        CheckedInt<size_t>(mStrideU) * ((mHeight + 1) / 2);
    return DataU() + offset.value();
  }

  const int32_t mWidth;
  const int32_t mHeight;
  const int32_t mStrideY;
  const int32_t mStrideU;
  const int32_t mStrideV;

 protected:
  const uint8_t* mBuffer;
};

/*
 * The followings are helpers defined in
 * https://w3c.github.io/webcodecs/#videoframe-algorithms
 */

// A sub-helper to convert from DOMRectInit to gfx::IntRect
static Maybe<nsAutoCString> ToIntRect(Maybe<gfx::IntRect>& aRect,
                                      const DOMRectInit& aRectInit) {
  auto cleanup = MakeScopeExit([&]() { aRect.reset(); });

  auto EQ = [](const double& a, const double& b) {
    constexpr double e = std::numeric_limits<double>::epsilon();
    return std::fabs(a - b) <= e;
  };
  auto GT = [&](const double& a, const double& b) {
    return !EQ(a, b) && a > b;
  };

  // Make sure the double can be casted to int, before checking the valid range.
  // The double's infinity value is larger than integer's max value so it will
  // be filtered out here.
  constexpr double MAX =
      static_cast<double>(std::numeric_limits<decltype(aRect->X())>::max());
  constexpr double MIN =
      static_cast<double>(std::numeric_limits<decltype(aRect->X())>::min());
  if (GT(aRectInit.mX, MAX) || GT(MIN, aRectInit.mX)) {
    return Some(nsAutoCString("x is out of the valid range"));
  }
  if (GT(aRectInit.mY, MAX) || GT(MIN, aRectInit.mY)) {
    return Some(nsAutoCString("y is out of the valid range"));
  }
  if (GT(aRectInit.mWidth, MAX) || GT(MIN, aRectInit.mWidth)) {
    return Some(nsAutoCString("width is out of the valid range"));
  }
  if (GT(aRectInit.mHeight, MAX) || GT(MIN, aRectInit.mHeight)) {
    return Some(nsAutoCString("height is out of the valid range"));
  }

  // Check the valid range.
  aRect.emplace(gfx::IntRect(static_cast<int32_t>(aRectInit.mX),
                             static_cast<int32_t>(aRectInit.mY),
                             static_cast<int32_t>(aRectInit.mWidth),
                             static_cast<int32_t>(aRectInit.mHeight)));

  if (aRect->X() < 0) {
    return Some(nsAutoCString("x must be non-negative"));
  }
  if (aRect->Y() < 0) {
    return Some(nsAutoCString("y must be non-negative"));
  }
  if (aRect->Width() <= 0) {
    return Some(nsAutoCString("width must be positive"));
  }
  if (aRect->Height() <= 0) {
    return Some(nsAutoCString("height must be positive"));
  }

  cleanup.release();
  return Nothing();
}

// A sub-helper to make sure visible range is in the picture.
static Maybe<nsAutoCString> ValidateVisibility(const gfx::IntRect& aVisibleRect,
                                               const gfx::IntSize& aPicSize) {
  MOZ_ASSERT(aVisibleRect.X() >= 0);
  MOZ_ASSERT(aVisibleRect.Y() >= 0);
  MOZ_ASSERT(aVisibleRect.Width() > 0);
  MOZ_ASSERT(aVisibleRect.Height() > 0);

  CheckedUint32 w(aVisibleRect.Width());
  w += aVisibleRect.X();
  if (w.value() > static_cast<uint32_t>(aPicSize.Width())) {
    return Some(nsAutoCString(
        "Sum of visible rectangle's x and width exceeds the picture's width"));
  }

  CheckedUint32 h(aVisibleRect.Height());
  h += aVisibleRect.Y();
  if (h.value() > static_cast<uint32_t>(aPicSize.Height())) {
    return Some(
        nsAutoCString("Sum of visible rectangle's y and height exceeds the "
                      "picture's height"));
  }

  return Nothing();
}

// https://w3c.github.io/webcodecs/#valid-videoframebufferinit
static Maybe<nsAutoCString> ValidateVideoFrameBufferInit(
    const VideoFrameBufferInit& aInit, gfx::IntSize& aCodedSize,
    Maybe<gfx::IntRect>& aVisibleRect, Maybe<gfx::IntSize>& aDisplaySize) {
  auto cleanup = MakeScopeExit([&]() {
    aCodedSize = {0, 0};
    aVisibleRect.reset();
  });

  if (aInit.mCodedWidth == 0) {
    return Some(nsAutoCString("codedWidth must be positive"));
  }

  if (aInit.mCodedHeight == 0) {
    return Some(nsAutoCString("codedHeight must be positive"));
  }
  // Implicit conversions from uint32_t to int32_t.
  aCodedSize = gfx::IntSize(aInit.mCodedWidth, aInit.mCodedHeight);

  if (aInit.mVisibleRect.WasPassed()) {
    if (Maybe<nsAutoCString> error =
            ToIntRect(aVisibleRect, aInit.mVisibleRect.Value())) {
      error->Insert("visibleRect's ", 0);
      return error;
    }
    // TODO: Spec here is wrong so we do differently:
    // https://github.com/w3c/webcodecs/issues/515
    // This comment should be removed once the issue is resolved.
    if (Maybe<nsAutoCString> error =
            ValidateVisibility(aVisibleRect.ref(), aCodedSize)) {
      return error;
    }
  }

  if (aInit.mDisplayWidth.WasPassed() != aInit.mDisplayHeight.WasPassed()) {
    return Some(nsAutoCString(
        "displayWidth and displayHeight cannot be set without the other"));
  }
  if (aInit.mDisplayWidth.WasPassed() && aInit.mDisplayHeight.WasPassed()) {
    if (aInit.mDisplayWidth.Value() == 0) {
      return Some(nsAutoCString("displayWidth must be positive"));
    }
    if (aInit.mDisplayHeight.Value() == 0) {
      return Some(nsAutoCString("displayHeight must be positive"));
    }
    // Implicit conversions from uint32_t to int32_t.
    aDisplaySize = Some(gfx::IntSize{aInit.mDisplayWidth.Value(),
                                     aInit.mDisplayHeight.Value()});
  }

  cleanup.release();
  return Nothing();
}

// https://w3c.github.io/webcodecs/#videoframe-verify-rect-offset-alignment
static Maybe<nsAutoCString> VerifyRectOffsetAlignment(
    const VideoFrame::Format& aFormat, const gfx::IntRect& aRect) {
  for (const VideoFrame::Format::Plane& p : aFormat.Planes()) {
    const gfx::IntSize sample = aFormat.SampleSize(p);
    if (aRect.X() % sample.Width() != 0) {
      return Some(
          nsAutoCString("Mismatch between format and given left offset"));
    }

    if (aRect.Y() % sample.Height() != 0) {
      return Some(
          nsAutoCString("Mismatch between format and given top offset"));
    }
  }
  return Nothing();
}

// https://w3c.github.io/webcodecs/#videoframe-parse-visible-rect
static Maybe<nsAutoCString> ParseVisibleRect(
    gfx::IntRect& aRect, const Maybe<gfx::IntRect>& aOverrideRect,
    const gfx::IntSize& aCodedSize, const VideoFrame::Format& aFormat) {
  if (aOverrideRect) {
    if (aOverrideRect->Width() == 0) {
      return Some(nsAutoCString("Given rect's width must be nonzero"));
    }

    if (aOverrideRect->Height() == 0) {
      return Some(nsAutoCString("Given rect's height must be nonzero"));
    }

    if (Maybe<nsAutoCString> error =
            ValidateVisibility(aOverrideRect.ref(), aCodedSize)) {
      return error;
    }

    aRect = *aOverrideRect;
  }

  return VerifyRectOffsetAlignment(aFormat, aRect);
}

// https://w3c.github.io/webcodecs/#computed-plane-layout
struct ComputedPlaneLayout {
  // The offset from the beginning of the buffer in one plane.
  uint32_t mDestinationOffset = 0;
  // The stride of the image data in one plane.
  uint32_t mDestinationStride = 0;
  // Sample count of picture's top offset (a.k.a samples of y).
  uint32_t mSourceTop = 0;
  // Sample count of the picture's height.
  uint32_t mSourceHeight = 0;
  // Byte count of the picture's left offset (a.k.a bytes of x).
  uint32_t mSourceLeftBytes = 0;
  // Byte count of the picture's width.
  uint32_t mSourceWidthBytes = 0;
};

// https://w3c.github.io/webcodecs/#combined-buffer-layout
struct CombinedBufferLayout {
  CombinedBufferLayout() : mAllocationSize(0) {}
  CombinedBufferLayout(uint32_t aAllocationSize,
                       nsTArray<ComputedPlaneLayout>&& aLayout)
      : mAllocationSize(aAllocationSize),
        mComputedLayouts(std::move(aLayout)) {}
  uint32_t mAllocationSize = 0;
  nsTArray<ComputedPlaneLayout> mComputedLayouts;
};

// https://w3c.github.io/webcodecs/#videoframe-compute-layout-and-allocation-size
static Maybe<nsAutoCString> ComputeLayoutAndAllocationSize(
    CombinedBufferLayout& aLayout, const gfx::IntRect& aRect,
    const VideoFrame::Format& aFormat,
    const Sequence<PlaneLayout>* aPlaneLayouts) {
  nsTArray<VideoFrame::Format::Plane> planes = aFormat.Planes();

  if (aPlaneLayouts && aPlaneLayouts->Length() != planes.Length()) {
    return Some(nsAutoCString("Mismatch between format and layout"));
  }

  uint32_t minAllocationSize = 0;
  nsTArray<ComputedPlaneLayout> layouts;
  nsTArray<uint32_t> endOffsets;

  for (size_t i = 0; i < planes.Length(); ++i) {
    const VideoFrame::Format::Plane& p = planes[i];
    const gfx::IntSize sampleSize = aFormat.SampleSize(p);

    // TODO: Spec here is wrong so we do differently:
    // https://github.com/w3c/webcodecs/issues/511
    // This comment should be removed once the issue is resolved.
    ComputedPlaneLayout layout{
        .mDestinationOffset = 0,
        .mDestinationStride = 0,
        .mSourceTop = (CheckedUint32(aRect.Y()) / sampleSize.Height()).value(),
        .mSourceHeight =
            (CheckedUint32(aRect.Height()) / sampleSize.Height()).value(),
        .mSourceLeftBytes = (CheckedUint32(aRect.X()) / sampleSize.Width() *
                             aFormat.SampleBytes(p))
                                .value(),
        .mSourceWidthBytes = (CheckedUint32(aRect.Width()) /
                              sampleSize.Width() * aFormat.SampleBytes(p))
                                 .value()};
    if (aPlaneLayouts) {
      const PlaneLayout& planeLayout = aPlaneLayouts->ElementAt(i);
      if (planeLayout.mStride < layout.mSourceWidthBytes) {
        nsAutoCString msg("The stride in ");
        msg += aFormat.PlaneName(p);
        msg += " plane is too small";
        return Some(msg);
      }
      layout.mDestinationOffset = planeLayout.mOffset;
      layout.mDestinationStride = planeLayout.mStride;
    } else {
      layout.mDestinationOffset = minAllocationSize;
      layout.mDestinationStride = layout.mSourceWidthBytes;
    }

    const CheckedInt<uint32_t> planeSize =
        CheckedInt<uint32_t>(layout.mDestinationStride) * layout.mSourceHeight;
    if (!planeSize.isValid()) {
      return Some(nsAutoCString("Invalid layout with an over-sized plane"));
    }
    const CheckedInt<uint32_t> planeEnd = planeSize + layout.mDestinationOffset;
    if (!planeEnd.isValid()) {
      return Some(
          nsAutoCString("Invalid layout with the out-out-bound offset"));
    }
    endOffsets.AppendElement(planeEnd.value());

    minAllocationSize = std::max(minAllocationSize, planeEnd.value());

    for (size_t j = 0; j < i; ++j) {
      const ComputedPlaneLayout& earlier = layouts[j];
      // If the current data's end is smaller or equal to the previous one's
      // head, or if the previous data's end is smaller or equal to the current
      // one's head, then they do not overlap. Otherwise, they do.
      if (endOffsets[i] > earlier.mDestinationOffset &&
          endOffsets[j] > layout.mDestinationOffset) {
        return Some(nsAutoCString("Invalid layout with the overlapped planes"));
      }
    }
    layouts.AppendElement(layout);
  }

  aLayout = CombinedBufferLayout(minAllocationSize, std::move(layouts));
  return Nothing();
}

// https://w3c.github.io/webcodecs/#videoframe-verify-rect-size-alignment
static Maybe<nsAutoCString> VerifyRectSizeAlignment(
    const VideoFrame::Format& aFormat, const gfx::IntRect& aRect) {
  for (const VideoFrame::Format::Plane& p : aFormat.Planes()) {
    const gfx::IntSize sample = aFormat.SampleSize(p);
    if (aRect.Width() % sample.Width() != 0) {
      return Some(
          nsAutoCString("Mismatch between format and given rect's width"));
    }

    if (aRect.Height() % sample.Height() != 0) {
      return Some(
          nsAutoCString("Mismatch between format and given rect's height"));
    }
  }
  return Nothing();
}

// https://w3c.github.io/webcodecs/#videoframe-parse-videoframecopytooptions
static Maybe<nsAutoCString> ParseVideoFrameCopyToOptions(
    CombinedBufferLayout& aLayout, const VideoFrameCopyToOptions& aOptions,
    const gfx::IntRect& aVisibleRect, const gfx::IntSize& aCodedSize,
    const VideoFrame::Format& aFormat) {
  Maybe<gfx::IntRect> overrideRect;
  if (aOptions.mRect.WasPassed()) {
    // TODO: We handle some edge cases that spec misses:
    // https://github.com/w3c/webcodecs/issues/513
    // This comment should be removed once the issue is resolved.
    if (Maybe<nsAutoCString> error =
            ToIntRect(overrideRect, aOptions.mRect.Value())) {
      error->Insert("rect's ", 0);
      return error;
    }
    if (Maybe<nsAutoCString> error =
            VerifyRectSizeAlignment(aFormat, overrideRect.ref())) {
      return error;
    }
  }

  gfx::IntRect parsedRect = aVisibleRect;
  if (Maybe<nsAutoCString> error =
          ParseVisibleRect(parsedRect, overrideRect, aCodedSize, aFormat)) {
    return error;
  }

  const Sequence<PlaneLayout>* optLayout = nullptr;
  if (aOptions.mLayout.WasPassed()) {
    optLayout = &aOptions.mLayout.Value();
  }

  return ComputeLayoutAndAllocationSize(aLayout, parsedRect, aFormat,
                                        optLayout);
}

// https://w3c.github.io/webcodecs/#videoframe-pick-color-space
static void PickColorSpace(VideoColorSpaceInit& aColorSpace,
                           const VideoColorSpaceInit* aInitColorSpace,
                           const VideoPixelFormat& aFormat) {
  if (aInitColorSpace) {
    aColorSpace = *aInitColorSpace;
    // TODO: we MAY replace null members of aInitColorSpace with guessed values
    // so we can always use these in CreateYUVImageFromBuffer
    return;
  }

  switch (aFormat) {
    case VideoPixelFormat::I420:
    case VideoPixelFormat::I420A:
    case VideoPixelFormat::I422:
    case VideoPixelFormat::I444:
    case VideoPixelFormat::NV12:
      // https://w3c.github.io/webcodecs/#rec709-color-space
      aColorSpace.mFullRange.Construct(false);
      aColorSpace.mMatrix.Construct(VideoMatrixCoefficients::Bt709);
      aColorSpace.mPrimaries.Construct(VideoColorPrimaries::Bt709);
      aColorSpace.mTransfer.Construct(VideoTransferCharacteristics::Bt709);
      break;
    case VideoPixelFormat::RGBA:
    case VideoPixelFormat::RGBX:
    case VideoPixelFormat::BGRA:
    case VideoPixelFormat::BGRX:
      // https://w3c.github.io/webcodecs/#srgb-color-space
      aColorSpace.mFullRange.Construct(true);
      aColorSpace.mMatrix.Construct(VideoMatrixCoefficients::Rgb);
      aColorSpace.mPrimaries.Construct(VideoColorPrimaries::Bt709);
      aColorSpace.mTransfer.Construct(
          VideoTransferCharacteristics::Iec61966_2_1);
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("unsupported format");
  }
}

/*
 * The followings are helpers to create a VideoFrame from a given buffer
 */

static already_AddRefed<gfx::DataSourceSurface> AllocateBGRASurface(
    gfx::DataSourceSurface* aSurface) {
  MOZ_ASSERT(aSurface);

  // Memory allocation relies on CreateDataSourceSurfaceWithStride so we still
  // need to do this even if the format is SurfaceFormat::BGR{A, X}.

  gfx::DataSourceSurface::ScopedMap surfaceMap(aSurface,
                                               gfx::DataSourceSurface::READ);
  if (NS_WARN_IF(!surfaceMap.IsMapped())) {
    return nullptr;
  }

  RefPtr<gfx::DataSourceSurface> bgraSurface =
      gfx::Factory::CreateDataSourceSurfaceWithStride(
          aSurface->GetSize(), gfx::SurfaceFormat::B8G8R8A8,
          surfaceMap.GetStride());
  if (NS_WARN_IF(!bgraSurface)) {
    return nullptr;
  }

  gfx::DataSourceSurface::ScopedMap bgraMap(bgraSurface,
                                            gfx::DataSourceSurface::WRITE);
  if (NS_WARN_IF(!bgraMap.IsMapped())) {
    return nullptr;
  }

  gfx::SwizzleData(surfaceMap.GetData(), surfaceMap.GetStride(),
                   aSurface->GetFormat(), bgraMap.GetData(),
                   bgraMap.GetStride(), bgraSurface->GetFormat(),
                   bgraSurface->GetSize());

  return bgraSurface.forget();
}

static already_AddRefed<layers::Image> CreateImageFromRawData(
    const gfx::IntSize& aSize, int32_t aStride, gfx::SurfaceFormat aFormat,
    uint8_t* aBuffer) {
  MOZ_ASSERT(!aSize.IsEmpty());
  MOZ_ASSERT(aBuffer);

  // Wrap the source buffer into a DataSourceSurface.
  RefPtr<gfx::DataSourceSurface> surface =
      gfx::Factory::CreateWrappingDataSourceSurface(aBuffer, aStride, aSize,
                                                    aFormat);
  if (NS_WARN_IF(!surface)) {
    return nullptr;
  }

  // Gecko favors BGRA so we convert surface into BGRA format first.
  RefPtr<gfx::DataSourceSurface> bgraSurface = AllocateBGRASurface(surface);
  if (NS_WARN_IF(!bgraSurface)) {
    return nullptr;
  }

  RefPtr<layers::SourceSurfaceImage> image =
      new layers::SourceSurfaceImage(bgraSurface.get());
  return image.forget();
}

static already_AddRefed<layers::Image> CreateRGBAImageFromBuffer(
    const VideoFrame::Format& aFormat, const gfx::IntSize& aSize,
    uint8_t* aBuffer) {
  const gfx::SurfaceFormat format = aFormat.ToSurfaceFormat();
  MOZ_ASSERT(format == gfx::SurfaceFormat::R8G8B8A8 ||
             format == gfx::SurfaceFormat::R8G8B8X8 ||
             format == gfx::SurfaceFormat::B8G8R8A8 ||
             format == gfx::SurfaceFormat::B8G8R8X8);
  // TODO: Use aFormat.SampleBytes() instead?
  const int bytesPerPixel = BytesPerPixel(format);
  return CreateImageFromRawData(aSize, aSize.Width() * bytesPerPixel, format,
                                aBuffer);
}

static already_AddRefed<layers::Image> CreateYUVImageFromI420Buffer(
    const VideoColorSpaceInit& aColorSpace, const gfx::IntSize& aSize,
    uint8_t* aBuffer) {
  I420BufferReader reader(aBuffer, aSize.Width(), aSize.Height());

  layers::PlanarYCbCrData data;
  data.mPictureRect = gfx::IntRect(0, 0, reader.mWidth, reader.mHeight);

  // Y plane.
  data.mYChannel = const_cast<uint8_t*>(reader.DataY());
  data.mYStride = reader.mStrideY;
  data.mYSkip = 0;
  // Cb plane.
  data.mCbChannel = const_cast<uint8_t*>(reader.DataU());
  data.mCbSkip = 0;
  // Cr plane.
  data.mCrChannel = const_cast<uint8_t*>(reader.DataV());
  data.mCbSkip = 0;
  // CbCr plane vector.
  data.mCbCrStride = reader.mStrideU;
  data.mChromaSubsampling = gfx::ChromaSubsampling::HALF_WIDTH_AND_HEIGHT;
  // Color settings.
  if (aColorSpace.mFullRange.WasPassed() && aColorSpace.mFullRange.Value()) {
    data.mColorRange = gfx::ColorRange::FULL;
  }
  if (aColorSpace.mMatrix.WasPassed()) {
    data.mYUVColorSpace = ToColorSpace(aColorSpace.mMatrix.Value());
  }
  if (aColorSpace.mTransfer.WasPassed()) {
    data.mTransferFunction = ToTransferFunction(aColorSpace.mTransfer.Value());
  }
  // TODO: take care of aColorSpace.mPrimaries.

  RefPtr<layers::PlanarYCbCrImage> image =
      new layers::RecyclingPlanarYCbCrImage(new layers::BufferRecycleBin());
  return image->CopyData(data) ? image.forget() : nullptr;
}

static already_AddRefed<layers::Image> CreateImageFromBuffer(
    const VideoFrame::Format& aFormat, const VideoColorSpaceInit& aColorSpace,
    const gfx::IntSize& aSize, uint8_t* aBuffer) {
  switch (aFormat.PixelFormat()) {
    case VideoPixelFormat::I420:
      return CreateYUVImageFromI420Buffer(aColorSpace, aSize, aBuffer);
    case VideoPixelFormat::I420A:
    case VideoPixelFormat::I422:
    case VideoPixelFormat::I444:
    case VideoPixelFormat::NV12:
      // Not yet support for now.
      break;
    case VideoPixelFormat::RGBA:
    case VideoPixelFormat::RGBX:
    case VideoPixelFormat::BGRA:
    case VideoPixelFormat::BGRX:
      return CreateRGBAImageFromBuffer(aFormat, aSize, aBuffer);
    default:
      MOZ_ASSERT_UNREACHABLE("unsupported format");
  }
  // Unsupported format.
  return nullptr;
}

// https://w3c.github.io/webcodecs/#dom-videoframe-videoframe-data-init
template <class T>
static already_AddRefed<VideoFrame> CreateVideoFrameFromBuffer(
    const GlobalObject& aGlobal, const T& aBuffer,
    const VideoFrameBufferInit& aInit, ErrorResult& aRv) {
  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(aGlobal.GetAsSupports());
  if (!global) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  gfx::IntSize codedSize;
  Maybe<gfx::IntRect> visibleRect;
  Maybe<gfx::IntSize> displaySize;
  if (Maybe<nsAutoCString> error = ValidateVideoFrameBufferInit(
          aInit, codedSize, visibleRect, displaySize)) {
    aRv.ThrowTypeError(*error);
    return nullptr;
  }

  VideoFrame::Format format(aInit.mFormat);
  // TODO: Spec doesn't ask for this in ctor but Pixel Format does. See
  // https://github.com/w3c/webcodecs/issues/512
  // This comment should be removed once the issue is resolved.
  if (!format.IsValidSize(codedSize)) {
    aRv.ThrowTypeError("coded width and/or height is invalid");
    return nullptr;
  }

  gfx::IntRect parsedRect({0, 0}, codedSize);
  if (Maybe<nsAutoCString> error =
          ParseVisibleRect(parsedRect, visibleRect, codedSize, format)) {
    aRv.ThrowTypeError(*error);
    return nullptr;
  }

  const Sequence<PlaneLayout>* optLayout = nullptr;
  if (aInit.mLayout.WasPassed()) {
    optLayout = &aInit.mLayout.Value();
  }
  CombinedBufferLayout combinedLayout;
  if (Maybe<nsAutoCString> error = ComputeLayoutAndAllocationSize(
          combinedLayout, parsedRect, format, optLayout)) {
    aRv.ThrowTypeError(*error);
    return nullptr;
  }

  // Get buffer's data and length before using it.
  aBuffer.ComputeState();

  const CheckedInt<size_t> byteLength = ByteLength(aBuffer);
  if (byteLength.value() <
      static_cast<size_t>(combinedLayout.mAllocationSize)) {
    aRv.ThrowTypeError("data is too small");
    return nullptr;
  }

  // TODO: If codedSize is (3, 3) and visibleRect is (0, 0, 1, 1) but the data
  // is 2 x 2 RGBA buffer (2 x 2 x 4 bytes), it pass the above check. In this
  // case, we can crop it to a 1 x 1-codedSize image (Bug 1782128).
  if (byteLength.value() < format.SampleCount(codedSize)) {  // 1 byte/sample
    aRv.ThrowTypeError("data is too small");
    return nullptr;
  }

  // By spec, we should set visible* here. But if we don't change the image,
  // visible* is same as parsedRect here. The display{Width, Height} is
  // visible{Width, Height} if it's not set.

  Maybe<uint64_t> duration =
      aInit.mDuration.WasPassed() ? Some(aInit.mDuration.Value()) : Nothing();

  VideoColorSpaceInit colorSpace;
  PickColorSpace(
      colorSpace,
      aInit.mColorSpace.WasPassed() ? &aInit.mColorSpace.Value() : nullptr,
      aInit.mFormat);

  RefPtr<layers::Image> data =
      CreateImageFromBuffer(format, colorSpace, codedSize, aBuffer.Data());
  if (!data) {
    aRv.ThrowTypeError(
        "Fail to create image (e.g., unsupported format or no enough memory "
        "space)");
    return nullptr;
  }
  MOZ_ASSERT(data->GetSize() == codedSize);

  // TODO: Spec should assign aInit.mFormat to inner format value:
  // https://github.com/w3c/webcodecs/issues/509.
  // This comment should be removed once the issue is resolved.
  return MakeAndAddRef<VideoFrame>(
      global, data.forget(), aInit.mFormat, codedSize, parsedRect,
      displaySize ? *displaySize : parsedRect.Size(), std::move(duration),
      aInit.mTimestamp, colorSpace);
}

/*
 * W3C Webcodecs VideoFrame implementation
 */

VideoFrame::VideoFrame(nsIGlobalObject* aParent,
                       already_AddRefed<layers::Image> aImage,
                       const VideoPixelFormat& aFormat, gfx::IntSize aCodedSize,
                       gfx::IntRect aVisibleRect, gfx::IntSize aDisplaySize,
                       Maybe<uint64_t>&& aDuration, int64_t aTimestamp,
                       const VideoColorSpaceInit& aColorSpace)
    : mParent(aParent),
      mResource(Some(Resource(std::move(aImage), VideoFrame::Format(aFormat)))),
      mCodedSize(aCodedSize),
      mVisibleRect(aVisibleRect),
      mDisplaySize(aDisplaySize),
      mDuration(aDuration),
      mTimestamp(Some(aTimestamp)),
      mColorSpace(aColorSpace) {
  MOZ_ASSERT(mParent);
}

VideoFrame::VideoFrame(const VideoFrame& aOther)
    : mParent(aOther.mParent),
      mResource(aOther.mResource),
      mCodedSize(aOther.mCodedSize),
      mVisibleRect(aOther.mVisibleRect),
      mDisplaySize(aOther.mDisplaySize),
      mDuration(aOther.mDuration),
      mTimestamp(aOther.mTimestamp),
      mColorSpace(aOther.mColorSpace) {
  MOZ_ASSERT(mParent);
}

nsIGlobalObject* VideoFrame::GetParentObject() const { return mParent.get(); }

JSObject* VideoFrame::WrapObject(JSContext* aCx,
                                 JS::Handle<JSObject*> aGivenProto) {
  return VideoFrame_Binding::Wrap(aCx, this, aGivenProto);
}

/* static */
already_AddRefed<VideoFrame> VideoFrame::Constructor(
    const GlobalObject& global, HTMLImageElement& imageElement,
    const VideoFrameInit& init, ErrorResult& aRv) {
  aRv.Throw(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
  return nullptr;
}

/* static */
already_AddRefed<VideoFrame> VideoFrame::Constructor(
    const GlobalObject& global, SVGImageElement& svgImageElement,
    const VideoFrameInit& init, ErrorResult& aRv) {
  aRv.Throw(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
  return nullptr;
}

/* static */
already_AddRefed<VideoFrame> VideoFrame::Constructor(
    const GlobalObject& global, HTMLCanvasElement& canvasElement,
    const VideoFrameInit& init, ErrorResult& aRv) {
  aRv.Throw(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
  return nullptr;
}

/* static */
already_AddRefed<VideoFrame> VideoFrame::Constructor(
    const GlobalObject& global, HTMLVideoElement& videoElement,
    const VideoFrameInit& init, ErrorResult& aRv) {
  aRv.Throw(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
  return nullptr;
}

/* static */
already_AddRefed<VideoFrame> VideoFrame::Constructor(
    const GlobalObject& global, OffscreenCanvas& offscreenCanvas,
    const VideoFrameInit& init, ErrorResult& aRv) {
  aRv.Throw(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
  return nullptr;
}

/* static */
already_AddRefed<VideoFrame> VideoFrame::Constructor(const GlobalObject& global,
                                                     ImageBitmap& imageBitmap,
                                                     const VideoFrameInit& init,
                                                     ErrorResult& aRv) {
  aRv.Throw(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
  return nullptr;
}

/* static */
already_AddRefed<VideoFrame> VideoFrame::Constructor(const GlobalObject& global,
                                                     VideoFrame& videoFrame,
                                                     const VideoFrameInit& init,
                                                     ErrorResult& aRv) {
  aRv.Throw(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
  return nullptr;
}

// The following constructors are defined in
// https://w3c.github.io/webcodecs/#dom-videoframe-videoframe-data-init

/* static */
already_AddRefed<VideoFrame> VideoFrame::Constructor(
    const GlobalObject& aGlobal, const ArrayBufferView& aBufferView,
    const VideoFrameBufferInit& aInit, ErrorResult& aRv) {
  return CreateVideoFrameFromBuffer(aGlobal, aBufferView, aInit, aRv);
}

/* static */
already_AddRefed<VideoFrame> VideoFrame::Constructor(
    const GlobalObject& aGlobal, const ArrayBuffer& aBuffer,
    const VideoFrameBufferInit& aInit, ErrorResult& aRv) {
  return CreateVideoFrameFromBuffer(aGlobal, aBuffer, aInit, aRv);
}

// https://w3c.github.io/webcodecs/#dom-videoframe-format
Nullable<VideoPixelFormat> VideoFrame::GetFormat() const {
  // TODO: Return Nullable<T>() if this is _detached_ (bug 1774306).
  return mResource
             ? Nullable<VideoPixelFormat>(mResource->Format().PixelFormat())
             : Nullable<VideoPixelFormat>();
}

// https://w3c.github.io/webcodecs/#dom-videoframe-codedwidth
uint32_t VideoFrame::CodedWidth() const {
  return static_cast<uint32_t>(mCodedSize.Width());
}

// https://w3c.github.io/webcodecs/#dom-videoframe-codedheight
uint32_t VideoFrame::CodedHeight() const {
  return static_cast<uint32_t>(mCodedSize.Height());
}

// https://w3c.github.io/webcodecs/#dom-videoframe-codedrect
already_AddRefed<DOMRectReadOnly> VideoFrame::GetCodedRect() const {
  // TODO: Return nullptr if this is _detached_ (bug 1774306).
  return MakeAndAddRef<DOMRectReadOnly>(
      mParent, 0.0f, 0.0f, static_cast<double>(mCodedSize.Width()),
      static_cast<double>(mCodedSize.Height()));
}

// https://w3c.github.io/webcodecs/#dom-videoframe-visiblerect
already_AddRefed<DOMRectReadOnly> VideoFrame::GetVisibleRect() const {
  // TODO: Return nullptr if this is _detached_ instead of checking resource
  // (bug 1774306).
  return mResource ? MakeAndAddRef<DOMRectReadOnly>(
                         mParent, static_cast<double>(mVisibleRect.X()),
                         static_cast<double>(mVisibleRect.Y()),
                         static_cast<double>(mVisibleRect.Width()),
                         static_cast<double>(mVisibleRect.Height()))
                   : nullptr;
}

// https://w3c.github.io/webcodecs/#dom-videoframe-displaywidth
uint32_t VideoFrame::DisplayWidth() const {
  return static_cast<uint32_t>(mDisplaySize.Width());
}

// https://w3c.github.io/webcodecs/#dom-videoframe-displayheight
uint32_t VideoFrame::DisplayHeight() const {
  return static_cast<uint32_t>(mDisplaySize.Height());
}

// https://w3c.github.io/webcodecs/#dom-videoframe-duration
Nullable<uint64_t> VideoFrame::GetDuration() const {
  return mDuration ? Nullable<uint64_t>(*mDuration) : Nullable<uint64_t>();
}

// https://w3c.github.io/webcodecs/#dom-videoframe-timestamp
Nullable<int64_t> VideoFrame::GetTimestamp() const {
  return mTimestamp ? Nullable<int64_t>(*mTimestamp) : Nullable<int64_t>();
}

// https://w3c.github.io/webcodecs/#dom-videoframe-colorspace
already_AddRefed<VideoColorSpace> VideoFrame::ColorSpace() const {
  return MakeAndAddRef<VideoColorSpace>(mParent, mColorSpace);
}

// https://w3c.github.io/webcodecs/#dom-videoframe-allocationsize
uint32_t VideoFrame::AllocationSize(const VideoFrameCopyToOptions& aOptions,
                                    ErrorResult& aRv) {
  // TODO: Throw error if this is _detached_ instead of checking resource (bug
  // 1774306).
  if (!mResource) {
    aRv.ThrowInvalidStateError("No media resource in VideoFrame");
    return 0;
  }

  CombinedBufferLayout layout;
  if (Maybe<nsAutoCString> error = ParseVideoFrameCopyToOptions(
          layout, aOptions, mVisibleRect, mCodedSize, mResource->Format())) {
    // TODO: Should throw layout.
    aRv.ThrowTypeError(*error);
    return 0;
  }

  return layout.mAllocationSize;
}

// https://w3c.github.io/webcodecs/#dom-videoframe-copyto
already_AddRefed<Promise> VideoFrame::CopyTo(
    const MaybeSharedArrayBufferViewOrMaybeSharedArrayBuffer& aDestination,
    const VideoFrameCopyToOptions& aOptions, ErrorResult& aRv) {
  // TODO: Throw error if this is _detached_ instead of checking resource (bug
  // 1774306).
  if (!mResource) {
    aRv.ThrowInvalidStateError("No media resource in VideoFrame");
    return nullptr;
  }

  RefPtr<Promise> p = Promise::Create(mParent.get(), aRv);

  CombinedBufferLayout layout;
  if (Maybe<nsAutoCString> error = ParseVideoFrameCopyToOptions(
          layout, aOptions, mVisibleRect, mCodedSize, mResource->Format())) {
    // TODO: Should reject with layout.
    p->MaybeRejectWithTypeError(*error);
    return p.forget();
  }

  // Get buffer's data and length before using it.
  InitSharedArrayBuffer(aDestination);

  CheckedInt<size_t> byteLength = ByteLength(aDestination);
  if (byteLength.value() < layout.mAllocationSize) {
    p->MaybeRejectWithTypeError("Destination buffer is too small");
    return p.forget();
  }

  Sequence<PlaneLayout> planeLayouts;

  nsTArray<Format::Plane> planes = mResource->Format().Planes();
  MOZ_ASSERT(layout.mComputedLayouts.Length() == planes.Length());

  // TODO: These jobs can be run in a thread pool (bug 1780656) to unblock the
  // current thread.
  for (size_t i = 0; i < layout.mComputedLayouts.Length(); ++i) {
    ComputedPlaneLayout& l = layout.mComputedLayouts[i];
    uint32_t destinationOffset = l.mDestinationOffset;

    PlaneLayout* pl = planeLayouts.AppendElement(fallible);
    if (!pl) {
      p->MaybeRejectWithTypeError("Out of memory");
      return p.forget();
    }
    pl->mOffset = l.mDestinationOffset;
    pl->mStride = l.mDestinationStride;

    // Copy pixels of `size` starting from `origin` on planes[i] to
    // `aDestination`.
    gfx::IntPoint origin(
        l.mSourceLeftBytes / mResource->Format().SampleBytes(planes[i]),
        l.mSourceTop);
    gfx::IntSize size(
        l.mSourceWidthBytes / mResource->Format().SampleBytes(planes[i]),
        l.mSourceHeight);
    if (!mResource->CopyTo(planes[i], {origin, size},
                           GetSharedArrayBufferData(aDestination) +
                               static_cast<size_t>(destinationOffset),
                           static_cast<size_t>(l.mDestinationStride))) {
      nsAutoCString msg("Failed to copy image data in");
      msg += mResource->Format().PlaneName(planes[i]);
      msg += " plane";
      p->MaybeRejectWithTypeError(msg);
      return p.forget();
    }
  }

  MOZ_ASSERT(layout.mComputedLayouts.Length() == planes.Length());
  // TODO: Spec doesn't resolve with a value. See
  // https://github.com/w3c/webcodecs/issues/510 This comment should be removed
  // once the issue is resolved.
  p->MaybeResolve(planeLayouts);
  return p.forget();
}

// https://w3c.github.io/webcodecs/#dom-videoframe-clone
already_AddRefed<VideoFrame> VideoFrame::Clone(ErrorResult& aRv) {
  // TODO: Throw error if this is _detached_ instead of checking resource (bug
  // 1774306).
  if (!mResource) {
    aRv.ThrowInvalidStateError("No media resource in the VideoFrame now");
    return nullptr;
  }
  // The VideoFrame's data must be shared instead of copied:
  // https://w3c.github.io/webcodecs/#raw-media-memory-model-reference-counting
  return MakeAndAddRef<VideoFrame>(*this);
}

// https://w3c.github.io/webcodecs/#close-videoframe
void VideoFrame::Close() {
  // TODO: Set _detached_ to `true` (bug 1774306).
  mResource.reset();
  mCodedSize = gfx::IntSize();
  mVisibleRect = gfx::IntRect();
  mDisplaySize = gfx::IntSize();
  mDuration.reset();
  mTimestamp.reset();
}

/*
 * VideoFrame::Format
 *
 * This class wraps a VideoPixelFormat defined in [1] and provides some
 * utilities for the VideoFrame's functions. Each sample in the format is 8
 * bits. The pixel layouts for a 4 x 2 image in the spec are illustrated below:
 * [1] https://w3c.github.io/webcodecs/#pixel-format
 *
 * I420 - 3 planes: Y, U, V
 * ------
 *     <- width ->
 *  Y: Y1 Y2 Y3 Y4 ^ height
 *     Y5 Y6 Y7 Y8 v
 *  U: U1    U2      => 1/2 Y's width, 1/2 Y's height
 *  V: V1    V2      => 1/2 Y's width, 1/2 Y's height
 *
 * I420A - 4 planes: Y, U, V, A
 * ------
 *     <- width ->
 *  Y: Y1 Y2 Y3 Y4 ^ height
 *     Y5 Y6 Y7 Y8 v
 *  U: U1    U2      => 1/2 Y's width, 1/2 Y's height
 *  V: V1    V2      => 1/2 Y's width, 1/2 Y's height
 *  A: A1 A2 A3 A4   => Y's width, Y's height
 *     A5 A6 A7 A8
 *
 * I422 - 3 planes: Y, U, V
 * ------
 *     <- width ->
 *  Y: Y1 Y2 Y3 Y4 ^ height
 *     Y5 Y6 Y7 Y8 v
 *  U: U1 U2 U3 U4 => Y's width, 1/2 Y's height
 *  V: V1 V2 V3 V4 => Y's width, 1/2 Y's height
 *
 * I444 - 3 planes: Y, U, V
 * ------
 *     <- width ->
 *  Y: Y1 Y2 Y3 Y4 ^ height
 *     Y5 Y6 Y7 Y8 v
 *  U: U1 U2 U3 U4   => Y's width, Y's height
 *     U5 U6 U7 U8
 *  V: V1 V2 V3 V4   => Y's width, Y's height
 *     V5 V6 V7 B8
 *
 * NV12 - 2 planes: Y, UV
 * ------
 *     <- width ->
 *  Y: Y1 Y2 Y3 Y4 ^ height
 *     Y5 Y6 Y7 Y8 v
 * UV: U1 V1 U2 V2 => Y's width, 1/2 Y's height
 *
 * RGBA - 1 plane encoding 3 colors: Red, Green, Blue, and an Alpha value
 * ------
 *     <---------------------- width ---------------------->
 *     R1 G1 B1 A1 | R2 G2 B2 A2 | R3 G3 B3 A3 | R4 G4 B4 A4 ^ height
 *     R5 G5 B5 A5 | R6 G6 B6 A6 | R7 G7 B7 A7 | R8 G8 B8 A8 v
 *
 * RGBX - 1 plane encoding 3 colors: Red, Green, Blue, and an padding value
 *      This is the opaque version of RGBA
 * ------
 *     <---------------------- width ---------------------->
 *     R1 G1 B1 X1 | R2 G2 B2 X2 | R3 G3 B3 X3 | R4 G4 B4 X4 ^ height
 *     R5 G5 B5 X5 | R6 G6 B6 X6 | R7 G7 B7 X7 | R8 G8 B8 X8 v
 *
 * BGRA - 1 plane encoding 3 colors: Blue, Green, Red, and an Alpha value
 * ------
 *     <---------------------- width ---------------------->
 *     B1 G1 R1 A1 | B2 G2 R2 A2 | B3 G3 R3 A3 | B4 G4 R4 A4 ^ height
 *     B5 G5 R5 A5 | B6 G6 R6 A6 | B7 G7 R7 A7 | B8 G8 R8 A8 v
 *
 * BGRX - 1 plane encoding 3 colors: Blue, Green, Red, and an padding value
 *      This is the opaque version of BGRA
 * ------
 *     <---------------------- width ---------------------->
 *     B1 G1 R1 X1 | B2 G2 R2 X2 | B3 G3 R3 X3 | B4 G4 R4 X4 ^ height
 *     B5 G5 R5 X5 | B6 G6 R6 X6 | B7 G7 R7 X7 | B8 G8 R8 X8 v
 */

VideoFrame::Format::Format(const VideoPixelFormat& aFormat)
    : mFormat(aFormat) {}

const VideoPixelFormat& VideoFrame::Format::PixelFormat() const {
  return mFormat;
}

gfx::SurfaceFormat VideoFrame::Format::ToSurfaceFormat() const {
  gfx::SurfaceFormat format = gfx::SurfaceFormat::UNKNOWN;
  switch (mFormat) {
    case VideoPixelFormat::I420:
    case VideoPixelFormat::I420A:
    case VideoPixelFormat::I422:
    case VideoPixelFormat::I444:
    case VideoPixelFormat::NV12:
      // Not yet support for now.
      break;
    case VideoPixelFormat::RGBA:
      format = gfx::SurfaceFormat::R8G8B8A8;
      break;
    case VideoPixelFormat::RGBX:
      format = gfx::SurfaceFormat::R8G8B8X8;
      break;
    case VideoPixelFormat::BGRA:
      format = gfx::SurfaceFormat::B8G8R8A8;
      break;
    case VideoPixelFormat::BGRX:
      format = gfx::SurfaceFormat::B8G8R8X8;
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("unsupported format");
  }
  return format;
}

nsTArray<VideoFrame::Format::Plane> VideoFrame::Format::Planes() const {
  switch (mFormat) {
    case VideoPixelFormat::I420:
    case VideoPixelFormat::I422:
    case VideoPixelFormat::I444:
      return {Plane::Y, Plane::U, Plane::V};
    case VideoPixelFormat::I420A:
      return {Plane::Y, Plane::U, Plane::V, Plane::A};
    case VideoPixelFormat::NV12:
      return {Plane::Y, Plane::UV};
    case VideoPixelFormat::RGBA:
    case VideoPixelFormat::RGBX:
    case VideoPixelFormat::BGRA:
    case VideoPixelFormat::BGRX:
      return {Plane::RGBA};
    default:
      MOZ_ASSERT_UNREACHABLE("unsupported format");
  }
  return {};
}

const char* VideoFrame::Format::PlaneName(const Plane& aPlane) const {
  switch (aPlane) {
    case Format::Plane::Y:  // and RGBA
      return IsYUV() ? "Y" : "RGBA";
    case Format::Plane::U:  // and UV
      MOZ_ASSERT(IsYUV());
      return mFormat == VideoPixelFormat::NV12 ? "UV" : "U";
    case Format::Plane::V:
      MOZ_ASSERT(IsYUV());
      return "V";
    case Format::Plane::A:
      MOZ_ASSERT(IsYUV());
      return "A";
    default:
      MOZ_ASSERT_UNREACHABLE("invalid plane");
  }
  return "Unknown";
}

uint32_t VideoFrame::Format::SampleBytes(const Plane& aPlane) const {
  switch (mFormat) {
    case VideoPixelFormat::I420:
    case VideoPixelFormat::I420A:
    case VideoPixelFormat::I422:
    case VideoPixelFormat::I444:
      return 1;  // 8 bits/sample on the Y, U, V, A plane.
    case VideoPixelFormat::NV12:
      switch (aPlane) {
        case Plane::Y:
          return 1;  // 8 bits/sample on the Y plane
        case Plane::UV:
          return 2;  // Interleaved U and V values on the UV plane.
        default:
          MOZ_ASSERT_UNREACHABLE("invalid plane");
      }
      break;
    case VideoPixelFormat::RGBA:
    case VideoPixelFormat::RGBX:
    case VideoPixelFormat::BGRA:
    case VideoPixelFormat::BGRX:
      return 4;  // 8 bits/sample, 32 bits/pixel
    default:
      MOZ_ASSERT_UNREACHABLE("unsupported format");
  }
  return 0;
}

gfx::IntSize VideoFrame::Format::SampleSize(const Plane& aPlane) const {
  // The sample width and height refers to
  // https://w3c.github.io/webcodecs/#sub-sampling-factor
  switch (aPlane) {
    case Plane::Y:  // and RGBA
    case Plane::A:
      return gfx::IntSize(1, 1);
    case Plane::U:  // and UV
    case Plane::V:
      switch (mFormat) {
        case VideoPixelFormat::I420:
        case VideoPixelFormat::I420A:
        case VideoPixelFormat::NV12:
          return gfx::IntSize(2, 2);
        case VideoPixelFormat::I422:
          return gfx::IntSize(2, 1);
        case VideoPixelFormat::I444:
          return gfx::IntSize(1, 1);
        default:
          MOZ_ASSERT_UNREACHABLE("invalid format");
      }
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("invalid plane");
  }
  return {0, 0};
}

bool VideoFrame::Format::IsValidSize(const gfx::IntSize& aSize) const {
  switch (mFormat) {
    case VideoPixelFormat::I420:
    case VideoPixelFormat::I420A:
    case VideoPixelFormat::NV12:
      return (aSize.Width() % 2 == 0) && (aSize.Height() % 2 == 0);
    case VideoPixelFormat::I422:
      return aSize.Height() % 2 == 0;
    case VideoPixelFormat::I444:
    case VideoPixelFormat::RGBA:
    case VideoPixelFormat::RGBX:
    case VideoPixelFormat::BGRA:
    case VideoPixelFormat::BGRX:
      return true;
    default:
      MOZ_ASSERT_UNREACHABLE("unsupported format");
  }
  return false;
}

size_t VideoFrame::Format::SampleCount(const gfx::IntSize& aSize) const {
  MOZ_ASSERT(IsValidSize(aSize));

  CheckedInt<size_t> count(aSize.Width());
  count *= aSize.Height();

  switch (mFormat) {
    case VideoPixelFormat::I420:
    case VideoPixelFormat::NV12:
      return (count + count / 2).value();
    case VideoPixelFormat::I420A:
      return (count * 2 + count / 2).value();
    case VideoPixelFormat::I422:
      return (count * 2).value();
    case VideoPixelFormat::I444:
      return (count * 3).value();
    case VideoPixelFormat::RGBA:
    case VideoPixelFormat::RGBX:
    case VideoPixelFormat::BGRA:
    case VideoPixelFormat::BGRX:
      return (count * 4).value();
    default:
      MOZ_ASSERT_UNREACHABLE("unsupported format");
  }
  return 0;
}

bool VideoFrame::Format::IsYUV() const {
  switch (mFormat) {
    case VideoPixelFormat::I420:
    case VideoPixelFormat::I420A:
    case VideoPixelFormat::I422:
    case VideoPixelFormat::I444:
    case VideoPixelFormat::NV12:
      return true;
    case VideoPixelFormat::RGBA:
    case VideoPixelFormat::RGBX:
    case VideoPixelFormat::BGRA:
    case VideoPixelFormat::BGRX:
      return false;
    default:
      MOZ_ASSERT_UNREACHABLE("unsupported format");
  }
  return false;
}

/*
 * VideoFrame::Resource
 */

VideoFrame::Resource::Resource(already_AddRefed<layers::Image> aImage,
                               const class Format& aFormat)
    : mImage(aImage), mFormat(aFormat) {
  MOZ_ASSERT(mImage);
}

VideoFrame::Resource::Resource(const Resource& aOther)
    : mImage(aOther.mImage), mFormat(aOther.mFormat) {
  MOZ_ASSERT(mImage);
}

const VideoFrame::Format& VideoFrame::Resource::Format() const {
  return mFormat;
}

const layers::Image* VideoFrame::Resource::Image() const {
  return mImage.get();
}

uint32_t VideoFrame::Resource::Stride(const Format::Plane& aPlane) const {
  CheckedInt<uint32_t> width(mImage->GetSize().Width());
  switch (aPlane) {
    case Format::Plane::Y:  // and RGBA
    case Format::Plane::A:
      switch (mFormat.PixelFormat()) {
        case VideoPixelFormat::I420:
        case VideoPixelFormat::I420A:
        case VideoPixelFormat::I422:
        case VideoPixelFormat::I444:
        case VideoPixelFormat::NV12:
        case VideoPixelFormat::RGBA:
        case VideoPixelFormat::RGBX:
        case VideoPixelFormat::BGRA:
        case VideoPixelFormat::BGRX:
          return (width * mFormat.SampleBytes(aPlane)).value();
        default:
          MOZ_ASSERT_UNREACHABLE("invalid format");
          break;
      }
      break;
    case Format::Plane::U:  // and UV
    case Format::Plane::V:
      switch (mFormat.PixelFormat()) {
        case VideoPixelFormat::I420:
        case VideoPixelFormat::I420A:
        case VideoPixelFormat::I422:
        case VideoPixelFormat::I444:
        case VideoPixelFormat::NV12:
          return (((width + 1) / 2) * mFormat.SampleBytes(aPlane)).value();
        default:
          MOZ_ASSERT_UNREACHABLE("invalid format");
          break;
      }
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("invalid plane");
  }
  return 0;
}

bool VideoFrame::Resource::CopyTo(const Format::Plane& aPlane,
                                  const gfx::IntRect& aRect,
                                  uint8_t* aDestination,
                                  size_t aDestinationStride) const {
  auto copyPlane = [&, this](const uint8_t* aPlaneData) {
    MOZ_ASSERT(aPlaneData);

    CheckedInt<size_t> offset(aRect.Y());
    offset *= Stride(aPlane);
    offset += aRect.X() * mFormat.SampleBytes(aPlane);
    if (!offset.isValid()) {
      return false;
    }

    CheckedInt<size_t> elementsBytes(aRect.Width());
    elementsBytes *= mFormat.SampleBytes(aPlane);
    if (!elementsBytes.isValid()) {
      return false;
    }

    aPlaneData += offset.value();
    for (int32_t row = 0; row < aRect.Height(); ++row) {
      PodCopy(aDestination, aPlaneData, elementsBytes.value());
      aPlaneData += Stride(aPlane);
      // Spec asks to move `aDestinationStride` bytes instead of
      // `Stride(aPlane)` forward.
      aDestination += aDestinationStride;
    }
    return true;
  };

  if (mImage->GetFormat() == ImageFormat::MOZ2D_SURFACE) {
    RefPtr<gfx::SourceSurface> surface = mImage->GetAsSourceSurface();
    if (NS_WARN_IF(!surface)) {
      return false;
    }

    RefPtr<gfx::DataSourceSurface> dataSurface = surface->GetDataSurface();
    if (NS_WARN_IF(!surface)) {
      return false;
    }

    gfx::DataSourceSurface::ScopedMap map(dataSurface,
                                          gfx::DataSourceSurface::READ);
    if (NS_WARN_IF(!map.IsMapped())) {
      return false;
    }

    const gfx::SurfaceFormat format = dataSurface->GetFormat();

    if (format == gfx::SurfaceFormat::R8G8B8A8 ||
        format == gfx::SurfaceFormat::R8G8B8X8 ||
        format == gfx::SurfaceFormat::B8G8R8A8 ||
        format == gfx::SurfaceFormat::B8G8R8X8) {
      MOZ_ASSERT(aPlane == Format::Plane::RGBA);

      // The mImage's format can be different from mFormat (since Gecko prefers
      // BGRA). To get the data in the matched format, we create a temp buffer
      // holding the image data in that format and then copy them to
      // `aDestination`.
      const gfx::SurfaceFormat f = mFormat.ToSurfaceFormat();
      MOZ_ASSERT(f == gfx::SurfaceFormat::R8G8B8A8 ||
                 f == gfx::SurfaceFormat::R8G8B8X8 ||
                 f == gfx::SurfaceFormat::B8G8R8A8 ||
                 f == gfx::SurfaceFormat::B8G8R8X8);

      // TODO: We could use Factory::CreateWrappingDataSourceSurface to wrap
      // `aDestination` to avoid extra copy.
      RefPtr<gfx::DataSourceSurface> tempSurface =
          gfx::Factory::CreateDataSourceSurfaceWithStride(
              dataSurface->GetSize(), f, map.GetStride());
      if (NS_WARN_IF(!tempSurface)) {
        return false;
      }

      gfx::DataSourceSurface::ScopedMap tempMap(tempSurface,
                                                gfx::DataSourceSurface::WRITE);
      if (NS_WARN_IF(!tempMap.IsMapped())) {
        return false;
      }

      if (!gfx::SwizzleData(map.GetData(), map.GetStride(),
                            dataSurface->GetFormat(), tempMap.GetData(),
                            tempMap.GetStride(), tempSurface->GetFormat(),
                            tempSurface->GetSize())) {
        return false;
      }

      return copyPlane(tempMap.GetData());
    }

    return false;
  }

  if (mImage->GetFormat() == ImageFormat::PLANAR_YCBCR) {
    switch (aPlane) {
      case Format::Plane::Y:
        return copyPlane(mImage->AsPlanarYCbCrImage()->GetData()->mYChannel);
      case Format::Plane::U:
        return copyPlane(mImage->AsPlanarYCbCrImage()->GetData()->mCbChannel);
      case Format::Plane::V:
        return copyPlane(mImage->AsPlanarYCbCrImage()->GetData()->mCrChannel);
      default:
        MOZ_ASSERT_UNREACHABLE("invalid plane");
    }
  }

  return false;
}

}  // namespace mozilla::dom
