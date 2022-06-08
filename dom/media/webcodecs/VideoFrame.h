/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_VideoFrame_h
#define mozilla_dom_VideoFrame_h

#include "js/TypeDecls.h"
#include "mozilla/Attributes.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/dom/BindingDeclarations.h"
#include "mozilla/dom/TypedArray.h"
#include "mozilla/gfx/Point.h"
#include "mozilla/gfx/Rect.h"
#include "nsCycleCollectionParticipant.h"
#include "nsWrapperCache.h"

class nsIGlobalObject;

namespace mozilla {

namespace layers {
class Image;
}  // namespace layers

namespace dom {

class DOMRectReadOnly;
class HTMLCanvasElement;
class HTMLImageElement;
class HTMLVideoElement;
class ImageBitmap;
class MaybeSharedArrayBufferViewOrMaybeSharedArrayBuffer;
class OffscreenCanvas;
class OwningMaybeSharedArrayBufferViewOrMaybeSharedArrayBuffer;
class Promise;
class SVGImageElement;
class VideoColorSpace;
class VideoFrame;
enum class VideoPixelFormat : uint8_t;
struct VideoFrameBufferInit;
struct VideoFrameInit;
struct VideoFrameCopyToOptions;

}  // namespace dom
}  // namespace mozilla

namespace mozilla::dom {

class VideoFrame final
    : public nsISupports /* or NonRefcountedDOMObject if this is a
                            non-refcounted object */
    ,
      public nsWrapperCache /* Change wrapperCache in the binding configuration
                               if you don't want this */
{
 public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS(VideoFrame)

 public:
  VideoFrame(nsIGlobalObject* aParent, already_AddRefed<layers::Image> aImage,
             const VideoPixelFormat& aFormat, gfx::IntSize aCodedSize,
             gfx::IntRect aVisibleRect, gfx::IntSize aDisplaySize,
             Maybe<uint64_t>&& aDuration, int64_t aTimestamp,
             const VideoColorSpaceInit& aColorSpace);

  VideoFrame(const VideoFrame& aOther);

 protected:
  ~VideoFrame() = default;

 public:
  // This should return something that eventually allows finding a
  // path to the global this object is associated with.  Most simply,
  // returning an actual global works.
  nsIGlobalObject* GetParentObject() const;

  JSObject* WrapObject(JSContext* aCx,
                       JS::Handle<JSObject*> aGivenProto) override;

  static already_AddRefed<VideoFrame> Constructor(const GlobalObject& global,
                                                  HTMLImageElement& image,
                                                  const VideoFrameInit& init,
                                                  ErrorResult& aRv);
  static already_AddRefed<VideoFrame> Constructor(const GlobalObject& global,
                                                  SVGImageElement& image,
                                                  const VideoFrameInit& init,
                                                  ErrorResult& aRv);
  static already_AddRefed<VideoFrame> Constructor(const GlobalObject& global,
                                                  HTMLCanvasElement& image,
                                                  const VideoFrameInit& init,
                                                  ErrorResult& aRv);
  static already_AddRefed<VideoFrame> Constructor(const GlobalObject& global,
                                                  HTMLVideoElement& image,
                                                  const VideoFrameInit& init,
                                                  ErrorResult& aRv);
  static already_AddRefed<VideoFrame> Constructor(const GlobalObject& global,
                                                  OffscreenCanvas& image,
                                                  const VideoFrameInit& init,
                                                  ErrorResult& aRv);
  static already_AddRefed<VideoFrame> Constructor(const GlobalObject& aGlobal,
                                                  ImageBitmap& aImage,
                                                  const VideoFrameInit& aInit,
                                                  ErrorResult& aRv);
  static already_AddRefed<VideoFrame> Constructor(
      const GlobalObject& aGlobal, const ArrayBufferView& aData,
      const VideoFrameBufferInit& aInit, ErrorResult& aRv);
  static already_AddRefed<VideoFrame> Constructor(
      const GlobalObject& aGlobal, const ArrayBuffer& aData,
      const VideoFrameBufferInit& aInit, ErrorResult& aRv);

  Nullable<VideoPixelFormat> GetFormat() const;

  uint32_t CodedWidth() const;

  uint32_t CodedHeight() const;

  already_AddRefed<DOMRectReadOnly> GetCodedRect() const;

  already_AddRefed<DOMRectReadOnly> GetVisibleRect() const;

  uint32_t DisplayWidth() const;

  uint32_t DisplayHeight() const;

  Nullable<uint64_t> GetDuration() const;

  Nullable<int64_t> GetTimestamp() const;

  already_AddRefed<VideoColorSpace> ColorSpace() const;

  uint32_t AllocationSize(const VideoFrameCopyToOptions& aOptions,
                          ErrorResult& aRv);

  already_AddRefed<Promise> CopyTo(
      const MaybeSharedArrayBufferViewOrMaybeSharedArrayBuffer& aDestination,
      const VideoFrameCopyToOptions& aOptions, ErrorResult& aRv);

  already_AddRefed<VideoFrame> Clone(ErrorResult& aRv);

  void Close();

 public:
  // A VideoPixelFormat wrapper providing utilities for VideoFrame.
  class Format final {
   public:
    explicit Format(const VideoPixelFormat& aFormat);
    ~Format() = default;
    const VideoPixelFormat& PixelFormat() const;
    gfx::SurfaceFormat ToSurfaceFormat() const;
    void Opaque();

    enum class Plane : uint8_t { Y = 0, RGBA = Y, U = 1, UV = U, V = 2, A = 3 };
    nsTArray<Plane> Planes() const;
    uint32_t SampleBytes(const Plane& aPlane) const;
    gfx::IntSize SampleSize(const Plane& aPlane) const;

   private:
    VideoPixelFormat mFormat;
  };

 private:
  // A class representing the VideoFrame's data.
  class Resource final {
   public:
    Resource(already_AddRefed<layers::Image> aImage, const Format& aFormat);
    Resource(const Resource& aOther);
    ~Resource() = default;
    const Format& Format() const;
    const uint8_t* Data() const;
    uint32_t Stride(const Format::Plane& aPlane) const;

   private:
    RefPtr<layers::Image> mImage;
    class Format mFormat;
  };

  nsCOMPtr<nsIGlobalObject> mParent;

  // Use Maybe instead of UniquePtr to allow copy ctor.
  Maybe<Resource> mResource;  // Nothing() after `Close()`d

  // TODO: Replace this by mResource->mImage->GetSize()?
  gfx::IntSize mCodedSize;
  gfx::IntRect mVisibleRect;
  gfx::IntSize mDisplaySize;

  Maybe<uint64_t> mDuration;
  Maybe<int64_t> mTimestamp;  // Nothing() after `Close()`d
  VideoColorSpaceInit mColorSpace;
};

}  // namespace mozilla::dom

#endif  // mozilla_dom_VideoFrame_h
