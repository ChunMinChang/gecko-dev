Components.utils.import("resource://gre/modules/Services.jsm");

var shutdownObs = {
  observe: function(subject, topic, data) {
    if (topic == 'ipc:content-shutdown') {
      dump("-------ipc:content-shutdown\n");
      sendAsyncMessage('content-shutdown', '');
    }
  }
};

Services.obs.addObserver(shutdownObs, 'ipc:content-shutdown', false);
