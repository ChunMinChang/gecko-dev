/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

"use strict";

/**
 * This test verifies that when Balrog advertises that an update should not
 * be downloaded in the background, it is not.
 */

function setup() {
  setupTestCommon();
  start_httpserver();
  setUpdateURL(gURLData + gHTTPHandlerPath);
  setUpdateChannel("test_channel");

  // Pretend that this is a background task.
  const bts = Cc["@mozilla.org/backgroundtasks;1"].getService(
    Ci.nsIBackgroundTasks
  );
  bts.overrideBackgroundTaskNameForTesting("test-task");
}
setup();

add_task(async function disableBackgroundUpdatesBackgroundTask() {
  let patches = getRemotePatchString({});
  let updateString = getRemoteUpdateString(
    { disableBackgroundUpdates: "true" },
    patches
  );
  gResponseBody = getRemoteUpdatesXMLString(updateString);

  let { updates } = await waitForUpdateCheck(true);
  let bestUpdate = await gAUS.selectUpdate(updates);
  let result = await gAUS.downloadUpdate(bestUpdate, false);
  Assert.equal(
    result,
    Ci.nsIApplicationUpdateService.DOWNLOAD_FAILURE_GENERIC,
    "Update should not download when disableBackgroundUpdates is specified " +
      "and we are in background task mode."
  );
});

add_task(async function finish() {
  stop_httpserver(doTestFinish);
});
