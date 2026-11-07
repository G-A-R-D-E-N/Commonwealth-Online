(function () {
  "use strict";

  var MAX_LENGTH = 128;
  var FOCUS_INPUT = 0;
  var FOCUS_OK = 1;
  var FOCUS_CANCEL = 2;

  function create(deps) {
    var root = deps.root;
    var el = root.querySelector("#coPasswordPrompt");
    var title = root.querySelector("#coPasswordTitle");
    var input = root.querySelector("#coPasswordInput");
    var okButton = root.querySelector(".co-password-ok");
    var cancelButton = root.querySelector(".co-password-cancel");
    var serverId = "";
    var focusIndex = FOCUS_INPUT;

    function paint() {
      input.classList.toggle("focused", focusIndex === FOCUS_INPUT);
      okButton.classList.toggle("focused", focusIndex === FOCUS_OK);
      cancelButton.classList.toggle("focused", focusIndex === FOCUS_CANCEL);
    }

    function close() {
      serverId = "";
      input.value = "";
      if (document.activeElement === input) input.blur();
      deps.state.passwordPrompt = false;
      el.classList.add("hidden");
      deps.render();
    }

    function submit() {
      var password = input.value;
      if (password.length > MAX_LENGTH) {
        title.textContent = "Password must be " + MAX_LENGTH + " characters or fewer.";
        return;
      }
      var id = serverId;
      close();
      deps.sendJoin(id, password);
    }

    function show(id, name) {
      serverId = id;
      title.textContent = name ? "Password for " + name : "Server password";
      input.value = "";
      focusIndex = FOCUS_INPUT;
      deps.state.passwordPrompt = true;
      el.classList.remove("hidden");
      paint();
      deps.render();
    }

    function handleButton(button, phase, options) {
      if (!button || phase !== "down") return;
      if (button === "b") {
        close();
      } else if (button === "dpad_up") {
        focusIndex = Math.max(FOCUS_INPUT, focusIndex - 1);
        paint();
      } else if (button === "dpad_down") {
        focusIndex = Math.min(FOCUS_CANCEL, focusIndex + 1);
        paint();
      } else if (button === "a" && (focusIndex === FOCUS_OK || (options && options.confirmText))) {
        submit();
      } else if (button === "a" && focusIndex === FOCUS_INPUT) {
        CO_Keyboard.open(input);
      } else if (button === "a") {
        close();
      }
      deps.render();
    }

    el.addEventListener("click", function (e) {
      if (e.target.closest(".co-password-ok")) {
        submit();
      } else if (e.target.closest(".co-password-cancel")) {
        close();
      }
    });

    return {
      show: show,
      handleButton: handleButton,
      isOpen: function () {
        return !!deps.state.passwordPrompt;
      }
    };
  }

  window.CO_PasswordPrompt = {
    create: create
  };
})();
