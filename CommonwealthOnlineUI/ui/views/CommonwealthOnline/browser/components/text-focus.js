(function () {
  "use strict";

  function isTextEntryTarget(node) {
    if (!node || !node.tagName) return false;
    var tag = node.tagName.toUpperCase();
    if (tag === "TEXTAREA") return true;
    if (tag === "INPUT") {
      var type = (node.type || "text").toLowerCase();
      return (
        type === "text" ||
        type === "search" ||
        type === "password" ||
        type === "email" ||
        type === "url" ||
        type === "tel" ||
        type === "number"
      );
    }
    return !!node.isContentEditable;
  }

  function isSelectControl(node) {
    return !!node && !!node.tagName && node.tagName.toUpperCase() === "SELECT";
  }

  function isFormEntryTarget(node) {
    return isTextEntryTarget(node) || isSelectControl(node);
  }

  function requestTextFocus(active) {
    if (typeof window.coRequestTextFocus !== "function") return;
    try {
      window.coRequestTextFocus(JSON.stringify({ active: !!active }));
    } catch (err) {
      console.warn("[CO] coRequestTextFocus failed:", err);
    }
  }


  window.CO_TextFocus = {
    isTextEntryTarget: isTextEntryTarget,
    isSelectControl: isSelectControl,
    isFormEntryTarget: isFormEntryTarget,
    request: requestTextFocus,

    bind: function (root) {
      if (!root) return;

      root.addEventListener("focusin", function (e) {
        if (isFormEntryTarget(e.target)) requestTextFocus(true);
      });

      root.addEventListener("focusout", function (e) {
        if (!isFormEntryTarget(e.target)) return;
        window.setTimeout(function () {
          if (isFormEntryTarget(document.activeElement)) return;
          requestTextFocus(false);
        }, 0);
      });
    }
  };
})();
