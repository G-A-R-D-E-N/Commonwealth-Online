const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");

const root = __dirname;
const browser = fs.readFileSync(path.join(root, "browser", "browser.js"), "utf8");
const directoryRefresh = fs.readFileSync(path.join(root, "browser", "components", "directory-refresh.js"), "utf8");
const serverList = fs.readFileSync(path.join(root, "browser", "components", "server-list.js"), "utf8");
const settings = fs.readFileSync(path.join(root, "settings", "settings-native.js"), "utf8");
const badge = fs.readFileSync(path.join(root, "pause-menu", "session-mode-badge.js"), "utf8");
const pauseScript = fs.readFileSync(path.join(root, "pause-menu", "script.js"), "utf8");
const pauseStyles = fs.readFileSync(path.join(root, "pause-menu", "styles.css"), "utf8");
const pauseHtml = fs.readFileSync(path.join(root, "pause-menu", "index.html"), "utf8");

assert.match(
  badge,
  /payload\.label/,
  "the badge renders the label field of the payload the UI DLL pushes, so the text table stays in F4TWorldModePolicy.cpp."
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
assert.match(browser, /sendAction\("cancelJoin", \{\}\)/);

assert.match(
  pauseScript,
  /mapPlayers\.markerClass\(other\)/,
  "the pause map must class each remote player by relationship, so a faction flag styles the pip."
);
assert.match(
  pauseStyles,
  /\.marker--rel-faction \{/,
  "the pause map must style the faction pip with a .marker--rel-faction rule."
);
assert.match(
  pauseStyles,
  /\.marker--named-always \.marker__name/,
  "a faction member's name must show without hover."
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
  /\.marker--player-other::before \{/,
  "a remote player pip needs a facing arrow, or the heading the payload carries stays invisible."
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
