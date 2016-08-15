/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

this.EXPORTED_SYMBOLS = ["AudioController"];

const { classes: Cc, interfaces: Ci, utils: Cu } = Components;

Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/Services.jsm");

const kPrefAutoplayMutable = "media.autoplay.mutable";

function log(aMsg) {
  // Prefix here is useful to grep log.
  dump("@ AudioController: " + aMsg + "\n");
}

// Notice that the variable name here should be same as the toString() value
// in BrowserApp.java's enum RingerMode.
const RingerMode = Object.freeze({
  Unknown: 0,
  Normal: 1,
  Vibrate: 2,
  Silent: 3,
});

var AudioController = {
  _ringerMode: RingerMode.Unknown,

  init: function() {
    log("init");
    // Register observer
    Services.obs.addObserver(this, "RingerMode:Change", false);
    Services.prefs.addObserver(kPrefAutoplayMutable, this, false);
  },

  uninit: function() {
    log("uninit");
    // Unregister observer
    Services.obs.removeObserver(this, "RingerMode:Change");
    Services.prefs.removeObserver(kPrefAutoplayMutable, this);
  },

  observe: function (aSubject, aTopic, aData) {
    log("observe: " + aTopic + " > " + aData);
    switch(aTopic) {
      case "RingerMode:Change": {
        // Update mode
        this._ringerMode = RingerMode[aData];
        break;
      }
      case "nsPref:changed": {
        if (aData != kPrefAutoplayMutable) {
          break;
        }

        log(kPrefAutoplayMutable + ": " + this.autoplayMutable);
        break;
      }
      default:
        break;
    }
  },

  maybeMuteAutoplay: function(aDocument) {
    log("maybeMuteAutoplay: " + this.autoplayMutable + ", " + this._ringerMode);
    if (!this.autoplayMutable ||
        this._ringerMode != RingerMode.Silent) {
      return false;
    }

    let muted = false;
    let medias = this._getMediaElements(aDocument);
    for (let m of medias) {
      if (m.autoplay && !m.muted) {
        m.muted = true;
        muted = true;
      }
    }
    return muted;
  },

  _getMediaElements: function(aDocument) {
    let videos = this._getElementsFromTag("video", aDocument);
    let audios = this._getElementsFromTag("audio", aDocument);
    return (videos) ? videos.concat(audios) : audios;
  },

  _getElementsFromTag: function(aTag, aDocument) {
    return Array.prototype.slice.call(aDocument.getElementsByTagName(aTag));
  },

  get autoplayMutable() {
    return Services.prefs.getBoolPref(kPrefAutoplayMutable);
  },
};
