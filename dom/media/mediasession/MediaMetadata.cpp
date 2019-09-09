/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/MediaMetadata.h"
#include "mozilla/dom/MediaSessionBinding.h"
#include "nsNetUtil.h"
#include "nsPIDOMWindow.h"

namespace mozilla {
namespace dom {


// Only needed for refcounted objects.
NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(MediaMetadata, mWindow)
NS_IMPL_CYCLE_COLLECTING_ADDREF(MediaMetadata)
NS_IMPL_CYCLE_COLLECTING_RELEASE(MediaMetadata)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(MediaMetadata)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

MediaMetadata::MediaMetadata(nsPIDOMWindowInner* aWindow, nsString aTitle,
                             nsString aArtist, nsString aAlbum,
                             const Sequence<MediaImage>& aArtwork,
                             ErrorResult& aRv)
    : mWindow(aWindow), mTitle(aTitle), mArtist(aArtist), mAlbum(aAlbum) {
  MOZ_ASSERT(mWindow);
  SetArtworkInternal(aArtwork, aRv);
}

MediaMetadata::~MediaMetadata() {}

nsPIDOMWindowInner* MediaMetadata::GetParentObject() const { return mWindow; }

JSObject*
MediaMetadata::WrapObject(JSContext* aCx, JS::Handle<JSObject*> aGivenProto)
{
  return MediaMetadata_Binding::Wrap(aCx, this, aGivenProto);
}

already_AddRefed<MediaMetadata>
MediaMetadata::Constructor(const GlobalObject& aGlobal, const MediaMetadataInit& aInit, ErrorResult& aRv)
{
  nsCOMPtr<nsPIDOMWindowInner> win = do_QueryInterface(aGlobal.GetAsSupports());
  if (!win) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  RefPtr<MediaMetadata> mediaMetadata = new MediaMetadata(
      win, aInit.mTitle, aInit.mArtist, aInit.mAlbum, aInit.mArtwork, aRv);

  return mediaMetadata.forget();
}

void MediaMetadata::GetTitle(nsString& aRetVal) const { aRetVal = mTitle; }

void MediaMetadata::SetTitle(const nsAString& aTitle) { mTitle = aTitle; }

void MediaMetadata::GetArtist(nsString& aRetVal) const { aRetVal = mArtist; }

void MediaMetadata::SetArtist(const nsAString& aArtist) { mArtist = aArtist; }

void MediaMetadata::GetAlbum(nsString& aRetVal) const { aRetVal = mAlbum; }

void MediaMetadata::SetAlbum(const nsAString& aAlbum) { mAlbum = aAlbum; }

void MediaMetadata::GetArtwork(nsTArray<MediaImage>& aRetVal) const {
  aRetVal = mArtwork;
}

void MediaMetadata::SetArtwork(const Sequence<MediaImage>& aArtwork,
                               ErrorResult& aRv) {
  SetArtworkInternal(aArtwork, aRv);
}

void MediaMetadata::SetArtworkInternal(nsTArray<MediaImage> aArtwork,
                                       ErrorResult& aRv) {
  for (MediaImage& image : aArtwork) {
    nsresult rv = GetAbsoluteUrl(image.mSrc, image.mSrc);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      aRv.ThrowTypeError<MSG_INVALID_URL>(image.mSrc);
      return;
    }
  }
  mArtwork = aArtwork;
}

nsresult MediaMetadata::GetAbsoluteUrl(nsString& aSrc, nsString& aDest) {
  nsCOMPtr<Document> doc = mWindow->GetDoc();
  MOZ_ASSERT(doc);

  nsCOMPtr<nsIURI> uri;
  nsresult rv =
      NS_NewURI(getter_AddRefs(uri), aSrc, doc->GetDocumentCharacterSet(),
                mWindow->GetDocBaseURI());
  if (NS_FAILED(rv)) {
    return rv;
  }

  nsAutoCString spec;
  rv = uri->GetSpec(spec);
  if (NS_FAILED(rv)) {
    return rv;
  }

  aDest = NS_ConvertUTF8toUTF16(spec);
  return NS_OK;
}

} // namespace dom
} // namespace mozilla
