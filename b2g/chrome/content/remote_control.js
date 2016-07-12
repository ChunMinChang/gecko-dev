/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- /
/* vim: set shiftwidth=2 tabstop=2 autoindent cindent expandtab: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

Cu.import("resource://gre/modules/Services.jsm");

function getCurrentTime() {
  let time = new Date().getTime();
  let str = new Date(time).toString();
  return str;
}

function debug(s) {
  dump("[ " + getCurrentTime() + " ] remote_control: " + s + "\n");
}

const kPrefPresentationDiscoverable = "dom.presentation.discoverable";

// To keep RemoteContorlService in the scope to prevent import again
var remoteControlScope = {};

function importRemoteControlService() {
  if (!("RemoteControlService" in remoteControlScope)) {
    Cu.import("resource://gre/modules/RemoteControlService.jsm", remoteControlScope);
  }
}

var statusMonitor = {
  init: function sm_init() {
    debug("init");

    Services.prefs.addObserver(kPrefPresentationDiscoverable, this, false);
    Services.obs.addObserver(this, "network-active-changed", false);
    Services.obs.addObserver(this, "network:offline-status-changed", false);
  },

  get discoverable() {
    return Services.prefs.getBoolPref(kPrefPresentationDiscoverable);
  },

  observe: function sm_observe(subject, topic, data) {
    debug("observe: " + topic + ", " + data);

    switch (topic) {
      case "network-active-changed": {
        if (!subject) {
          // Stop service when there is no active network
          this.stopService();
          break;
        }

        // Start service when active network change with new IP address
        // Other case will be handled by "network:offline-status-changed"
        this.startService();
        break;

        break;
      }
      case "network:offline-status-changed": {
        if (data == "offline") {
          // Stop service when network status change to offline
          this.stopService();
        } else { // online
          // Resume service when network status change to online
          this.startService();
        }
        break;
      }
      case "nsPref:changed": {
        if (data != kPrefPresentationDiscoverable) {
          break;
        }

        if (!this.startService()) {
          this.stopService();
        }

        break;
      }
      default:
        break;
    }
  },

  startService: function sm_startService() {
    debug("startService");
    // Start only when network is alive and presentation is discoverable
    if (!Services.io.offline && this.discoverable) {
      importRemoteControlService();
      remoteControlScope.RemoteControlService.start();
      debug("  YES");
      return true;
    }
    debug("  NO");
    return false;
  },

  stopService: function sm_stopService() {
    debug("stopService");
    remoteControlScope.RemoteControlService &&
    remoteControlScope.RemoteControlService.stop();
  }
};

statusMonitor.init();
