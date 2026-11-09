(function () {
  "use strict";

  var LOGO_SRC = "../shared/logo.svg";
  var ROOT_ID = "coJoinOverlay";
  var MAX_STATUS = 96;

  var STATUS = {
    Connecting: "Connecting to server",
    Authenticating: "Authenticating",
    LoadingWorld: "Loading into world",
    FlyOver: "Welcome to Commonwealth Online",
    Spawning: "Entering the Commonwealth",
    Ready: "",
    Failed: "Join failed",
    Cancelled: "Join cancelled"
  };

  var HIDDEN_STATES = { Ready: true, Failed: true, Cancelled: true };

  var root = null;
  var state = null;

  function has(target, key) {
    return Object.prototype.hasOwnProperty.call(target, key);
  }

  function esc(text) {
    return String(text == null ? "" : text)
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;")
      .replace(/'/g, "&#39;");
  }

  function clampUnit(value) {
    var number = Number(value);
    if (!isFinite(number) || number <= 0) {
      return 0;
    }
    return number >= 1 ? 1 : number;
  }

  function cleanStatus(value) {
    return String(value == null ? "" : value)
      .replace(/[^\x20-\x7E]/g, "")
      .trim()
      .slice(0, MAX_STATUS);
  }

  function statusFor(name, override) {
    var custom = typeof override === "string" ? cleanStatus(override) : "";
    if (custom !== "") {
      return custom;
    }
    return has(STATUS, name) ? STATUS[name] : "";
  }

  function normalize(raw) {
    var next = raw;
    if (typeof raw === "string") {
      try {
        next = JSON.parse(raw);
      } catch (err) {
        return null;
      }
    }
    if (!next || typeof next !== "object" || Array.isArray(next)) {
      return null;
    }
    var name = typeof next.state === "string" ? next.state : "";
    if (!has(STATUS, name)) {
      return null;
    }
    return {
      state: name,
      status: statusFor(name, next.status),
      progress: clampUnit(next.progress),
      visible: next.visible !== false,
      dismissible: next.dismissible === true
    };
  }

  function isVisible(candidate) {
    if (!candidate || typeof candidate !== "object") {
      return false;
    }
    if (candidate.visible === false) {
      return false;
    }
    return has(STATUS, candidate.state) && !HIDDEN_STATES[candidate.state];
  }

  function render(candidate) {
    if (!isVisible(candidate)) {
      return "";
    }
    var line = statusFor(candidate.state, candidate.status);
    var width = Math.round(clampUnit(candidate.progress) * 100);
    return (
      '<div class="co-join-overlay" role="status" aria-live="polite">' +
      '<img class="co-join-overlay__logo" src="' +
      LOGO_SRC +
      '" alt="Commonwealth Online" width="220" height="54">' +
      '<p class="co-join-overlay__status">' +
      esc(line) +
      "</p>" +
      '<span class="co-join-overlay__track" aria-hidden="true">' +
      '<span class="co-join-overlay__fill" style="width:' +
      width +
      '%"></span>' +
      "</span>" +
      "</div>"
    );
  }

  function hideRoot() {
    if (!root) {
      return;
    }
    root.innerHTML = "";
    root.hidden = true;
    if (root.setAttribute) {
      root.setAttribute("hidden", "");
    }
  }

  function showRoot() {
    if (!root) {
      return;
    }
    root.hidden = false;
    if (root.removeAttribute) {
      root.removeAttribute("hidden");
    }
  }

  function watchLogo() {
    if (!root || !root.querySelector) {
      return;
    }
    var logo = root.querySelector("img");
    if (logo && logo.addEventListener) {
      logo.addEventListener("error", function () {
        logo.style.display = "none";
      });
    }
  }

  function paint() {
    if (!root) {
      return;
    }
    if (!isVisible(state)) {
      hideRoot();
      return;
    }
    root.innerHTML = render(state);
    showRoot();
    watchLogo();
  }

  function setState(raw) {
    var next = normalize(raw);
    if (!next) {
      return false;
    }
    state = next;
    paint();
    return true;
  }

  function hide() {
    state = null;
    hideRoot();
    return true;
  }

  function init() {
    if (typeof document === "undefined" || !document.getElementById) {
      return;
    }
    root = document.getElementById(ROOT_ID);
    if (!root) {
      return;
    }
    paint();
  }

  if (typeof window !== "undefined") {
    window.coJoinOverlaySetState = setState;
    window.coJoinOverlayHide = hide;
    window.coJoinOverlayModel = {
      ROOT_ID: ROOT_ID,
      LOGO_SRC: LOGO_SRC,
      STATUS: STATUS,
      HIDDEN_STATES: HIDDEN_STATES,
      normalize: normalize,
      isVisible: isVisible,
      statusFor: statusFor,
      render: render,
      setState: setState,
      hide: hide
    };
  }

  if (typeof document !== "undefined" && document.addEventListener) {
    if (document.readyState === "loading") {
      document.addEventListener("DOMContentLoaded", init);
    } else {
      init();
    }
  }
})();
