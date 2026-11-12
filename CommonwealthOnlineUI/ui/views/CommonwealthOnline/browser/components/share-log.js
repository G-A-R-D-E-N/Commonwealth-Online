(function () {
  "use strict";

  function create(options) {
    var root = options && options.root;
    var sendAction = options && options.sendAction;
    var button = root ? root.querySelector("#coShareLogButton") : null;
    var notice = root ? root.querySelector("#coShareLogNotice") : null;

    function setNotice(text, isError) {
      if (!notice) return;
      notice.textContent = text || "";
      notice.className = isError ? "co-share-log-notice co-share-log-notice--error" : "co-share-log-notice";
    }

    if (button && typeof sendAction === "function") {
      button.addEventListener("click", function () {
        setNotice("", false);
        sendAction("shareLog", {});
      });
    }

    return {
      show: function (event) {
        setNotice(event && event.message ? event.message : "", !(event && event.success));
      }
    };
  }

  window.CO_ShareLog = {
    create: create
  };
})();
