(function () {
  "use strict";

  var extrasFeature = 4;
  var extrasCategoryGroup = 4294967295;
  var currentFeature = null;

  var steppers = [
    { action: "setHairStyle", label: "Hair style option", value: 0 },
    { action: "setHairColor", label: "Hair colour option", value: 0 },
    { action: "setBeard", label: "Facial hair option", value: 0 },
    { action: "setDetailColor", label: "Eye colour option", value: 0 }
  ];

  var modeControls = [
    { label: "FACE", control: "F" },
    { label: "BODY", control: "B" },
    { label: "EXTRAS", control: "Accept" },
    { label: "SEX", control: "X" },
    { label: "PRESET", control: "R" },
    { label: "P-", control: "KeyLeft" },
    { label: "P+", control: "KeyRight" }
  ];

  var contextControls = [
    { label: "ACCEPT", control: "Accept" },
    { label: "DONE", control: "Done" },
    { label: "CANCEL", control: "Cancel" },
    { label: "TYPE", control: "T" },
    { label: "COLOUR", control: "C" },
    { label: "OVERLAY", control: "R" },
    { label: "SKIN", control: "Q" },
    { label: "APPLY", control: "Space" },
    { label: "<", control: "KeyLeft" },
    { label: ">", control: "KeyRight" },
    { label: "SIZE-", control: "KeyDown" },
    { label: "SIZE+", control: "KeyUp" }
  ];

  var featureButtons = [
    { label: "HAIR", feature: 0, apply: "setHairStyle" },
    { label: "HAIR COLOUR", feature: 1, apply: "setHairColor" },
    { label: "BEARD", feature: 2, apply: "setBeard" },
    { label: "EYES", feature: 3, apply: "setDetailColor" },
    { label: "SKIN", feature: 5, apply: "setDetailColor" },
    { label: "MORPHS", feature: 6, apply: "setMorphPreset" },
    { label: "EXTRAS", feature: extrasFeature, apply: null }
  ];

  var controllerMap = {
    dpad_up: "Up",
    dpad_down: "Down",
    dpad_left: "Left",
    dpad_right: "Right",
    a: "Accept",
    b: "Cancel",
    x: "XButton",
    y: "YButton",
    lb: "LShoulder",
    rb: "RShoulder"
  };

  var boneRegion = { value: 0 };
  var morphIntensity = { value: 0 };

  function send(payload) {
    return window.CO_CharacterCreator.send(payload);
  }

  function controlButton(entry) {
    var button = document.createElement("button");
    button.type = "button";
    button.className = "creator__control";
    button.textContent = entry.label;
    button.addEventListener("click", function () {
      send({ action: "input", control: entry.control });
    });
    return button;
  }

  function controlRow(title, entries) {
    var row = document.createElement("div");
    row.className = "creator__control-row";
    var heading = document.createElement("span");
    heading.className = "creator__control-title";
    heading.textContent = title;
    row.appendChild(heading);
    entries.forEach(function (entry) {
      row.appendChild(controlButton(entry));
    });
    return row;
  }

  function isExtrasCategoryList(payload) {
    return payload.feature === extrasFeature && payload.groupIndex === extrasCategoryGroup;
  }

  function requestOptions(feature, groupIndex) {
    if (groupIndex === undefined) {
      send({ action: "getOptions", feature: feature });
      return;
    }
    send({ action: "getOptions", feature: feature, groupIndex: groupIndex });
  }

  function buildFeatureRow() {
    var row = document.createElement("div");
    row.className = "creator__control-row";

    var heading = document.createElement("span");
    heading.className = "creator__control-title";
    heading.textContent = "OPTIONS";
    row.appendChild(heading);

    featureButtons.forEach(function (entry) {
      var button = document.createElement("button");
      button.type = "button";
      button.className = "creator__control";
      button.textContent = entry.label;
      button.dataset.feature = String(entry.feature);
      button.addEventListener("click", function () {
        currentFeature = entry;
        if (entry.feature === extrasFeature) {
          requestOptions(entry.feature, extrasCategoryGroup);
          return;
        }
        requestOptions(entry.feature);
      });
      row.appendChild(button);
    });

    return row;
  }

  function renderOptions(payload) {
    if (!currentFeature || currentFeature.feature !== payload.feature) {
      return;
    }
    var feature = currentFeature;
    var host = document.getElementById("options");
    if (!host) {
      return;
    }
    host.innerHTML = "";

    if (!payload.count) {
      var empty = document.createElement("div");
      empty.className = "creator__feedback is-rejected";
      empty.textContent =
        "Engine returned no options for feature " +
        payload.feature +
        " (group " +
        payload.groupIndex +
        ").";
      host.appendChild(empty);
      return;
    }

    var status = document.createElement("div");
    status.className = "creator__feedback";
    var listingCategories = isExtrasCategoryList(payload);
    status.textContent = listingCategories
      ? "Extras categories: " + payload.count
      : "Feature " +
        payload.feature +
        " (group " +
        payload.groupIndex +
        "): " +
        payload.count +
        " options";
    host.appendChild(status);

    if (!listingCategories && payload.feature === extrasFeature) {
      var back = document.createElement("button");
      back.type = "button";
      back.className = "creator__control";
      back.textContent = "BACK TO EXTRAS";
      back.addEventListener("click", function () {
        requestOptions(extrasFeature, extrasCategoryGroup);
      });
      host.appendChild(back);
    }

    payload.options.forEach(function (option) {
      var button = document.createElement("button");
      button.type = "button";
      button.className = "creator__option" + (option.applied ? " is-applied" : "");
      button.textContent = (option.applied ? "\u2022 " : "  ") + (option.text || "(unnamed)");

      button.addEventListener("click", function () {
        if (currentFeature !== feature) {
          return;
        }

        if (listingCategories) {
          requestOptions(extrasFeature, option.index);
          return;
        }

        if (payload.feature === extrasFeature) {
          send(
            option.applied
              ? { action: "removeTint", group: payload.groupIndex }
              : { action: "applyTint", group: payload.groupIndex, value: option.index }
          );
        } else {
          send({ action: feature.apply, value: option.index });
        }

        setTimeout(function () {
          requestOptions(payload.feature, payload.groupIndex);
          send({ action: "getState" });
        }, 150);
      });

      host.appendChild(button);
    });
  }

  function buildBoneRegionRow() {
    var row = document.createElement("div");
    row.className = "creator__stepper";

    var dec = document.createElement("button");
    dec.type = "button";
    dec.className = "creator__step";
    dec.textContent = "-";

    var label = document.createElement("span");
    label.className = "creator__step-label";

    var inc = document.createElement("button");
    inc.type = "button";
    inc.className = "creator__step";
    inc.textContent = "+";

    function paint() {
      label.textContent = "Bone region: " + boneRegion.value;
    }

    function step(delta) {
      boneRegion.value = Math.max(0, boneRegion.value + delta);
      paint();
      send({ action: "setBoneRegion", value: boneRegion.value });
      setTimeout(function () {
        send({ action: "getState" });
      }, 120);
    }

    dec.addEventListener("click", function () {
      step(-1);
    });
    inc.addEventListener("click", function () {
      step(1);
    });

    paint();
    row.appendChild(dec);
    row.appendChild(label);
    row.appendChild(inc);
    return row;
  }

  function buildMorphIntensityRow() {
    var row = document.createElement("div");
    row.className = "creator__stepper";

    var dec = document.createElement("button");
    dec.type = "button";
    dec.className = "creator__step";
    dec.textContent = "-";

    var label = document.createElement("span");
    label.className = "creator__step-label";

    var inc = document.createElement("button");
    inc.type = "button";
    inc.className = "creator__step";
    inc.textContent = "+";

    function paint() {
      label.textContent = "Morph intensity: " + morphIntensity.value.toFixed(1);
    }

    function step(delta) {
      var next = Math.round((morphIntensity.value + delta) * 10) / 10;
      morphIntensity.value = Math.max(-1, Math.min(1, next));
      paint();
      send({ action: "setMorphIntensity", value: morphIntensity.value });
    }

    dec.addEventListener("click", function () {
      step(-0.1);
    });
    inc.addEventListener("click", function () {
      step(0.1);
    });

    paint();
    row.appendChild(dec);
    row.appendChild(label);
    row.appendChild(inc);
    return row;
  }

  function buildAxesRow() {
    var row = document.createElement("div");
    row.className = "creator__control-row";

    var heading = document.createElement("span");
    heading.className = "creator__control-title";
    heading.textContent = "SCULPT AXES (current bone region)";
    row.appendChild(heading);

    var button = document.createElement("button");
    button.type = "button";
    button.className = "creator__control";
    button.textContent = "LOAD AXES";
    button.addEventListener("click", function () {
      send({ action: "getAxes" });
    });
    row.appendChild(button);

    return row;
  }

  function buildPresetsRow() {
    var row = document.createElement("div");
    row.className = "creator__control-row";

    var heading = document.createElement("span");
    heading.className = "creator__control-title";
    heading.textContent = "PRESETS";
    row.appendChild(heading);

    var button = document.createElement("button");
    button.type = "button";
    button.className = "creator__control";
    button.textContent = "LOAD PRESETS";
    button.addEventListener("click", function () {
      send({ action: "getPresets" });
    });
    row.appendChild(button);

    return row;
  }

  function buildControls() {
    currentFeature = null;
    var host = document.getElementById("controls");
    host.innerHTML = "";

    steppers.forEach(function (stepper) {
      var wrap = document.createElement("div");
      wrap.className = "creator__stepper";

      var dec = document.createElement("button");
      dec.type = "button";
      dec.className = "creator__step";
      dec.textContent = "-";

      var label = document.createElement("span");
      label.className = "creator__step-label";

      var inc = document.createElement("button");
      inc.type = "button";
      inc.className = "creator__step";
      inc.textContent = "+";

      function paint() {
        label.textContent = stepper.label + ": " + stepper.value;
      }

      function step(delta) {
        stepper.value = Math.max(0, stepper.value + delta);
        paint();
        send({ action: stepper.action, value: stepper.value });
        setTimeout(function () {
          send({ action: "getState" });
        }, 120);
      }

      dec.addEventListener("click", function () {
        step(-1);
      });
      inc.addEventListener("click", function () {
        step(1);
      });

      paint();
      wrap.appendChild(dec);
      wrap.appendChild(label);
      wrap.appendChild(inc);
      host.appendChild(wrap);
    });

    host.appendChild(controlRow("MODE", modeControls));
    host.appendChild(buildBoneRegionRow());
    host.appendChild(buildMorphIntensityRow());
    host.appendChild(buildAxesRow());

    var axes = document.createElement("div");
    axes.className = "creator__options";
    axes.id = "axes";
    host.appendChild(axes);

    host.appendChild(buildPresetsRow());

    var presets = document.createElement("div");
    presets.className = "creator__options";
    presets.id = "presets";
    host.appendChild(presets);

    host.appendChild(controlRow("CONTEXT", contextControls));
    host.appendChild(buildFeatureRow());

    var options = document.createElement("div");
    options.className = "creator__options";
    options.id = "options";
    host.appendChild(options);

    var feedback = document.createElement("div");
    feedback.className = "creator__feedback";
    feedback.id = "inputFeedback";
    feedback.textContent = "No control sent yet.";
    host.appendChild(feedback);
  }

  function handleController(payload) {
    if (payload.phase !== "down") {
      return;
    }
    var control = controllerMap[payload.button];
    if (!control) {
      return;
    }
    send({ action: "input", control: control });
  }

  window.CO_CreatorControls = {
    build: buildControls,
    handleController: handleController,
    renderOptions: renderOptions
  };
})();
