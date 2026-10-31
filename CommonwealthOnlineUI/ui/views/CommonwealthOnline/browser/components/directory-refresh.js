(function () {
  "use strict";

  var DIRECTORY_UNAVAILABLE = "The server directory is unavailable right now. Use Direct Connect or the Local tab to join a server.";
  var MAX_SERVERS = 512;
  var MAX_RESERVED_WEBSITE_SERVERS = 64;

  function serversFromPayload(payload) {
    if (Array.isArray(payload)) return payload;
    if (payload && Array.isArray(payload.servers)) return payload.servers;
    return null;
  }

  function isLivePayload(payload) {
    if (!payload || Array.isArray(payload)) return false;
    return payload.heartbeatIntervalSeconds !== undefined || payload.ttlSeconds !== undefined;
  }

  function addressKey(entry) {
    return String(entry.host || entry.address || "").trim().toLowerCase() + ":" + (entry.port > 0 ? entry.port : 7777);
  }

  function normalizeEntry(raw, liveDefault) {
    var entry = {};
    Object.keys(raw).forEach(function (key) { entry[key] = raw[key]; });
    entry.host = String(raw.host || raw.address || "").trim();
    entry.port = parseInt(raw.port, 10) > 0 ? parseInt(raw.port, 10) : 7777;
    entry.live = raw.live === true || (raw.live === undefined && liveDefault === true);
    return entry;
  }

  function fillMissing(target, source) {
    ["description", "region", "tags", "discord", "modlist"].forEach(function (field) {
      var value = target[field];
      var empty = value === undefined || value === null || value === "" ||
        (Array.isArray(value) && value.length === 0);
      if (empty && source[field] !== undefined) target[field] = source[field];
    });
  }

  function mergeLists(lists) {
    var slots = [];
    var seen = {};
    lists.forEach(function (entries, source) {
      entries.forEach(function (entry) {
        var key = addressKey(entry);
        if (!Object.prototype.hasOwnProperty.call(seen, key)) {
          seen[key] = slots.length;
          slots.push({ entry: entry, source: source });
          return;
        }
        var existing = slots[seen[key]].entry;
        if (existing.live === entry.live) return;
        if (entry.live === true) {
          fillMissing(entry, existing);
          slots[seen[key]].entry = entry;
        } else {
          fillMissing(existing, entry);
        }
      });
    });

    if (slots.length <= MAX_SERVERS) {
      return slots.map(function (slot) { return slot.entry; });
    }

    var keep = slots.map(function () { return false; });
    var curatedSource = lists.length > 0 ? lists.length - 1 : 0;
    var kept = 0;
    slots.forEach(function (slot, index) {
      if (kept < MAX_SERVERS && slot.source === curatedSource && kept < MAX_RESERVED_WEBSITE_SERVERS) {
        keep[index] = true;
        kept += 1;
      }
    });
    slots.forEach(function (slot, index) {
      if (kept < MAX_SERVERS && !keep[index]) {
        keep[index] = true;
        kept += 1;
      }
    });
    return slots.filter(function (slot, index) { return keep[index]; }).map(function (slot) { return slot.entry; });
  }

  function query(urls, fetchUrl) {
    return new Promise(function (resolve) {
      var lists = [];
      var sawValidList = false;
      var index = 0;

      var tryNext = function () {
        if (index >= urls.length) {
          if (sawValidList) {
            resolve({ success: true, servers: mergeLists(lists), connectionStatus: "Online", directoryNotice: "" });
          } else {
            resolve({
              success: false,
              servers: [],
              connectionStatus: "Directory unavailable",
              directoryNotice: DIRECTORY_UNAVAILABLE
            });
          }
          return;
        }

        var url = urls[index++];
        Promise.resolve()
          .then(function () { return fetchUrl(url); })
          .then(function (payload) {
            var servers = serversFromPayload(payload);
            if (!servers) {
              throw new Error("Server directory JSON must contain a servers array.");
            }
            sawValidList = true;
            var liveDefault = isLivePayload(payload);
            lists.push(servers.map(function (raw) { return normalizeEntry(raw, liveDefault); }));
            tryNext();
          })
          .catch(function (err) {
            if (typeof console !== "undefined" && console.warn) {
              console.warn("[CO] Server directory fetch failed:", url, err);
            }
            tryNext();
          });
      };

      tryNext();
    });
  }

  window.CO_DirectoryRefresh = {
    directoryUnavailableMessage: DIRECTORY_UNAVAILABLE,
    maxServers: MAX_SERVERS,
    maxReservedWebsiteServers: MAX_RESERVED_WEBSITE_SERVERS,
    serversFromPayload: serversFromPayload,
    isLivePayload: isLivePayload,
    mergeLists: mergeLists,
    query: query
  };
})();
