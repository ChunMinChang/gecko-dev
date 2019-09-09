/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_MediaMetadata_h
#define mozilla_dom_MediaMetadata_h

#include "js/TypeDecls.h"
#include "mozilla/Attributes.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/dom/BindingDeclarations.h"
#include "nsCycleCollectionParticipant.h"
#include "nsWrapperCache.h"

namespace mozilla {
namespace dom {

struct MediaImage;
struct MediaMetadataInit;

} // namespace dom
} // namespace mozilla

namespace mozilla {
namespace dom {

class MediaMetadata final : public nsISupports, public nsWrapperCache
{
public:
  // Ref counting and cycle collection
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS(MediaMetadata)

  // WebIDL methods
  MediaMetadata* GetParentObject() const;

  virtual JSObject* WrapObject(JSContext* aCx, JS::Handle<JSObject*> aGivenProto) override;

  static already_AddRefed<MediaMetadata> Constructor(const GlobalObject& aGlobal, const MediaMetadataInit& aInit, ErrorResult& aRv);

  void GetTitle(nsString& aRetVal) const;

  void SetTitle(const nsAString& aTitle);

  void GetArtist(nsString& aRetVal) const;

  void SetArtist(const nsAString& aArtist);

  void GetAlbum(nsString& aRetVal) const;

  void SetAlbum(const nsAString& aAlbum);

  void GetArtwork(nsTArray<MediaImage>& aRetVal) const;

  void SetArtwork(const Sequence<MediaImage>& aArtwork, ErrorResult& aRv);

private:
  explicit MediaMetadata();
  ~MediaMetadata();
};

} // namespace dom
} // namespace mozilla

#endif // mozilla_dom_MediaMetadata_h
