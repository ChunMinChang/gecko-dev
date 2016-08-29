/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * This is the implementation of nsIPresentationDevicePrompt XPCOM.
 * It will be registered into a XPCOM component by Presentation.jsm.
 *
 * This component will prompt a device selection UI for users to choose which
 * devices they want to connect, when PresentationRequest is started.
 */

"use strict";

var EXPORTED_SYMBOLS = ["PresentationDevicePrompt"];

const { classes: Cc, interfaces: Ci, utils: Cu , results: Cr } = Components;

Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/Services.jsm");

// An string bundle for localization.
XPCOMUtils.defineLazyGetter(this, "Strings", function() {
  return Services.strings.createBundle("chrome://presentation.api/locale/presentation.properties");
});

/*
 * Utils
 */
function log(aMsg) {
  // Prefix is useful to grep log.
  dump("@ PresentationDevicePrompt: " + aMsg + "\n");
}

function GetString(aName, aStr) {
  return (aStr) ? Strings.formatStringFromName(aName, [aStr], 1) :
                  Strings.GetStringFromName(aName);
}

/*
 * Device Selection UI
 */
const MAIN_POPUPSET = "mainPopupSet";
const PANEL_ID = "presentation-devices-panel";
const PANEL_UI_URL = "chrome://presentation.api/content/ui/menu.html"

const UI_WIDTH = 320;
const UI_HEIGHT = 350;

/*
 * nsIPresentationDevicePrompt
 */
// For XPCOM registration
const PRESENTATIONDEVICEPROMPT_CONTRACTID = "@mozilla.org/presentation-device/prompt;1";
const PRESENTATIONDEVICEPROMPT_CID        = Components.ID("{388bd149-c919-4a43-b646-d7ec57877689}");

function PresentationDevicePrompt() {}

