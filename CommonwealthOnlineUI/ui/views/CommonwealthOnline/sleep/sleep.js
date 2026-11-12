(function () {
  "use strict";

  var hours = 8;
  var maxHours = 24;
  var elapsed = 0;
  var phase = "closed";
  var token = 0;
  var submitted = false;
  var previousFocus = null;
  var root = document.getElementById("coSleep");
  var picker = document.getElementById("coSleepPicker");
  var less = document.getElementById("coSleepLess");
  var more = document.getElementById("coSleepMore");
  var start = document.getElementById("coSleepStart");
  var cancel = document.getElementById("coSleepCancel");
  var wake = document.getElementById("coSleepWake");
  var clock = document.getElementById("coSleepClock");
  var clockHand = document.getElementById("coSleepClockHand");
  var remaining = document.getElementById("coSleepRemaining");

  function send(op) {
    if (typeof window.coSleepAction === "function") {
      if (op === "sleep") {
        if (submitted) return;
        submitted = true;
      }
      window.coSleepAction(JSON.stringify({ op: op, hours: hours, token: token }));
    }
  }

  function render() {
    var choosing = phase === "choose";
    root.hidden = !choosing;
    clock.hidden = phase !== "resting";
    picker.hidden = !choosing;
    start.hidden = !choosing;
    less.disabled = hours <= 1;
    more.disabled = hours >= maxHours;
    document.getElementById("coSleepHours").textContent = hours;
    document.getElementById("coSleepUnit").textContent = hours === 1 ? "hour" : "hours";
    clockHand.style.transform = "rotate(" + (elapsed * 30) + "deg)";
    var left = Math.max(0, Math.ceil(hours - elapsed));
    remaining.textContent = left + (left === 1 ? " hour remaining" : " hours remaining");
  }

  function change(delta) {
    if (phase !== "choose") return;
    hours = Math.max(1, Math.min(maxHours, hours + delta));
    render();
  }

  window.coSleepSetState = function (state) {
    if (!state || ["closed", "choose", "resting"].indexOf(state.phase) === -1) return;
    var previousPhase = phase;
    phase = state.phase;
    submitted = false;
    token = Number.isSafeInteger(state.token) && state.token >= 0 ? state.token : 0;
    maxHours = Number.isInteger(state.maxHours) ? Math.max(1, Math.min(24, state.maxHours)) : 24;
    hours = Number.isInteger(state.hours) ? Math.max(1, Math.min(maxHours, state.hours)) : Math.min(8, maxHours);
    elapsed = Number.isFinite(state.elapsed) ? Math.max(0, Math.min(hours, state.elapsed)) : 0;
    render();
    if (previousPhase === "closed" && phase !== "closed") {
      previousFocus = document.activeElement;
      if (phase === "choose") start.focus();
      else if (phase === "resting") wake.focus();
    } else if (previousPhase !== "closed" && phase === "closed" && previousFocus) {
      previousFocus.focus();
      previousFocus = null;
    } else if (previousPhase !== phase && phase === "choose") {
      start.focus();
    } else if (previousPhase !== phase && phase === "resting") {
      wake.focus();
    }
  };

  less.addEventListener("click", function () { change(-1); });
  more.addEventListener("click", function () { change(1); });
  start.addEventListener("click", function () { if (phase === "choose") send("sleep"); });
  cancel.addEventListener("click", function () { if (phase !== "closed") send("cancel"); });
  wake.addEventListener("click", function () { if (phase === "resting") send("cancel"); });
  window.coSleepController = function (button, buttonPhase) {
    if (buttonPhase !== "down" || phase === "closed") return;
    if (phase === "resting") { if (button === "a" || button === "b") send("cancel"); return; }
    if (button === "b") send("cancel");
    else if (phase === "choose" && button === "a") send("sleep");
    else if (button === "dpad_left") change(-1);
    else if (button === "dpad_right") change(1);
  };
  window.addEventListener("keydown", function (event) {
    if (phase === "resting") {
      if (event.repeat) { event.preventDefault(); return; }
      if (event.key === "Escape") { event.preventDefault(); send("cancel"); }
      return;
    }
    if (phase !== "choose") return;
    if (event.key === "Tab") {
      var controls = [less, more, start, cancel].filter(function (button) { return !button.disabled; });
      var index = controls.indexOf(document.activeElement);
      event.preventDefault();
      controls[index < 0 ? (event.shiftKey ? controls.length - 1 : 0) : (index + (event.shiftKey ? controls.length - 1 : 1)) % controls.length].focus();
    }
    else if (event.key === "Escape") { event.preventDefault(); send("cancel"); }
    else if (event.key === "ArrowLeft") { event.preventDefault(); change(-1); }
    else if (event.key === "ArrowRight") { event.preventDefault(); change(1); }
  });
  render();
}());
