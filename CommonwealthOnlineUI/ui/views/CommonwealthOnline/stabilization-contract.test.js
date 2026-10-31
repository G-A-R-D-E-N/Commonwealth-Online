const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");

const root = __dirname;
const browser = fs.readFileSync(path.join(root, "browser", "browser.js"), "utf8");
const directoryRefresh = fs.readFileSync(path.join(root, "browser", "components", "directory-refresh.js"), "utf8");
const serverList = fs.readFileSync(path.join(root, "browser", "components", "server-list.js"), "utf8");
const settings = fs.readFileSync(path.join(root, "settings", "settings-native.js"), "utf8");
const capabilities = fs.readFileSync(path.join(root, "../../../src/F4TPrismaCapabilities.cpp"), "utf8");
const registry = fs.readFileSync(path.join(root, "../../../src/F4TSettingsRegistry.cpp"), "utf8");
const bridge = fs.readFileSync(path.join(root, "../../../src/F4TServerBrowserBridgeFacade.cpp"), "utf8");
const bridgeImpl = fs.readFileSync(path.join(root, "../../../src/F4TServerBrowserBridge.cpp"), "utf8");
const badge = fs.readFileSync(path.join(root, "pause-menu", "session-mode-badge.js"), "utf8");
const pauseScript = fs.readFileSync(path.join(root, "pause-menu", "script.js"), "utf8");
const pauseStyles = fs.readFileSync(path.join(root, "pause-menu", "styles.css"), "utf8");
const pauseHtml = fs.readFileSync(path.join(root, "pause-menu", "index.html"), "utf8");

