(function () {
  "use strict";

  window.CO_SelectMenus = {
    bind: function (root) {
      var menu = null;
      var owner = null;

      function close(restoreFocus) {
        if (menu) menu.remove();
        if (owner) {
          owner.removeAttribute("aria-expanded");
          if (restoreFocus && owner.isConnected) owner.focus();
        }
        menu = null;
        owner = null;
      }

      function open(select) {
        close(false);
        owner = select;
        owner.setAttribute("aria-expanded", "true");
        menu = document.createElement("div");
        menu.className = "co-select-menu";
        menu.setAttribute("role", "listbox");
        var buttons = Array.prototype.map.call(select.options, function (option) {
          var button = document.createElement("button");
          button.type = "button";
          button.className = "co-select-option";
          button.textContent = option.textContent;
          button.disabled = option.disabled;
          button.setAttribute("role", "option");
          button.setAttribute("aria-selected", String(option.selected));
          button.addEventListener("click", function () {
            select.value = option.value;
            close(true);
            select.dispatchEvent(new Event("change", { bubbles: true }));
          });
          menu.appendChild(button);
          return button;
        });
        menu.addEventListener("keydown", function (event) {
          var index = buttons.indexOf(document.activeElement);
          if (event.key === "Escape") {
            event.preventDefault();
            event.stopPropagation();
            close(true);
          } else if (event.key === "ArrowDown" || event.key === "ArrowUp") {
            event.preventDefault();
            event.stopPropagation();
            buttons[(index + (event.key === "ArrowDown" ? 1 : buttons.length - 1)) % buttons.length].focus();
          } else if (event.key === "Tab") {
            close(true);
          } else if (event.key === "Enter" || event.key === " ") {
            event.stopPropagation();
          }
        });
        document.body.appendChild(menu);
        var rect = select.getBoundingClientRect();
        var height = Math.min(menu.scrollHeight, 240, Math.max(60, window.innerHeight - 16));
        menu.style.left = Math.max(8, Math.min(rect.left, window.innerWidth - rect.width - 8)) + "px";
        menu.style.width = rect.width + "px";
        menu.style.maxHeight = height + "px";
        menu.style.top = Math.max(8, rect.bottom + height < window.innerHeight ? rect.bottom : rect.top - height) + "px";
        if (buttons.length) buttons[Math.max(0, select.selectedIndex)].focus();
      }

      root.addEventListener("mousedown", function (event) {
        var select = event.target.closest("select.co-filter-select");
        if (!select || select.disabled || event.button !== 0) return;
        event.preventDefault();
        if (owner === select) close(true);
        else open(select);
      }, true);
      root.addEventListener("keydown", function (event) {
        var select = event.target.closest("select.co-filter-select");
        if (select && !select.disabled && (event.key === "Enter" || event.key === " ")) {
          event.preventDefault();
          event.stopPropagation();
          open(select);
        }
      }, true);
      document.addEventListener("mousedown", function (event) {
        if (menu && event.target !== owner && !menu.contains(event.target)) close(false);
      });
      window.addEventListener("resize", function () { close(false); });
      root.addEventListener("scroll", function () { close(false); }, true);
      return close;
    }
  };
})();
