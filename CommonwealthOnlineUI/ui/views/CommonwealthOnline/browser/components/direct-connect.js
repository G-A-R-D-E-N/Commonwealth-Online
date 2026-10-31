(function () {
  "use strict";

  window.CO_DirectConnect = {
    render: function (root, state) {
      var pane = root.querySelector('[data-pane="direct-connect"]');
      if (!pane) return;

      var container = pane.querySelector(".co-direct-connect");
      if (!container) return;

      var active = document.activeElement;
      var target = CO_Keyboard.getTarget();
      if (container.contains(active) && CO_TextFocus.isFormEntryTarget(active) || container.contains(target)) {
        container.querySelectorAll(".co-direct-row").forEach(function (row, index) {
          row.classList.toggle("focused", state.focusZone === "direct-form" && state.directConnectFieldIndex === index);
        });
        return;
      }
      var address = state.directConnect.address == null ? "127.0.0.1" : state.directConnect.address;
      var port = state.directConnect.port == null ? "7777" : state.directConnect.port;
      var addressFocused = state.focusZone === "direct-form" && state.directConnectFieldIndex === 0 ? " focused" : "";
      var portFocused = state.focusZone === "direct-form" && state.directConnectFieldIndex === 1 ? " focused" : "";
      var passwordFocused = state.focusZone === "direct-form" && state.directConnectFieldIndex === 2 ? " focused" : "";
      var connectFocused = state.focusZone === "direct-form" && state.directConnectFieldIndex === 3 ? " focused" : "";
      var retryButtonHtml = state.retryAvailable
        ? '<button type="button" class="co-btn co-dc-retry' + (state.focusZone === "direct-form" && state.directConnectFieldIndex === 4 ? " focused" : "") + '">Retry</button>'
        : "";
      var passwordField = container.querySelector("#co-dc-password");
      var password = passwordField ? passwordField.value : "";

      container.innerHTML =
        '<div class="co-direct-row' + addressFocused + '">' +
        '<label for="co-dc-address">Server Address</label>' +
        '<input class="co-filter-input" id="co-dc-address" type="text" value="' + escapeAttr(address) + '">' +
        "</div>" +
        '<div class="co-direct-row' + portFocused + '">' +
        '<label for="co-dc-port">Port</label>' +
        '<input class="co-filter-input" id="co-dc-port" type="text" value="' + escapeAttr(port) + '">' +
        "</div>" +
        '<div class="co-direct-row' + passwordFocused + '">' +
        '<label for="co-dc-password">Password (optional)</label>' +
        '<input class="co-filter-input" id="co-dc-password" type="password" maxlength="128" autocomplete="off">' +
        "</div>" +
        '<div class="co-direct-actions">' +
        '<button type="button" class="co-btn co-dc-connect' + connectFocused + '">Connect</button>' +
        retryButtonHtml +
        "</div>" +
        '<p class="co-detail-desc" style="margin-top:16px;color:var(--co-text-dim);font-size:var(--co-font-sm)">Enter an IP or domain (for example 127.0.0.1, 2001:db8::1 or server.example.com). You can also paste host:port or [IPv6]:port in the address field.</p>';
      container.querySelector("#co-dc-password").value = password;
    },

    bind: function (root, onConnect, onChange, onRetry) {
      var pane = root.querySelector('[data-pane="direct-connect"]');
      if (!pane) return;

      pane.addEventListener("click", function (e) {
        if (e.target.closest(".co-dc-retry")) {
          if (typeof onRetry === "function") onRetry();
          return;
        }
        if (!e.target.closest(".co-dc-connect")) return;
        var address = pane.querySelector("#co-dc-address");
        var port = pane.querySelector("#co-dc-port");
        var password = pane.querySelector("#co-dc-password");
        onConnect(address ? address.value : "", port ? port.value : "", password ? password.value : "");
        CO_DirectConnect.clearPassword(root);
      });

      pane.addEventListener("input", function (e) {
        if (!e.target || (e.target.id !== "co-dc-address" && e.target.id !== "co-dc-port")) return;
        if (typeof onChange !== "function") return;
        var vals = CO_DirectConnect.getValues(root);
        onChange(vals.address, vals.port);
      });
    },

    getValues: function (root) {
      var pane = root.querySelector('[data-pane="direct-connect"]');
      if (!pane) return { address: "127.0.0.1", port: "7777", password: "" };
      var address = pane.querySelector("#co-dc-address");
      var port = pane.querySelector("#co-dc-port");
      var password = pane.querySelector("#co-dc-password");
      return {
        address: address ? address.value : "127.0.0.1",
        port: port ? port.value : "7777",
        password: password ? password.value : ""
      };
    },

    clearPassword: function (root) {
      var field = root.querySelector("#co-dc-password");
      if (field) field.value = "";
    }
  };

  function escapeAttr(value) {
    return String(value).replace(/&/g, "&amp;").replace(/"/g, "&quot;").replace(/</g, "&lt;");
  }
})();
