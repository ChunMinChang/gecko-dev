const SAVE_PER_SITE_PREF = "browser.download.lastDir.savePerSite";

function test_deleted_iframe(perSitePref, windowOptions = {}) {
  return async function () {
    await SpecialPowers.pushPrefEnv({
      set: [[SAVE_PER_SITE_PREF, perSitePref]],
    });
    let { DownloadLastDir } = ChromeUtils.importESModule(
      "resource://gre/modules/DownloadLastDir.sys.mjs"
    );

    let win = await BrowserTestUtils.openNewBrowserWindow(windowOptions);
    let tab = await BrowserTestUtils.openNewForegroundTab(
      win.gBrowser,
      "about:mozilla"
    );

    let doc = tab.linkedBrowser.contentDocument;
    let iframe = doc.createElement("iframe");
    doc.body.appendChild(iframe);

    ok(iframe.contentWindow, "iframe should have a window");
    let gDownloadLastDir = new DownloadLastDir(iframe.contentWindow);
    let cw = iframe.contentWindow;
    let promiseIframeWindowGone = new Promise(resolve => {
      Services.obs.addObserver(function obs(subject, topic) {
        if (subject == cw) {
          Services.obs.removeObserver(obs, topic);
          resolve();
        }
      }, "dom-window-destroyed");
    });
    iframe.remove();
    await promiseIframeWindowGone;
    cw = null;
    ok(!iframe.contentWindow, "Managed to destroy iframe");

    let someDir = "blah";
    try {
      someDir = await gDownloadLastDir.getFileAsync("http://www.mozilla.org/");
    } catch (ex) {
      ok(
        false,
        "Got an exception trying to get the directory where things should be saved."
      );
      console.error(ex);
    }
    // NB: someDir can legitimately be null here when set, hence the 'blah' workaround:
    isnot(
      someDir,
      "blah",
      "Should get a file even after the window was destroyed."
    );

    try {
      gDownloadLastDir.setFile("http://www.mozilla.org/", null);
    } catch (ex) {
      ok(
        false,
        "Got an exception trying to set the directory where things should be saved."
      );
      console.error(ex);
    }

    await BrowserTestUtils.closeWindow(win);
  };
}

add_task(test_deleted_iframe(false));
add_task(test_deleted_iframe(false));
add_task(test_deleted_iframe(true, { private: true }));
add_task(test_deleted_iframe(true, { private: true }));
