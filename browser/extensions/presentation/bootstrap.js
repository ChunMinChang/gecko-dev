/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const {classes: Cc, interfaces: Ci, utils: Cu} = Components;

Cu.import("resource://gre/modules/XPCOMUtils.jsm");

// Presentation API
XPCOMUtils.defineLazyModuleGetter(this, "Presentation",
                                  "chrome://presentation.api/content/Presentation.jsm");

function log(aMsg) {
  // dump("@ presentation add-on: " + aMsg + "\n");
}

function install(aData, aReason) {
}

function uninstall(aData, aReason) {
}

function startup(aData, aReason) {
  log("startup");
  Presentation.init();
}

function shutdown(aData, aReason) {
  log("shutdown");
  Presentation.uninit();
}
