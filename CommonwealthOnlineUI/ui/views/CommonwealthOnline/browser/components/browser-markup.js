(function () {
  "use strict";

  function build(layout) {
    var layoutClass = layout === "embedded" ? " co-browser--embedded" : " co-browser--standalone";
    return (
      '<div class="co-root' +
      layoutClass +
      '" id="coRoot">' +
      '<div class="co-panel" id="coPanel">' +
      '<div class="co-hud-corner co-hud-corner--tl" aria-hidden="true"></div>' +
      '<div class="co-hud-corner co-hud-corner--tr" aria-hidden="true"></div>' +
      '<div class="co-hud-corner co-hud-corner--bl" aria-hidden="true"></div>' +
      '<div class="co-hud-corner co-hud-corner--br" aria-hidden="true"></div>' +
      '<div class="co-welcome-banner">' +
      '<span class="co-welcome-banner__bracket">[</span>' +
      '<span class="co-welcome-banner__text">WELCOME TO COMMONWEALTH ONLINE</span>' +
      '<span class="co-welcome-banner__bracket">]</span>' +
      '</div>' +
      '<header class="co-panel-header">' +
      '<div class="co-panel-title-group">' +
      '<h1 class="co-panel__heading">MULTIPLAYER</h1>' +
      '<span class="co-panel__subheading">// COMMONWEALTH ONLINE //</span>' +
      '</div>' +
      '</header>' +
      '<div class="co-subpanel">' +
      '<nav class="co-nav-list"></nav>' +
      '<div class="co-subpanel-content">' +
      '<div class="co-tab-content">' +
      '<div class="co-tab-pane" data-pane="direct-connect">' +
      '<header class="co-subpanel-header">' +
      '<h2 class="co-subpanel-title">Direct Connect</h2>' +
      "</header>" +
      '<div class="co-direct-connect"></div>' +
      "</div>" +
      '<div class="co-tab-pane active" data-pane="server-browser">' +
      '<header class="co-subpanel-header">' +
      '<h2 class="co-subpanel-title" id="coSubpanelTitle">Servers</h2>' +
      '<div class="co-subpanel-meta">' +
      '<span id="coConnectionStatus">Online</span>' +
      '<span class="co-count" id="coServerCount"></span>' +
      "</div>" +
      "</header>" +
      '<div class="co-filters"></div>' +
      '<div class="co-preview-area"></div>' +
      '<div class="co-server-list-wrap">' +
      '<div class="co-server-list"></div>' +
      '<div class="co-scroll-indicator" aria-hidden="true">&raquo;&raquo;</div>' +
      "</div>" +
      "</div>" +
      '<div class="co-tab-pane" data-pane="steam-friends">' +
      '<header class="co-subpanel-header">' +
      '<h2 class="co-subpanel-title">Play</h2>' +
      "</header>" +
      '<p class="co-pane-lead">Host on this PC through Steam, join a friend, or pick a dedicated server on your network.</p>' +
      '<div class="co-steam"></div>' +
      "</div>" +
      '<div class="co-tab-pane" data-pane="favorites">' +
      '<header class="co-subpanel-header">' +
      '<h2 class="co-subpanel-title">Favorites</h2>' +
      "</header>" +
      '<div class="co-preview-area"></div>' +
      '<div class="co-server-list-wrap">' +
      '<div class="co-server-list co-favorites-list"></div>' +
      '<div class="co-scroll-indicator" aria-hidden="true">&raquo;&raquo;</div>' +
      "</div>" +
      "</div>" +
      '<div class="co-tab-pane" data-pane="recent">' +
      '<header class="co-subpanel-header">' +
      '<h2 class="co-subpanel-title">Recent</h2>' +
      "</header>" +
      '<div class="co-preview-area"></div>' +
      '<div class="co-server-list-wrap">' +
      '<div class="co-server-list co-recent-list"></div>' +
      '<div class="co-scroll-indicator" aria-hidden="true">&raquo;&raquo;</div>' +
      "</div>" +
      "</div>" +
      "</div>" +
      "</div>" +
      "</div>" +
      '<footer class="co-action-bar">' +
      '<div class="co-prompts"></div>' +
      '<button type="button" class="co-btn co-back" data-action="back" id="coBackButton">Back</button>' +
      '<button type="button" class="co-btn co-share-log" id="coShareLogButton">Share log</button>' +
      '<span class="co-share-log-notice" id="coShareLogNotice"></span>' +
      "</footer>" +
      '<div class="co-keyboard hidden"></div>' +
      '<div class="co-join-overlay hidden" id="coJoinOverlay">' +
      '<div class="co-spinner"></div>' +
      '<p id="coJoinMessage">Connecting...</p>' +
      '<ul class="co-modpack-detail hidden" id="coModpackDetail"></ul>' +
      '<button type="button" class="co-btn co-copy-modpack hidden" id="coCopyModpackButton">Copy list</button>' +
      '<span class="co-copy-modpack-notice" id="coCopyModpackNotice"></span>' +
      '<p class="co-modpack-warnings-title hidden" id="coModpackWarningsTitle">Extra F4SE plugins are loaded (not required by this server):</p>' +
      '<ul class="co-modpack-detail co-modpack-warnings hidden" id="coModpackWarnings"></ul>' +
      "</div>" +
      '<div class="co-password-prompt hidden" id="coPasswordPrompt">' +
      '<p class="co-password-title" id="coPasswordTitle">Server password</p>' +
      '<label for="coPasswordInput">Password</label>' +
      '<input class="co-filter-input" id="coPasswordInput" type="password" maxlength="128" autocomplete="off">' +
      '<div class="co-password-actions">' +
      '<button type="button" class="co-btn co-password-ok">Join</button>' +
      '<button type="button" class="co-btn co-password-cancel">Cancel</button>' +
      "</div>" +
      "</div>" +
      "</div>" +
      "</div>"
    );
  }

  window.CO_BrowserMarkup = { build: build };
})();