assert.match(
  bridge,
  /COUI::WorldMode::Set\(a_entry\.worldMode\)/,
  "the session badge must take its world mode from the entry's worldMode field, which is what the " +
    "server publishes; entry.mode is the browser's own filter category (Co-op, Roleplay, Survival)."
);
assert.doesNotMatch(
  bridge,
  /COUI::WorldMode::Set\(a_entry\.mode\)/,
  "reading the browser filter category as the world mode labels every listed server with the same badge."
);
assert.match(
  badge,
  /payload\.label/,
  "the badge renders the label field of the payload the UI DLL pushes, so the text table stays in F4TWorldModePolicy.cpp."
);
assert.doesNotMatch(
  bridgeImpl,
  /^\s*SetCurrentServerInternal\(/m,
  "every join path must reach the badge through ServerBrowserBridge::SetCurrentServer; a direct call to the " +
    "internal setter skips COUI::WorldMode::Set and leaves the badge on the previous server's mode."
);
assert.doesNotMatch(
  bridgeImpl,
  /^\s*ClearCurrentServerInternal\(/m,
  "every disconnect path must reach the badge through ServerBrowserBridge::ClearCurrentServer; a direct call to " +
    "the internal clear leaves the badge showing the mode of a server the player has left."
);

assert.match(browser, /state\.connectionStatus = "Directory unavailable"/);
assert.match(browser, /state\.directoryNotice = event\.error \|\| ""/);
assert.match(serverList, /state\.directoryNotice && state\.activeTab === "server-browser"/);
assert.match(serverList, /emptyEl\.textContent = emptyText/);
assert.doesNotMatch(serverList, /class="co-empty">" \+ emptyText/);
assert.match(browser, /state\.connectionStatus = "Online"/);
assert.match(directoryRefresh, /connectionStatus: "Directory unavailable"/);
assert.match(directoryRefresh, /The server directory is unavailable right now\. Use Direct Connect or the Local tab to join a server\./);
assert.doesNotMatch(browser, /state\.connectionStatus = err && err\.message/);
assert.doesNotMatch(browser, /applyDirectoryServers\(\[\]\)/);
assert.match(settings, /deliveryKey \+= ":" \+ String\(event\.offset == null \? 0 : event\.offset\)/);
assert.match(capabilities, /GetViewRole\(a_view\) == a_role/);
assert.match(browser, /sendAction\("cancelJoin", \{\}\)/);
assert.match(bridge, /action == "cancelJoin"/);

for (const id of ["display.rainOcclusion", "display.lensFlare"]) {
  const start = registry.indexOf(`.id = "${id}"`);
  const end = registry.indexOf("});", start);
  assert.ok(start >= 0 && end > start, `${id} must remain registered`);
  assert.match(registry.slice(start, end), /\.showInUI = false/);
}

{
  const initStart = bridge.indexOf("void ServerBrowserBridge::Initialize(");
  const initEnd = bridge.indexOf("\n\tvoid ServerBrowserBridge::Reactivate(", initStart);
  const initialize = bridge.slice(initStart, initEnd);
  assert.ok(initStart >= 0 && initEnd > initStart, "Initialize must be found");
  assert.doesNotMatch(
    initialize,
    /ClearPendingJoinFailure/,
    "recreating the browser DOM (Initialize) must preserve a pending join-failure notification " +
      "stored while the browser was not dispatchable; clearing it here silently drops the " +
      "notification before OnBrowserShown can ever flush it."
  );

  const shutdownStart = bridge.indexOf("void ServerBrowserBridge::Shutdown()");
  const shutdownEnd = bridge.indexOf("\n\tvoid ServerBrowserBridge::OnBrowserShown()", shutdownStart);
  const shutdown = bridge.slice(shutdownStart, shutdownEnd);
  assert.ok(shutdownStart >= 0 && shutdownEnd > shutdownStart, "Shutdown must be found");
  assert.match(
    shutdown,
    /ClearPendingJoinFailure/,
    "a true terminal boundary (bridge shutdown, e.g. kPreLoadGame) must still discard a stale " +
      "pending join-failure notification so it cannot resurface in an unrelated later session."
  );

  const flushStart = bridgeImpl.indexOf("void FlushPendingJoinFailure()");
  const flushEnd = bridgeImpl.indexOf("\n\t\tvoid ClearPendingJoinFailure()", flushStart);
  const flush = bridgeImpl.slice(flushStart, flushEnd);
  assert.ok(flushStart >= 0 && flushEnd > flushStart, "FlushPendingJoinFailure must be found");
  const guardIndex = flush.indexOf("if (!g_pendingJoinFailure || !CanDispatchToBrowser())");
  const dispatchIndex = flush.indexOf("DispatchEventToView(*g_pendingJoinFailure)");
  const resetIndex = flush.indexOf("g_pendingJoinFailure.reset()");
  assert.ok(
    guardIndex >= 0 && guardIndex < dispatchIndex && dispatchIndex < resetIndex,
    "FlushPendingJoinFailure must guard on presence and dispatchability, then dispatch, then " +
      "reset immediately after — so a pending failure preserved across recreation is delivered " +
      "exactly once: a second OnBrowserShown before a new failure is stored finds nothing pending."
  );

  const completeStart = bridgeImpl.indexOf("void CompleteJoinFailure(");
  const completeEnd = bridgeImpl.indexOf("\n\t\tvoid FlushPendingJoinFailure()", completeStart);
  const complete = bridgeImpl.slice(completeStart, completeEnd);
  assert.ok(completeStart >= 0 && completeEnd > completeStart, "CompleteJoinFailure must be found");
  assert.match(
    complete,
    /if \(CanDispatchToBrowser\(\)\) \{\s*DispatchEventToView\(failureEvent\);\s*\} else \{\s*g_pendingJoinFailure = failureEvent;\s*\}/,
    "a join failure must be dispatched immediately when the browser can receive it, and stored " +
      "as pending only on the fallback (not-dispatchable) path, so it is never both dispatched " +
      "and queued for the same failure."
  );
}

assert.match(
  pauseScript,
  /ally\.classList\.toggle\("marker--faction", other\.faction === true\)/,
  "the pause map must add marker--faction to an ally marker only when the entry's faction flag is true."
);
assert.match(
  pauseStyles,
  /\.marker--faction \{/,
  "the pause map must style the faction marker with a .marker--faction rule."
);
assert.match(
  pauseStyles,
  /\.marker--faction \.marker__name,/,
  "a faction member's name must show without hover, like the ally name rule."
);

assert.match(
  pauseHtml,
  /id="mapElsewhere"/,
  "the map needs its own element for the count of allies it is not drawing."
);
assert.match(
  pauseScript,
  /othersElsewhere = Math\.max\(0, Math\.floor\(Number\(payload\.othersElsewhere\) \|\| 0\)\)/,
  "the ally count must come from the payload's othersElsewhere field, clamped so a bad value cannot print a negative count."
);
assert.match(
  pauseScript,
  /el\.style\.setProperty\("--marker-rot", rot \+ "deg"\)/,
  "the ally heading must reach CSS as --marker-rot so the upright name label can cancel it."
);
assert.match(
  pauseStyles,
  /\.marker--ally::after \{/,
  "an ally dot needs a facing tick, or the heading the payload carries stays invisible."
);
assert.match(
  pauseStyles,
  /transform: translateX\(-50%\) rotate\(calc\(-1 \* var\(--marker-rot, 0deg\)\)\)/,
  "the name label must counter-rotate the marker so a turned ally's name stays readable."
);
assert.match(
  pauseScript,
  /mapElsewhere\.hidden = !inMap \|\| count < 1/,
  "the count is shown only while a map is on screen and only when at least one ally is hidden."
);
assert.match(
  pauseStyles,
  /\.map-elsewhere \{/,
  "the hidden-ally count needs its own style rule."
);
assert.match(
  pauseStyles,
  /\.map-elsewhere\[hidden\] \{\s*display: none;\s*\}/,
  "the count's own display rule must yield to the hidden attribute, or it stays on screen at zero."
);

console.log("Commonwealth Online stabilization contracts passed");
