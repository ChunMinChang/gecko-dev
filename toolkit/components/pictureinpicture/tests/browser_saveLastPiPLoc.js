/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * This function tests that the browser saves the last location of size of
 * the PiP window and will open the next PiP window in the same location
 * with the size. It adjusts for aspect ratio by keeping the same height and
 * adjusting the width of the PiP window.
 */
async function doTest() {
  await BrowserTestUtils.withNewTab(
    {
      url: TEST_PAGE,
      gBrowser,
    },
    // Function to switch video source.
    async browser => {
      async function switchVideoSource(src) {
        let videoResized = BrowserTestUtils.waitForEvent(pipWin, "resize");
        await ContentTask.spawn(browser, { src }, async ({ src }) => {
          let doc = content.document;
          let video = doc.getElementById("with-controls");
          video.src = src;
        });
        await videoResized;
      }

      function getAvailScreenSize(screen) {
        let screenLeft = {},
          screenTop = {},
          screenWidth = {},
          screenHeight = {};
        screen.GetAvailRectDisplayPix(
          screenLeft,
          screenTop,
          screenWidth,
          screenHeight
        );

        // We have to divide these dimensions by the CSS scale factor for the
        // display in order for the video to be positioned correctly on displays
        // that are not at a 1.0 scaling.
        let scaleFactor =
          screen.contentsScaleFactor / screen.defaultCSSScaleFactor;
        screenWidth.value *= scaleFactor;
        screenHeight.value *= scaleFactor;
        screenLeft.value *= scaleFactor;
        screenTop.value *= scaleFactor;

        return [
          screenLeft.value,
          screenTop.value,
          screenWidth.value,
          screenHeight.value,
        ];
      }

      let screen = Cc["@mozilla.org/gfx/screenmanager;1"]
        .getService(Ci.nsIScreenManager)
        .screenForRect(1, 1, 1, 1);

      let [defaultX, defaultY, defaultWidth, defaultHeight] =
        getAvailScreenSize(screen);

      // Default size of PiP window
      let rightEdge = defaultX + defaultWidth;
      let bottomEdge = defaultY + defaultHeight;

      // tab height
      // Used only for Linux as the PiP window has a tab
      let tabHeight = 35;

      // clear already saved information
      clearSavedPosition();

      // Open PiP
      let pipWin = await triggerPictureInPicture(browser, "with-controls");
      ok(pipWin, "Got Picture-in-Picture window.");

      let defaultPiPWidth = pipWin.outerWidth;
      let defaultPiPHeight = pipWin.outerHeight;

      // Check that it is opened at default location
      isfuzzy(
        pipWin.screenX,
        rightEdge - defaultPiPWidth,
        ACCEPTABLE_DIFFERENCE,
        "Default PiP X location"
      );
      if (AppConstants.platform == "linux") {
        isfuzzy(
          pipWin.screenY,
          bottomEdge - defaultPiPHeight - tabHeight,
          ACCEPTABLE_DIFFERENCE,
          "Default PiP Y location"
        );
      } else {
        isfuzzy(
          pipWin.screenY,
          bottomEdge - defaultPiPHeight,
          ACCEPTABLE_DIFFERENCE,
          "Default PiP Y location"
        );
      }
      isfuzzy(
        pipWin.outerHeight,
        defaultPiPHeight,
        ACCEPTABLE_DIFFERENCE,
        "Default PiP height"
      );
      isfuzzy(
        pipWin.outerWidth,
        defaultPiPWidth,
        ACCEPTABLE_DIFFERENCE,
        "Default PiP width"
      );

      let top = defaultY;
      let left = defaultX;
      pipWin.moveTo(left, top);
      let height = pipWin.outerHeight / 2;
      let width = pipWin.outerWidth / 2;
      pipWin.resizeTo(width, height);

      // CLose first PiP window and open another
      await ensureMessageAndClosePiP(browser, "with-controls", pipWin, true);
      pipWin = await triggerPictureInPicture(browser, "with-controls");
      ok(pipWin, "Got Picture-in-Picture window.");

      // PiP is opened at 0, 0 with size 1/4 default width and 1/4 default height
      isfuzzy(
        pipWin.screenX,
        left,
        ACCEPTABLE_DIFFERENCE,
        "Opened at last X location"
      );
      isfuzzy(
        pipWin.screenY,
        top,
        ACCEPTABLE_DIFFERENCE,
        "Opened at last Y location"
      );
      isfuzzy(
        pipWin.outerHeight,
        height,
        ACCEPTABLE_DIFFERENCE,
        "Opened with 1/2 default height"
      );
      isfuzzy(
        pipWin.outerWidth,
        width,
        ACCEPTABLE_DIFFERENCE,
        "Opened with 1/2 default width"
      );

      // Mac and Linux did not allow moving to coordinates offscreen so this
      // test is skipped on those platforms
      if (AppConstants.platform == "win") {
        // Move to -1111, -1111 and adjust size to 1/4 width and 1/4 height
        left = -11111;
        top = -11111;
        pipWin.moveTo(left, top);
        pipWin.resizeTo(pipWin.outerWidth / 4, pipWin.outerHeight / 4);

        await ensureMessageAndClosePiP(browser, "with-controls", pipWin, true);
        pipWin = await triggerPictureInPicture(browser, "with-controls");
        ok(pipWin, "Got Picture-in-Picture window.");

        // because the coordinates are off screen, the default size and location will be used
        isfuzzy(
          pipWin.screenX,
          rightEdge - defaultPiPWidth,
          ACCEPTABLE_DIFFERENCE,
          "Opened at default X location"
        );
        isfuzzy(
          pipWin.screenY,
          bottomEdge - defaultPiPHeight,
          ACCEPTABLE_DIFFERENCE,
          "Opened at default Y location"
        );
        isfuzzy(
          pipWin.outerWidth,
          defaultPiPWidth,
          ACCEPTABLE_DIFFERENCE,
          "Opened at default PiP width"
        );
        isfuzzy(
          pipWin.outerHeight,
          defaultPiPHeight,
          ACCEPTABLE_DIFFERENCE,
          "Opened at default PiP height"
        );
      }

      // Linux doesn't handle switching the video source well and it will
      // cause the tests to failed in unexpected ways. Possibly caused by
      // bug 1594223 https://bugzilla.mozilla.org/show_bug.cgi?id=1594223
      if (AppConstants.platform != "linux") {
        // Save width and height for when aspect ratio is changed
        height = pipWin.outerHeight;
        width = pipWin.outerWidth;

        left = 200;
        top = 100;
        pipWin.moveTo(left, top);

        // Now switch the video so the video ratio is different
        await switchVideoSource("test-video-cropped.mp4");
        await ensureMessageAndClosePiP(browser, "with-controls", pipWin, true);
        pipWin = await triggerPictureInPicture(browser, "with-controls");
        ok(pipWin, "Got Picture-in-Picture window.");

        isfuzzy(
          pipWin.screenX,
          left,
          ACCEPTABLE_DIFFERENCE,
          "Opened at last X location"
        );
        isfuzzy(
          pipWin.screenY,
          top,
          ACCEPTABLE_DIFFERENCE,
          "Opened at last Y location"
        );
        isfuzzy(
          pipWin.outerHeight,
          height,
          ACCEPTABLE_DIFFERENCE,
          "Opened height with previous width"
        );
        isfuzzy(
          pipWin.outerWidth,
          height * (pipWin.outerWidth / pipWin.outerHeight),
          ACCEPTABLE_DIFFERENCE,
          "Width is changed to adjust for aspect ration"
        );

        left = 300;
        top = 300;
        pipWin.moveTo(left, top);
        pipWin.resizeTo(defaultPiPWidth / 2, defaultPiPHeight / 2);

        // Save height for when aspect ratio is changed
        height = pipWin.outerHeight;

        // Now switch the video so the video ratio is different
        await switchVideoSource("test-video.mp4");
        await ensureMessageAndClosePiP(browser, "with-controls", pipWin, true);
        pipWin = await triggerPictureInPicture(browser, "with-controls");
        ok(pipWin, "Got Picture-in-Picture window.");

        isfuzzy(
          pipWin.screenX,
          left,
          ACCEPTABLE_DIFFERENCE,
          "Opened at last X location"
        );
        isfuzzy(
          pipWin.screenY,
          top,
          ACCEPTABLE_DIFFERENCE,
          "Opened at last Y location"
        );
        isfuzzy(
          pipWin.outerHeight,
          height,
          ACCEPTABLE_DIFFERENCE,
          "Opened with previous height"
        );
        isfuzzy(
          pipWin.outerWidth,
          height * (pipWin.outerWidth / pipWin.outerHeight),
          ACCEPTABLE_DIFFERENCE,
          "Width is changed to adjust for aspect ration"
        );
      }

      // Move so that part of PiP is off screen (bottom right)

      left = rightEdge - Math.round((3 * pipWin.outerWidth) / 4);
      top = bottomEdge - Math.round((3 * pipWin.outerHeight) / 4);

      let movePromise = BrowserTestUtils.waitForEvent(
        pipWin.windowRoot,
        "MozUpdateWindowPos"
      );
      pipWin.moveTo(left, top);
      await movePromise;

      await ensureMessageAndClosePiP(browser, "with-controls", pipWin, true);
      pipWin = await triggerPictureInPicture(browser, "with-controls");
      ok(pipWin, "Got Picture-in-Picture window.");

      // Redefine top and left to where the PiP windop will open
      left = rightEdge - pipWin.outerWidth;
      top = bottomEdge - pipWin.outerHeight;

      // await new Promise(r => setTimeout(r, 5000));
      // PiP is opened bottom right but not off screen
      isfuzzy(
        pipWin.screenX,
        left,
        ACCEPTABLE_DIFFERENCE,
        "Opened at last X location but shifted back on screen"
      );
      if (AppConstants.platform == "linux") {
        isfuzzy(
          pipWin.screenY,
          top - tabHeight,
          ACCEPTABLE_DIFFERENCE,
          "Opened at last Y location but shifted back on screen"
        );
      } else {
        isfuzzy(
          pipWin.screenY,
          top,
          ACCEPTABLE_DIFFERENCE,
          "Opened at last Y location but shifted back on screen"
        );
      }

      // Move so that part of PiP is off screen (top left)
      left = defaultX - Math.round(pipWin.outerWidth / 4);
      top = defaultY - Math.round(pipWin.outerHeight / 4);

      movePromise = BrowserTestUtils.waitForEvent(
        pipWin.windowRoot,
        "MozUpdateWindowPos"
      );
      pipWin.moveTo(left, top);
      await movePromise;

      await ensureMessageAndClosePiP(browser, "with-controls", pipWin, true);
      pipWin = await triggerPictureInPicture(browser, "with-controls");
      ok(pipWin, "Got Picture-in-Picture window.");

      // PiP is opened top left on screen
      isfuzzy(
        pipWin.screenX,
        defaultX,
        ACCEPTABLE_DIFFERENCE,
        "Opened at last X location but shifted back on screen"
      );
      isfuzzy(
        pipWin.screenY,
        defaultY,
        ACCEPTABLE_DIFFERENCE,
        "Opened at last Y location but shifted back on screen"
      );

      if (AppConstants.platform != "linux") {
        // test that if video is on right edge and new video with smaller width
        // is opened next, it is still on the right edge
        left = rightEdge - pipWin.outerWidth;
        top = Math.round(bottomEdge / 4);

        pipWin.moveTo(left, top);

        // Used to ensure that video width decreases for next PiP window
        width = pipWin.outerWidth;
        isfuzzy(
          pipWin.outerWidth + pipWin.screenX,
          rightEdge,
          ACCEPTABLE_DIFFERENCE,
          "Video is on right edge before video is changed"
        );

        // Now switch the video so the video width is smaller
        await switchVideoSource("test-video-cropped.mp4");
        await ensureMessageAndClosePiP(browser, "with-controls", pipWin, true);
        pipWin = await triggerPictureInPicture(browser, "with-controls");
        ok(pipWin, "Got Picture-in-Picture window.");

        Assert.less(pipWin.outerWidth, width, "New video width is smaller");
        isfuzzy(
          pipWin.outerWidth + pipWin.screenX,
          rightEdge,
          ACCEPTABLE_DIFFERENCE,
          "Video is on right edge after video is changed"
        );
      }

      await ensureMessageAndClosePiP(browser, "with-controls", pipWin, true);
    }
  );
}

add_task(async function test_pip_save_last_loc() {
  await doTest();
});

add_task(async function test_pip_save_last_loc_with_os_zoom() {
  await SpecialPowers.pushPrefEnv({ set: [["ui.textScaleFactor", 120]] });
  await doTest();
});
