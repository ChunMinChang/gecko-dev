/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { classes: Cc, interfaces: Ci, utils: Cu } = Components;

Cu.import('resource://gre/modules/XPCOMUtils.jsm');
Cu.import('resource://gre/modules/Services.jsm');

XPCOMUtils.defineLazyModuleGetter(this, "Prompt",
                                  "resource://gre/modules/Prompt.jsm");

const kPRESENTATIONDEVICEPROMPT_CONTRACTID = "@mozilla.org/presentation-device/prompt;1";
const kPRESENTATIONDEVICEPROMPT_CID        = Components.ID("{388bd149-c919-4a43-b646-d7ec57877689}");

function debug(aMsg) {
  dump("-*- PresentationDevicePrompt: " + aMsg + "\n");
}

// nsIPresentationDevicePrompt
function PresentationDevicePrompt() {
  debug("PresentationDevicePrompt init");
}

PresentationDevicePrompt.prototype = {
  classID: kPRESENTATIONDEVICEPROMPT_CID,
  contractID: kPRESENTATIONDEVICEPROMPT_CONTRACTID,
  classDescription: "Fennec Presentation Device Prompt",
  QueryInterface: XPCOMUtils.generateQI([Ci.nsIPresentationDevicePrompt]),

  _getString: function(aName) {
    debug("_getString");

    if (!this.bundle) {
        this.bundle = Services.strings.createBundle("chrome://browser/locale/devicePrompt.properties");
    }
    return this.bundle.GetStringFromName(aName);
  },

  _loadDevices: function() {
    debug("_loadDevices");

    let deviceList = [];

    let deviceManager = Cc["@mozilla.org/presentation-device/manager;1"]
                          .getService(Ci.nsIPresentationDeviceManager);
    let devices = deviceManager.getAvailableDevices().QueryInterface(Ci.nsIArray);

    for (let i = 0; i < devices.length; i++) {
      let device = devices.queryElementAt(i, Ci.nsIPresentationDevice);
      deviceList.push(device);
    }

    return deviceList;
  },

  _getPromptMenu: function(aDevices) {
    debug("_getPromptMenu");

    return aDevices.map(function(device) {
      return { label: device.name };
    });
  },

  _getPrompt: function(aTitle, aMenu) {
    debug("_getPrompt");

    let p = new Prompt({
      title: aTitle,
    });

    p.setSingleChoiceItems(aMenu);

    return p;
  },

  _showPrompt: function(aPrompt) {
    debug("_showPrompt");

    let response = null;
    aPrompt.show(function(data) {
      response = data;
    });

    // Spin this thread while we wait for a result
    let thread = Services.tm.currentThread;
    while (response === null)
      thread.processNextEvent(true);

    return response;
  },

  // This will be fired when window.PresentationRequest(URL).start() is called
  promptDeviceSelection: function(aRequest) {
    debug("promptDeviceSelection");

    let devices = this._loadDevices();

    if (!devices.length) { // Cancel request if no available device
      aRequest.cancel();
    }

    let prompt = this._getPrompt(this._getString("deviceMenu.title"),
                                 this._getPromptMenu(devices));
    let response = this._showPrompt(prompt);
    let deviceIndex = response.button;

    if (deviceIndex < 0) { // Cancel request if no selected device
      aRequest.cancel();
    }

    aRequest.select(devices[deviceIndex]);
  },
};

this.NSGetFactory = XPCOMUtils.generateNSGetFactory([PresentationDevicePrompt]);
