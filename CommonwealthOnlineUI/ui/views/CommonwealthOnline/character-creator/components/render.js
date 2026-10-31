(function () {
  "use strict";

  var modeNames = {
    0: "START",
    1: "FACE",
    2: "BODY",
    3: "SCULPT",
    4: "HAIR",
    5: "FEATURE",
    6: "FEATURE CATEGORY",
    7: "PRESET",
    8: "PRESET NAME",
    9: "BODY ADVANCED",
    10: "BODY OVERLAY",
    11: "OVERLAY SELECT",
    12: "OVERLAY TRANSFORM",
    13: "BODY SKIN",
    14: "CUSTOM COLOUR"
  };

  var lastState = null;

  function send(payload) {
    return window.CO_CharacterCreator.send(payload);
  }

  function row(label, value) {
    var el = document.createElement("div");
    el.className = "creator__row";

    var key = document.createElement("span");
    key.className = "creator__row-label";
    key.textContent = label;

    var val = document.createElement("span");
    val.className = "creator__row-value";
    val.textContent = value === undefined || value === null || value === "" ? "\u2014" : String(value);

    el.appendChild(key);
    el.appendChild(val);
    return el;
  }

  // TODO-verify(#366): form-key display names mirror the bridge contract; see docs/code-notes/character-creator.md.
  function formatFormId(formId) {
    if (!formId) {
      return "\u2014";
    }
    return "0x" + Number(formId).toString(16).toUpperCase().padStart(8, "0");
  }

  function renderOverview(state, panel) {
    panel.innerHTML = "";
    panel.appendChild(row("Available", state.available ? "yes" : "no"));
    panel.appendChild(row("Menu open", state.menuOpen ? "yes" : "no"));
    panel.appendChild(
      row(
        "Live mode",
        state.menuMode === undefined
          ? "unknown"
          : String(state.menuMode) + " " + (modeNames[state.menuMode] || "")
      )
    );
    panel.appendChild(row("Sculpting", state.sculpting === undefined ? "unknown" : state.sculpting));
    panel.appendChild(row("Unconfirmed changes", state.dirty === undefined ? "unknown" : state.dirty));
    panel.appendChild(row("Sex", state.isFemale ? "Female" : "Male"));
    panel.appendChild(row("Race", formatFormId(state.raceFormId)));
    panel.appendChild(row("Height", state.height));
    panel.appendChild(row("CharGen flag", state.byCharGenFlag));
    panel.appendChild(row("CharGen HUD mode", state.charGenHUDMode));
  }

  function renderHead(state, panel) {
    panel.innerHTML = "";
    panel.appendChild(row("Hair part", formatFormId(state.hairPartFormId)));
    panel.appendChild(row("Facial hair part", formatFormId(state.facialHairPartFormId)));
    panel.appendChild(row("Eyes part", formatFormId(state.eyesPartFormId)));
    panel.appendChild(row("Hair colour", formatFormId(state.hairColorFormId)));
    panel.appendChild(row("Facial hair colour", formatFormId(state.facialHairColorFormId)));

    var list = document.createElement("div");
    list.className = "creator__list";
    (state.headParts || []).forEach(function (part) {
      list.appendChild(
        row(
          "type " + part.type,
          formatFormId(part.formId) + (part.editorId ? "  " + part.editorId : "")
        )
      );
    });
    if (!list.childElementCount) {
      list.appendChild(row("Head parts", "none reported"));
    }
    panel.appendChild(list);
  }

  function renderTints(state, panel) {
    panel.innerHTML = "";
    var weight = state.morphWeight || {};
    panel.appendChild(row("Body triang. thin", weight.thin));
    panel.appendChild(row("Body triang. muscular", weight.muscular));
    panel.appendChild(row("Body triang. large", weight.large));

    var tint = state.bodyTintColor || {};
    panel.appendChild(row("Body tint R", tint.r));
    panel.appendChild(row("Body tint G", tint.g));
    panel.appendChild(row("Body tint B", tint.b));
    panel.appendChild(row("Body tint A", tint.a));
    panel.appendChild(row("Tint entries", state.tintCount));
  }

  function renderAxes(payload) {
    var host = document.getElementById("axes");
    if (!host) {
      return;
    }
    host.innerHTML = "";

    var status = document.createElement("div");
    status.className = "creator__feedback";
    status.textContent =
      "Bone region " + payload.boneIndex + " (id " + payload.boneId + "): " + payload.count + " axes";
    host.appendChild(status);

    (payload.axes || []).forEach(function (axis) {
      var command = axis.isScalingAxis ? "setAxisScale" : axis.isSlider ? "setAxisMorph" : null;
      var kind = axis.isScalingAxis ? "scale" : axis.isSlider ? "morph" : "static";
      var current = axis.isScalingAxis ? axis.scalingValue : axis.sliderValue;

      var line = document.createElement("div");
      line.className = "creator__stepper";

      var label = document.createElement("span");
      label.className = "creator__step-label";
      label.textContent =
        "axis " +
        axis.axisIndex +
        " " +
        kind +
        ": " +
        (typeof current === "number" ? current.toFixed(2) : "\u2014");

      line.appendChild(label);

      if (command) {
        var value = { v: typeof current === "number" ? current : 0 };

        [-0.1, 0.1].forEach(function (delta) {
          var step = document.createElement("button");
          step.type = "button";
          step.className = "creator__step";
          step.textContent = delta < 0 ? "-" : "+";
          step.addEventListener("click", function () {
            value.v = Math.max(-1, Math.min(1, Math.round((value.v + delta) * 100) / 100));
            label.textContent = "axis " + axis.axisIndex + " " + kind + ": " + value.v.toFixed(2);
            send({ action: command, axis: axis.axisIndex, value: value.v });
          });
          line.appendChild(step);
        });
      }

      host.appendChild(line);
    });
  }

  function renderPresets(payload) {
    var host = document.getElementById("presets");
    if (!host) {
      return;
    }
    host.innerHTML = "";

    var status = document.createElement("div");
    status.className = "creator__feedback";
    if (!payload.available) {
      status.classList.add("is-rejected");
      status.textContent =
        "Presets unavailable: LooksMenu presets are an F4EE feature and the F4EE plugin is not loaded.";
      host.appendChild(status);
      return;
    }

    status.textContent = "Presets: " + payload.count;
    host.appendChild(status);

    if (!payload.count) {
      var empty = document.createElement("div");
      empty.className = "creator__feedback";
      empty.textContent = "No preset files found in the F4EE preset folder.";
      host.appendChild(empty);
    }

    (payload.presets || []).forEach(function (preset) {
      var button = document.createElement("button");
      button.type = "button";
      button.className = "creator__option";
      button.textContent = preset.name || preset.path;

      button.addEventListener("click", function () {
        send({ action: "loadPreset", path: preset.path });
        setTimeout(function () {
          send({ action: "getState" });
        }, 150);
      });

      host.appendChild(button);
    });
  }

  function noteInput(control, handled) {
    var feedback = document.getElementById("inputFeedback");
    if (!feedback) {
      return;
    }
    feedback.textContent =
      "Last control: " + control + (handled ? " (handled)" : " (not handled in the current mode)");
    feedback.classList.toggle("is-rejected", !handled);
    send({ action: "getState" });
  }

  function render(state) {
    lastState = state;
    if (!state) {
      return;
    }

    document.getElementById("generation").textContent =
      "SESSION " + (state.generation === undefined ? "\u2014" : state.generation);

    renderOverview(state, document.getElementById("panel-overview"));
    renderHead(state, document.getElementById("panel-head"));
    renderTints(state, document.getElementById("panel-tints"));
  }

  window.CO_CreatorRender = {
    render: render,
    renderAxes: renderAxes,
    renderPresets: renderPresets,
    noteInput: noteInput,
    getLastState: function () {
      return lastState;
    }
  };
})();
