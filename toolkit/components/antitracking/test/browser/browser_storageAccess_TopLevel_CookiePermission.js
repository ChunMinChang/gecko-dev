add_task(async _ => {
  PermissionTestUtils.add(
    TEST_4TH_PARTY_PAGE,
    "cookie",
    Services.perms.ALLOW_ACTION
  );
});

add_task(async function testCookiePermissionRequestStorageAccessUnderSite() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["dom.storage_access.enabled", true],
      ["dom.storage_access.forward_declared.enabled", true],
      [
        "network.cookie.cookieBehavior",
        BEHAVIOR_REJECT_TRACKER_AND_PARTITION_FOREIGN,
      ],
      ["dom.storage_access.auto_grants", false],
      ["dom.storage_access.max_concurrent_auto_grants", 1],
    ],
  });
  let tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    url: TEST_4TH_PARTY_PAGE,
  });
  let browser = tab.linkedBrowser;
  await SpecialPowers.spawn(browser, [TEST_DOMAIN], async tp => {
    SpecialPowers.wrap(content.document).notifyUserGestureActivation();
    var p = content.document.requestStorageAccessUnderSite(tp);
    try {
      await p;
      ok(true, "Must resolve.");
    } catch {
      ok(false, "Must not reject.");
    }
  });
  await BrowserTestUtils.removeTab(tab);
  await SpecialPowers.popPermissions();
});

add_task(async function testCookiePermissionCompleteStorageAccessRequest() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["dom.storage_access.enabled", true],
      ["dom.storage_access.forward_declared.enabled", true],
      [
        "network.cookie.cookieBehavior",
        BEHAVIOR_REJECT_TRACKER_AND_PARTITION_FOREIGN,
      ],
      ["dom.storage_access.auto_grants", false],
      ["dom.storage_access.max_concurrent_auto_grants", 1],
    ],
  });
  let tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    url: TEST_TOP_PAGE,
  });
  let browser = tab.linkedBrowser;
  await SpecialPowers.spawn(browser, [TEST_4TH_PARTY_DOMAIN], async tp => {
    await SpecialPowers.pushPermissions([
      {
        type: "AllowStorageAccessRequest^http://example.com",
        allow: Services.perms.ALLOW_ACTION,
        context: content.document,
      },
    ]);
    SpecialPowers.wrap(content.document).notifyUserGestureActivation();
    var p = content.document.completeStorageAccessRequestFromSite(tp);
    try {
      await p;
      ok(true, "Must resolve.");
    } catch {
      ok(false, "Must not reject.");
    }
  });
  await BrowserTestUtils.removeTab(tab);
  await SpecialPowers.popPermissions();
});

add_task(async _ => {
  Services.perms.removeAll();
});

add_task(async _ => {
  PermissionTestUtils.add(
    TEST_4TH_PARTY_PAGE,
    "cookie",
    Services.perms.DENY_ACTION
  );
});

add_task(
  async function testCookiePermissionRejectRequestStorageAccessUnderSite() {
    await SpecialPowers.pushPrefEnv({
      set: [
        ["dom.storage_access.enabled", true],
        ["dom.storage_access.forward_declared.enabled", true],
        [
          "network.cookie.cookieBehavior",
          BEHAVIOR_REJECT_TRACKER_AND_PARTITION_FOREIGN,
        ],
        ["dom.storage_access.auto_grants", false],
        ["dom.storage_access.max_concurrent_auto_grants", 1],
      ],
    });
    let tab = await BrowserTestUtils.openNewForegroundTab({
      gBrowser,
      url: TEST_4TH_PARTY_PAGE,
    });
    let browser = tab.linkedBrowser;
    await SpecialPowers.spawn(browser, [TEST_DOMAIN], async tp => {
      SpecialPowers.wrap(content.document).notifyUserGestureActivation();
      var p = content.document.requestStorageAccessUnderSite(tp);
      try {
        await p;
        ok(false, "Must not resolve.");
      } catch {
        ok(true, "Must reject.");
      }
    });
    await BrowserTestUtils.removeTab(tab);
    await SpecialPowers.popPermissions();
  }
);

add_task(
  async function testCookiePermissionRejectCompleteStorageAccessRequest() {
    await SpecialPowers.pushPrefEnv({
      set: [
        ["dom.storage_access.enabled", true],
        ["dom.storage_access.forward_declared.enabled", true],
        [
          "network.cookie.cookieBehavior",
          BEHAVIOR_REJECT_TRACKER_AND_PARTITION_FOREIGN,
        ],
        ["dom.storage_access.auto_grants", false],
        ["dom.storage_access.max_concurrent_auto_grants", 1],
      ],
    });
    let tab = await BrowserTestUtils.openNewForegroundTab({
      gBrowser,
      url: TEST_TOP_PAGE,
    });
    let browser = tab.linkedBrowser;
    await SpecialPowers.spawn(browser, [TEST_4TH_PARTY_DOMAIN], async tp => {
      await SpecialPowers.pushPermissions([
        {
          type: "AllowStorageAccessRequest^http://example.com",
          allow: Services.perms.ALLOW_ACTION,
          context: content.document,
        },
      ]);
      SpecialPowers.wrap(content.document).notifyUserGestureActivation();
      var p = content.document.completeStorageAccessRequestFromSite(tp);
      try {
        await p;
        ok(false, "Must not resolve.");
      } catch {
        ok(true, "Must reject.");
      }
    });
    await BrowserTestUtils.removeTab(tab);
    await SpecialPowers.popPermissions();
  }
);

add_task(async () => {
  Services.perms.removeAll();
  await new Promise(resolve => {
    Services.clearData.deleteData(Ci.nsIClearDataService.CLEAR_ALL, () =>
      resolve()
    );
  });
});
