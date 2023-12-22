// META: global=window,dedicatedworker
// META: script=/webcodecs/video-encoder-utils.js

promise_test(async t => {
  const config = {
    codec: 'vp8',
    width: 1280,
    height: 720,
    bitrate: 5000000,
    bitrateMode: 'constant',
    framerate: 25,
    latencyMode: 'realtime',
    contentHint: 'text',
  };

  let support;
  let supported = false;
  try {
    support = await VideoEncoder.isConfigSupported(config);
    supported = support.supported;
  } catch (e) {
  }
  assert_implements_optional(
      supported, 'Unsupported config: ' + JSON.stringify(config));

  assert_equals(support.supported, true);

  let new_config = support.config;
  assert_equals(new_config.codec, config.codec);
  assert_equals(new_config.contentHint, 'text');
}, 'Test that contentHint is recognized by VideoEncoder');
