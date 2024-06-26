/* Any copyright is dedicated to the Public Domain.
  http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const SCREENSHOTS_EVENTS = [
  { category: "screenshots", method: "started", object: "toolbar_button" },
  { category: "screenshots", method: "canceled", object: "navigation" },
];

add_task(async function test() {
  await BrowserTestUtils.withNewTab(
    {
      gBrowser,
      url: SHORT_TEST_PAGE,
    },
    async browser => {
      await clearAllTelemetryEvents();

      let helper = new ScreenshotsHelper(browser);

      // click toolbar button so panel shows
      helper.triggerUIFromToolbar();
      await helper.waitForOverlay();

      await SpecialPowers.spawn(browser, [], () => {
        content.location.reload();
      });

      await helper.waitForOverlayClosed();

      await assertScreenshotsEvents(SCREENSHOTS_EVENTS);
    }
  );
});
