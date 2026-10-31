(function () {
  "use strict";
  window.CO_MainMenuKeys = function (event, move, activate, back) {
    switch (event.key) {
      case "ArrowUp":
        event.preventDefault();
        move(-1);
        break;
      case "ArrowDown":
        event.preventDefault();
        move(1);
        break;
      case "Enter":
      case " ":
        event.preventDefault();
        activate();
        break;
      case "Escape":
        event.preventDefault();
        back();
        break;
    }
  };
})();
