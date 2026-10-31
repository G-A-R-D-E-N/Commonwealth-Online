(function () {
  "use strict";

  var FILTER_ROWS = [
    { key: "search", type: "text", label: "Search", placeholder: "Search servers..." },
    { key: "region", type: "select", label: "Region", options: ["Any Region", "Oceania", "North America", "Europe", "Local"] },
    { key: "mode", type: "select", label: "Mode", options: ["Any Mode", "Co-op", "Roleplay", "Survival"] },
    { key: "worldspace", type: "select", label: "Worldspace", options: ["Any Worldspace", "Commonwealth", "Far Harbor", "Nuka-World"] },
    { key: "ping", type: "select", label: "Ping", options: ["Any", "< 50 ms", "< 100 ms", "< 150 ms"] },
    { key: "showFull", type: "checkbox", label: "Show Full" },
    { key: "hasPassword", type: "checkbox", label: "Has Password" },
    { key: "modded", type: "checkbox", label: "Modded" }
  ];

  function renderRow(row, index, state) {
    var rowFocused = state.focusZone === "filters" && state.selectedFilterIndex === index ? " focused" : "";
    if (row.type === "text") {
      return (
        '<div class="co-filter-row' + rowFocused + '" data-filter-index="' + index + '">' +
        '<div class="co-filter-label">' + row.label + '</div>' +
        '<input class="co-filter-input" type="text" data-filter="' + row.key + '" placeholder="' + row.placeholder + '" value="' + escapeAttr(state.filters[row.key] || "") + '">' +
        "</div>"
      );
    }
    if (row.type === "select") {
      var opts = row.options.map(function (opt) {
        var sel = (state.filters[row.key] || row.options[0]) === opt ? " selected" : "";
        return '<option value="' + escapeAttr(opt) + '"' + sel + ">" + opt + "</option>";
      }).join("");
      return (
        '<div class="co-filter-row' + rowFocused + '" data-filter-index="' + index + '">' +
        '<div class="co-filter-label">' + row.label + '</div>' +
        '<select class="co-filter-select" data-filter="' + row.key + '">' + opts + "</select>" +
        "</div>"
      );
    }
    var checked = state.filters[row.key] ? " checked" : "";
    return (
      '<label class="co-checkbox-row co-filter-row co-filter-checks' + rowFocused + '" data-filter-index="' + index + '">' +
      '<span class="co-checkbox' + checked + '" data-filter="' + row.key + '"></span>' +
      row.label +
      "</label>"
    );
  }

  window.CO_Filters = {
    FILTER_ROWS: FILTER_ROWS,

    render: function (root, state) {
      var el = root.querySelector(".co-filters");
      if (!el) return;

      if (el.contains(document.activeElement) || el.contains(CO_Keyboard.getTarget())) return;
      var focusedClass = state.focusZone === "filters" ? " focused" : "";
      var searchHtml = renderRow(FILTER_ROWS[0], 0, state);
      var selectHtml = FILTER_ROWS.slice(1, 5).map(function (row, i) {
        return renderRow(row, i + 1, state);
      }).join("");
      var checkboxHtml = FILTER_ROWS.slice(5).map(function (row, i) {
        return renderRow(row, i + 5, state);
      }).join("");
      var resetFocused = state.focusZone === "filters" && state.selectedFilterIndex === FILTER_ROWS.length ? " focused" : "";

      el.className = "co-filters" + focusedClass;
      el.innerHTML =
        '<div class="co-filters-row">' + searchHtml + "</div>" +
        '<div class="co-filters-row co-filters-row-selects">' + selectHtml + "</div>" +
        '<div class="co-filters-row co-filters-row-options">' +
        checkboxHtml +
        '<button type="button" class="co-btn co-reset-btn' + resetFocused + '">Reset Filters</button>' +
        "</div>";
    },

    bind: function (root, onFilterChange, onReset) {
      var el = root.querySelector(".co-filters");
      if (!el) return;

      el.addEventListener("change", function (e) {
        var target = e.target;
        if (!target.dataset.filter) return;
        onFilterChange(target.dataset.filter, target.type === "checkbox" ? target.checked : target.value);
      });

      el.addEventListener("input", function (e) {
        var target = e.target;
        if (target.classList.contains("co-filter-input")) {
          onFilterChange(target.dataset.filter, target.value);
        }
      });

      el.addEventListener("click", function (e) {
        if (e.target.closest(".co-reset-btn")) {
          onReset();
          return;
        }
        var row = e.target.closest(".co-checkbox-row");
        if (row) {
          var box = row.querySelector(".co-checkbox");
          if (box) {
            onFilterChange(box.dataset.filter, !box.classList.contains("checked"));
          }
        }
      });
    }
  };

  function escapeAttr(value) {
    return String(value)
      .replace(/&/g, "&amp;")
      .replace(/"/g, "&quot;")
      .replace(/</g, "&lt;");
  }
})();