PresentationDevicePrompt.prototype = {
  // properties required for XPCOM registration:
  classID: PRESENTATIONDEVICEPROMPT_CID,
  classDescription: "Firefox Desktop Presentation Device Prompt",
  contractID: PRESENTATIONDEVICEPROMPT_CONTRACTID,
  QueryInterface: XPCOMUtils.generateQI([Ci.nsIPresentationDevicePrompt]),

  _devices: [],   // The current available devices.
  _browser: null, // The browser that starts the PresentationRequest.
  _uiPanel: null, // The panel element attaching to the requesting browser.
  _uiFrame: null, // The iframe element embedded in the panel UI.

  // This will be fired when window.PresentationRequest(URL).start() is called.
  promptDeviceSelection: function(aRequest) {
    log("promptDeviceSelection: " + aRequest.requestURL);

    this._browser = this._getBrowserForRequest(aRequest);
    this._devices = this._loadDevices();

    // Cancel request if no available device.
    if (!this._devices.length) {
      log("No available device.");
      aRequest.cancel(Cr.NS_ERROR_NOT_AVAILABLE);
      return;
    }

    // Show the selection UI.
    function show() {
      log("show");
      // Create a device selection UI panel.
      if (!this._uiPanel) {
        this._createUIPanel();
      }

      // Setup the callback that will be fired when the device is selected.
      let responsed = false;
      let self = this;
      this._selectDevice = function(aDevice) {
        log("Select " + aDevice.name + "(" + aDevice.id + ")");
        aRequest.select(aDevice);
        // Close the popup
        responsed = true;
        self._uiPanel.hidePopup();
      };

      // Setup callback that will be called when the panel is dismissed.
      // This will be fired when user click somewhere out of the popup panel.
      this._uiPanel.addEventListener("popuphidden", function onPopupShown() {
        self._uiPanel.removeEventListener("popuphidden", onPopupShown);
        // If it's not caused by _selectDevice, then it's canceled by user.
        if (!responsed) {
          log("Dismiss by user");
          // Note: We can not put NS_OK here because it will lead a aseertion
          // failure in Promise::MaybeReject. The NS_OK is defined to zero,
          // so the MOZ_ASSERT(NS_FAILED(NS_OK)) will fail.
          aRequest.cancel(Cr.NS_ERROR_ABORT);
        }
      });

      // Show the UI at the window's center.
      let anchor = this._browserWindow;
      this._uiPanel.hidden = false;

      // ??? Does it need to do other calculation for zoom-in ???
      this._uiPanel.openPopupAtScreen(
        anchor.screenX + (anchor.outerWidth - UI_WIDTH) / 2,
        anchor.screenY + (anchor.outerHeight - UI_HEIGHT) / 2,
        true);
    }

    // Fire callback function when the page is active again.
    function onReactivate(aCallback) {
      let browser = this._browser;
      let tabbrowser = browser.getTabBrowser();
      let tab = tabbrowser.getTabForBrowser(browser);

      function callbackWrapper() {
        browser.removeEventListener("focus", callbackWrapper);
        tab.removeEventListener("TabSelect", callbackWrapper);
        aCallback();
      }

      browser.addEventListener("focus", callbackWrapper);
      tab.addEventListener("TabSelect", callbackWrapper);
    }

    // If the page or the window is inactive, then pend the UI until they are
    // active again.
    if (!this._isActive) {
      log("Show selection UI until the page is active.");
      let reactivate = onReactivate.bind(this);
      let callback = show.bind(this);
      reactivate(callback);
      return;
    }

    (show.bind(this))();
  },

  _loadDevices: function() {
    let deviceManager = Cc["@mozilla.org/presentation-device/manager;1"]
                      .getService(Ci.nsIPresentationDeviceManager);
    let devices = deviceManager.getAvailableDevices().QueryInterface(Ci.nsIArray);
    let list = [];
    for (let i = 0; i < devices.length; i++) {
      let device = devices.queryElementAt(i, Ci.nsIPresentationDevice);
      list.push(device);
    }

    return list;
  },

  /*
   * Utils to create and interact with the device selection UI.
   */
  // Create a panel UI attaching to the requesting browser for device selection.
  _createUIPanel: function() {
    log("_createUIPanel");
    let mainPopupSet = this._chromeDocument.getElementById(MAIN_POPUPSET);
    this._uiPanel = this._createPanel(PANEL_ID);
    this._uiFrame = this._createIframe(PANEL_UI_URL);
    this._uiPanel.appendChild(this._uiFrame);

    // Refresh the menu every time when the popup is prompted.
    let self = this;
    this._uiPanel.addEventListener("popupshowing", function() {
      self._refreshMenu();
    });

    mainPopupSet.appendChild(this._uiPanel);
  },

  _createPanel: function(aId) {
    let panel = this._chromeDocument.createElement("panel");
    let properties = {
      id: aId,
      type: "autocomplete",
      position: "after_start",
      hidden: true,
      noautofocus: true,
      orient: "vertical",
    };
    for (let p in properties) {
      panel.setAttribute(p, properties[p]);
    }
    return panel;
  },

  _createIframe: function(aSrc) {
    let iframe = this._chromeDocument.createElement("iframe");
    let properties = {
      src: aSrc,
      flex: 2,
      type: "chrome",
      marginwidth: 0,
      marginheight: 0,
      width: UI_WIDTH,
      height: UI_HEIGHT,
    };
    for (var p in properties) {
      iframe.setAttribute(p, properties[p]);
    }

    // Ask the devices information when the page is loaded.
    let self = this;
    iframe.addEventListener("DOMContentLoaded", function() {
      // Set the title of the prompt UI
      self._uiTitle.innerText = GetString("presentation.title");
      // Refrsh the descript and the device selection menu of the prompt UI
      self._refreshMenu();
    }, false);

    return iframe;
  },

  _refreshMenu: function() {
    log("_refreshMenu");

    if (!this._uiTitle) {
      log("The DOM content is not initialized.");
      return;
    }

    // Refresh the description of the prompt UI
    // because it shows the domain name of the requesting URL.
    this._uiDescription.innerText =
      GetString("presentation.message", this._domainName);

    // Remove the items created last time in the selection menu.
    while (this._uiList.lastChild) {
      this._uiList.removeChild(this._uiList.lastChild);
    }

    // Load the devices into the menu
    let self = this;
    for (let device of this._devices) {
      let item = self._uiDocument.createElement("li");
      let link = self._uiDocument.createElement("a");
      let text = self._uiDocument.createTextNode(device.name);
      link.appendChild(text);
      // Set the callback when user selects a device.
      link.onclick = function() {
        self._selectDevice(device);
      };
      item.appendChild(link);
      this._uiList.appendChild(item);
    }
  },

  /*
   * The following is used to get the elements in menu.html.
   * If the layout of menu.html is changed, then we just need to modify the
   * following selectors here.
   */
  get _uiDocument() {
    return this._uiFrame.contentDocument;
  },

  get _uiTitle() {
    return this._uiDocument.querySelector("#menu-header > .header-title-container > h1");
  },

  get _uiDescription() {
    return this._uiDocument.querySelector("#menu-header > p");
  },

  get _uiList() {
    return this._uiDocument.querySelector("#menu-container ul");
  },

  /*
   * Utils for the browser which starts the request.
   */
  _getBrowserForRequest: function (aRequest) {
    return aRequest.chromeEventHandler;
  },

  get _browserWindow() {
    return this._browser.ownerDocument.defaultView;
  },

  get _chromeWindow() {
    return this._browser.ownerGlobal;
  },

  get _chromeDocument() {
    return this._browser.ownerDocument;
  },

  get _currentUrl() {
    return this._browser.currentURI.spec;
  },

  get _domainName() {
    let name;
    let strs = this._currentUrl.split('/');
    for (let i = 1 ; i < strs.length ; i++) { // strs[0] is protocol name.
      if (strs[i]) {  // Find the first string after '/'.
        name = strs[i];
        break;
      }
    }
    return name;
  },

  // Return true if the tab and window are both selected/using.
  // Otherwise, return false.
  get _isActive() {
    let fm = Cc["@mozilla.org/focus-manager;1"].getService(Ci.nsIFocusManager);
    return (fm.activeWindow == this._browserWindow) && // Check window is active.
           this._browser.docShellIsActive; // Check browser is active.
  },
};
