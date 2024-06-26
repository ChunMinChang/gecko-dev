"use strict";

const { ExtensionPermissions } = ChromeUtils.importESModule(
  "resource://gre/modules/ExtensionPermissions.sys.mjs"
);

AddonTestUtils.init(this);
AddonTestUtils.overrideCertDB();
AddonTestUtils.createAppInfo(
  "xpcshell@tests.mozilla.org",
  "XPCShell",
  "1",
  "42"
);

add_task(async function setup() {
  // Bug 1646182: Force ExtensionPermissions to run in rkv mode, the legacy
  // storage mode will run in xpcshell-legacy-ep.toml
  await ExtensionPermissions._uninit();

  Services.prefs.setBoolPref(
    "extensions.webextOptionalPermissionPrompts",
    false
  );
  registerCleanupFunction(() => {
    Services.prefs.clearUserPref("extensions.webextOptionalPermissionPrompts");
  });
  await AddonTestUtils.promiseStartupManager();
  AddonTestUtils.usePrivilegedSignatures = false;
});

async function test_migrated_permission_to_optional({ manifest_version }) {
  let id = "permission-upgrade@test";
  let extensionData = {
    manifest: {
      version: "1.0",
      browser_specific_settings: { gecko: { id } },
      permissions: [
        "webRequest",
        "tabs",
        "http://example.net/*",
        "http://example.com/*",
      ],
    },
    useAddonManager: "permanent",
  };

  function checkPermissions() {
    let policy = WebExtensionPolicy.getByID(id);
    ok(policy.hasPermission("webRequest"), "addon has webRequest permission");
    ok(policy.hasPermission("tabs"), "addon has tabs permission");
    ok(
      policy.canAccessURI(Services.io.newURI("http://example.net/")),
      "addon has example.net host permission"
    );
    ok(
      policy.canAccessURI(Services.io.newURI("http://example.com/")),
      "addon has example.com host permission"
    );
    ok(
      !policy.canAccessURI(Services.io.newURI("http://other.com/")),
      "addon does not have other.com host permission"
    );
  }

  let extension = ExtensionTestUtils.loadExtension(extensionData);
  await extension.startup();
  checkPermissions();

  extensionData.manifest.manifest_version = manifest_version;

  // Move to using optional permission
  extensionData.manifest.version = "2.0";
  extensionData.manifest.permissions = ["tabs"];

  // The ExtensionTestCommon.generateFiles() test helper will normalize (move)
  // host permissions into the `permissions` key for the MV2 test.
  extensionData.manifest.host_permissions = ["http://example.net/*"];

  extensionData.manifest.optional_permissions = [
    "webRequest",
    "http://example.com/*",
    "http://other.com/*",
  ];

  // Restart the addon manager to flush the AddonInternal instance created
  // when installing the addon above.  See bug 1622117.
  await AddonTestUtils.promiseRestartManager();
  await extension.upgrade(extensionData);

  equal(extension.version, "2.0", "Expected extension version");
  checkPermissions();

  await extension.unload();
}

add_task(function test_migrated_permission_to_optional_mv2() {
  return test_migrated_permission_to_optional({ manifest_version: 2 });
});

// Test migration of mv2 (required) to mv3 (optional) host permissions.
add_task(function test_migrated_permission_to_optional_mv3() {
  return test_migrated_permission_to_optional({ manifest_version: 3 });
});

// This tests that settings are removed if a required permission is removed.
// We use two settings APIs to make sure the one we keep permission to is not
// removed inadvertantly.
add_task(async function test_required_permissions_removed() {
  function cacheIsEnabled() {
    return (
      Services.prefs.getBoolPref("browser.cache.disk.enable") &&
      Services.prefs.getBoolPref("browser.cache.memory.enable")
    );
  }

  let extData = {
    background() {
      if (browser.browserSettings) {
        browser.browserSettings.cacheEnabled.set({ value: false });
      }
      browser.privacy.services.passwordSavingEnabled.set({ value: false });
    },
    manifest: {
      browser_specific_settings: { gecko: { id: "pref-test@test" } },
      permissions: ["tabs", "browserSettings", "privacy", "http://test.com/*"],
    },
    useAddonManager: "permanent",
  };
  let extension = ExtensionTestUtils.loadExtension(extData);
  ok(
    Services.prefs.getBoolPref("signon.rememberSignons"),
    "privacy setting intial value as expected"
  );
  await extension.startup();
  ok(!cacheIsEnabled(), "setting is set after startup");

  extData.manifest.permissions = ["tabs"];
  extData.manifest.optional_permissions = ["privacy"];
  await extension.upgrade(extData);
  ok(cacheIsEnabled(), "setting is reset after upgrade");
  ok(
    !Services.prefs.getBoolPref("signon.rememberSignons"),
    "privacy setting is still set after upgrade"
  );

  await extension.unload();
});

// This tests that settings are removed if a granted permission is removed.
// We use two settings APIs to make sure the one we keep permission to is not
// removed inadvertantly.
add_task(async function test_granted_permissions_removed() {
  function cacheIsEnabled() {
    return (
      Services.prefs.getBoolPref("browser.cache.disk.enable") &&
      Services.prefs.getBoolPref("browser.cache.memory.enable")
    );
  }

  let extData = {
    async background() {
      browser.test.onMessage.addListener(async msg => {
        await browser.permissions.request({ permissions: msg.permissions });
        if (browser.browserSettings) {
          browser.browserSettings.cacheEnabled.set({ value: false });
        }
        browser.privacy.services.passwordSavingEnabled.set({ value: false });
        browser.test.sendMessage("done");
      });
    },
    // "tabs" is never granted, it is included to exercise the removal code
    // that called during the upgrade.
    manifest: {
      browser_specific_settings: { gecko: { id: "pref-test@test" } },
      optional_permissions: [
        "tabs",
        "browserSettings",
        "privacy",
        "http://test.com/*",
      ],
    },
    useAddonManager: "permanent",
  };
  let extension = ExtensionTestUtils.loadExtension(extData);
  ok(
    Services.prefs.getBoolPref("signon.rememberSignons"),
    "privacy setting intial value as expected"
  );
  await extension.startup();
  await withHandlingUserInput(extension, async () => {
    extension.sendMessage({ permissions: ["browserSettings", "privacy"] });
    await extension.awaitMessage("done");
  });
  ok(!cacheIsEnabled(), "setting is set after startup");

  extData.manifest.permissions = ["privacy"];
  delete extData.manifest.optional_permissions;
  await extension.upgrade(extData);
  ok(cacheIsEnabled(), "setting is reset after upgrade");
  ok(
    !Services.prefs.getBoolPref("signon.rememberSignons"),
    "privacy setting is still set after upgrade"
  );

  await extension.unload();
});
