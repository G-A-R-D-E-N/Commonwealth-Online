(function () {
  "use strict";

  var DIRECTORY_UNAVAILABLE = "The server directory is unavailable right now. Use Direct Connect or the Local tab to join a server.";

  function serversFromPayload(payload) {
    if (Array.isArray(payload)) return payload;
    if (payload && Array.isArray(payload.servers)) return payload.servers;
    return null;
  }

  function query(urls, fetchUrl) {
    return new Promise(function (resolve) {
      var index = 0;
      var sawValidList = false;

      var tryNext = function () {
        if (index >= urls.length) {
          if (sawValidList) {
            resolve({ success: true, servers: [], connectionStatus: "Online", directoryNotice: "" });
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
            if (servers.length) {
              resolve({
                success: true,
                servers: servers,
                connectionStatus: "Online",
                directoryNotice: ""
              });
              return;
            }
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
    serversFromPayload: serversFromPayload,
    query: query
  };
})();
