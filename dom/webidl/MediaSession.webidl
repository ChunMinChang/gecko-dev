/* -*- Mode: IDL; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * The origin of this IDL file is
 * https://w3c.github.io/mediasession/#idl-index
 */

enum MediaSessionAction {
  "play",
  "pause",
  "seekbackward",
  "seekforward",
  "previoustrack",
  "nexttrack",
  "stop",
  "seekto"
};

callback MediaSessionActionHandler = void(MediaSessionActionDetails details);

[Exposed=Window, Pref="media.mediasession.enabled"]
interface MediaSession {
  attribute MediaMetadata? metadata;
  void setActionHandler(MediaSessionAction action, MediaSessionActionHandler? handler);
};

[Constructor(optional MediaMetadataInit init = {}), Exposed=Window, Pref="media.mediasession.enabled"]
interface MediaMetadata {
  attribute DOMString title;
  attribute DOMString artist;
  attribute DOMString album;
  // TODO: Use FrozenArray once available. (Bug 1236777)
  // attribute FrozenArray<MediaImage> artwork;
  [Frozen, Cached, Pure, SetterThrows]
  attribute sequence<MediaImage> artwork;
};

dictionary MediaMetadataInit {
  DOMString title = "";
  DOMString artist = "";
  DOMString album = "";
  sequence<MediaImage> artwork = [];
};

dictionary MediaImage {
  required USVString src;
  DOMString sizes = "";
  DOMString type = "";
};

dictionary MediaSessionActionDetails {
  required MediaSessionAction action;
};

dictionary MediaSessionSeekActionDetails : MediaSessionActionDetails {
  double? seekOffset;
};

dictionary MediaSessionSeekToActionDetails : MediaSessionActionDetails {
  required double seekTime;
  boolean? fastSeek;
};