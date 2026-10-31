const assert = require("node:assert");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const context = { window: {} };
vm.runInNewContext(fs.readFileSync(path.join(__dirname, "faction.friends.js"), "utf8"), context);
const friends = context.window.coFriendsModel;

const supported = {
  supported: true,
  friends: [
    { friendId: "f1", name: "Alice", online: true },
    { friendId: "f2", name: "Bob", online: false },
    { friendId: "", name: "Broken" },
    { friendId: "f3", name: "x".repeat(200) }
  ],
  pendingIncoming: [{ requestId: "fr-1", displayName: "Carol" }, { requestId: "" }],
  pendingOutgoingPlayerIds: [3]
};

const model = friends.normalize(supported);
assert.deepEqual(JSON.parse(JSON.stringify(model.friends)), [
  { friendId: "f1", name: "Alice", online: true },
  { friendId: "f2", name: "Bob", online: false }
], "invalid friend rows are dropped");
assert.deepEqual(JSON.parse(JSON.stringify(model.incoming)), [{ requestId: "fr-1", displayName: "Carol" }]);
assert.deepEqual(JSON.parse(JSON.stringify(model.outgoing)), [3]);

const html = friends.renderHtml(supported);
assert.match(html, /FRIENDS/);
assert.match(html, /Alice/);
assert.match(html, /Online/);
assert.match(html, /Offline/);
assert.match(html, /Carol wants to add you as a friend/);
assert.match(html, /data-op="friendAccept" data-request="fr-1"/);
assert.match(html, /data-op="friendDecline" data-request="fr-1"/);
assert.match(html, /data-op="friendRemove" data-friend="f1"/);
assert.match(html, /1 friend request waiting for an answer/);
assert.doesNotMatch(html, /Broken/, "a friend row without an id never renders");

const empty = friends.renderHtml({ supported: true, friends: [], pendingIncoming: [], pendingOutgoingPlayerIds: [] });
assert.match(empty, /You have not added any friends yet\./);
assert.doesNotMatch(empty, /friendRemove/);

assert.match(friends.renderHtml({ supported: false }), /This server does not support friends\./);
assert.doesNotMatch(friends.renderHtml({ supported: false }), /friendRemove/);

const escaped = friends.renderHtml({
  supported: true,
  friends: [{ friendId: "f9", name: "<img src=x onerror=alert(1)>", online: false }]
});
assert.doesNotMatch(escaped, /<img/, "names are escaped before they reach the DOM");

const fallback = friends.normalize({ supported: true, pendingIncoming: [{ requestId: "fr-9" }] });
assert.equal(fallback.incoming[0].displayName, "Stranger", "a request without a name shows Stranger");

console.log("faction.friends.test: all checks passed.");
