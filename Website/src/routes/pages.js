"use strict";

/**
 * Public pages. Each view receives a `page` object with the per-page locals
 * used by the shared partials (title, description, body class, active nav key
 * and any page-specific scripts).
 */

const express = require("express");

const router = express.Router();

router.get("/", (req, res) => {
  res.render("pages/home", {
    page: {
      title: "Commonwealth Online",
      description:
        "Commonwealth Online is a Fallout 4 multiplayer framework with dedicated servers, remote player sync, appearance and equipment replication, shared world state and custom PrismaUI menus. Supports both Fallout 4 Anniversary Edition and the original release. No DLC, mods or Creation Club content required unless other players have them.",
      bodyClass: "co-home-page",
      activeKey: "home",
      scripts: ["/static/js/links.js", "/static/js/navbar.js", "/static/js/script.js"],
    },
  });
});

router.get("/media", (req, res) => {
  res.render("pages/media", {
    page: {
      title: "Media - Commonwealth Online",
      description:
        "In-game screenshots and multiplayer demonstrations from Commonwealth Online on Fallout 4 Anniversary Edition and the original release. Remote players, equipment sync, community characters and custom menus.",
      bodyClass: "co-media-page",
      activeKey: "media",
      scripts: ["/static/js/links.js", "/static/js/navbar.js", "/static/js/script.js"],
    },
  });
});

router.get("/servers", (req, res) => {
  res.render("pages/servers", {
    page: {
      title: "Servers - Commonwealth Online",
      description:
        "Browse public Commonwealth Online servers for Fallout 4 Anniversary Edition and the original release. DLC, mods and Creation Club content are not required unless other players have them.",
      bodyClass: "co-servers-page",
      activeKey: "servers",
      scripts: ["/static/js/links.js", "/static/js/navbar.js", "/static/js/script.js", "/static/js/servers.js"],
      footerCopy:
        "Independent fan project for Fallout 4 multiplayer framework research. Not affiliated with Bethesda or Microsoft. Supports both Fallout 4 Anniversary Edition and the original release. DLC, mods and Creation Club content are not required unless other players have them. Public server entries are curated in servers/server.json and served through the Commonwealth Online API.",
    },
  });
});

router.get("/updates", (req, res) => {
  res.render("pages/updates", {
    page: {
      title: "Updates - Commonwealth Online",
      description:
        "Project updates and release notes for Commonwealth Online (Fallout 4 multiplayer, Anniversary Edition and the original release) pulled live from Gitea releases.",
      bodyClass: "co-updates-page",
      activeKey: "updates",
      scripts: [
        "/static/js/vendor/marked.min.js",
        "/static/js/links.js",
        "/static/js/navbar.js",
        "/static/js/script.js",
        "/static/js/updates.js",
      ],
      footerCopy:
        "Independent fan project for Fallout 4 multiplayer framework research. Not affiliated with Bethesda or Microsoft. Supports both Fallout 4 Anniversary Edition and the original release. DLC, mods and Creation Club content are not required unless other players have them. Updates are served live from the project Gitea releases.",
    },
  });
});

router.get("/repo", (req, res) => {
  res.render("pages/repo", {
    page: {
      title: "Repository - Commonwealth Online",
      description:
        "Browse the Commonwealth Online source repository for the Fallout 4 multiplayer framework. Live from the project Gitea instance.",
      bodyClass: "co-repo-page",
      activeKey: "repository",
      scripts: [
        "/static/js/vendor/marked.min.js",
        "/static/js/links.js",
        "/static/js/navbar.js",
        "/static/js/script.js",
        "/static/js/repo.js",
      ],
      footerCopy:
        "Independent fan project for Fallout 4 multiplayer framework research. Not affiliated with Bethesda or Microsoft. Supports both Fallout 4 Anniversary Edition and the original release. DLC, mods and Creation Club content are not required unless other players have them.",
      footerLinks: [
        { label: "Repository", href: "/repo" },
        { label: "Gitea", key: "github" },
        { label: "Discord", key: "discord" },
        { label: "Media", key: "media" },
        { label: "Servers", key: "servers" },
        { label: "Updates", key: "updates" },
      ],
    },
  });
});

/** Legacy static-site URLs, kept so existing links and bookmarks keep working. */
const legacyRedirects = {
  "/index.html": "/",
  "/media/index.html": "/media",
  "/servers/index.html": "/servers",
  "/updates/index.html": "/updates",
  "/repo/index.html": "/repo",
};

for (const [from, to] of Object.entries(legacyRedirects)) {
  router.get(from, (req, res) => res.redirect(301, to));
}

module.exports = router;
