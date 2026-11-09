(function () {
  "use strict";

  function copyWithExecCommand(text) {
    if (typeof document === "undefined" || !document.body || typeof document.execCommand !== "function") {
      return false;
    }
    var area = null;
    try {
      area = document.createElement("textarea");
      area.value = text;
      area.setAttribute("readonly", "");
      area.style.position = "fixed";
      area.style.opacity = "0";
      document.body.appendChild(area);
      area.select();
      return document.execCommand("copy") === true;
    } catch (e) {
      return false;
    } finally {
      if (area && area.parentNode) {
        area.parentNode.removeChild(area);
      }
    }
  }

  function writeClipboard(text) {
    var clipboard = typeof navigator !== "undefined" ? navigator.clipboard : null;
    if (!clipboard || typeof clipboard.writeText !== "function") {
      return Promise.resolve(copyWithExecCommand(text));
    }
    return Promise.resolve()
      .then(function () { return clipboard.writeText(text); })
      .then(function () { return true; })
      .catch(function () { return copyWithExecCommand(text); });
  }

  function create(options) {
    var joinOverlay = options.joinOverlay;
    var button = options.button;
    var notice = options.notice;
    var onDismiss = options.onDismiss;
    var lines = [];

    function show(nextLines, isError) {
      lines = isError ? (nextLines || []) : [];
      if (button) {
        button.classList.toggle("hidden", lines.length === 0);
      }
      if (notice) {
        notice.textContent = "";
      }
    }

    function hide() {
      lines = [];
      if (button) button.classList.add("hidden");
      if (notice) notice.textContent = "";
    }

    function onClick(event) {
      if (event.target && event.target.closest && event.target.closest("#coCopyModpackButton")) {
        if (!lines.length) {
          return;
        }
        var pending = lines.join("\n");
        if (notice) {
          notice.textContent = "";
        }
        writeClipboard(pending).then(function (copied) {
          if (notice) {
            notice.textContent = copied ? "Copied to clipboard." : "Copy failed.";
          }
        });
        return;
      }
      if (onDismiss) onDismiss();
    }

    function bind() {
      if (joinOverlay) joinOverlay.addEventListener("click", onClick);
    }

    return { bind: bind, show: show, hide: hide };
  }

  window.CO_ModpackCopy = { create: create, writeClipboard: writeClipboard };
})();
