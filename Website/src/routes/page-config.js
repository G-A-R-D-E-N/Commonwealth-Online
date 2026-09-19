"use strict";

const SERVER_FOOTER_COPY =
  "Independent fan project for Fallout 4 multiplayer framework research. Not affiliated with Bethesda or Microsoft. Supports both Fallout 4 Anniversary Edition and the original release. DLC, mods and Creation Club content are not required unless other players have them. Public server entries are curated in servers/server.json.";

const UPDATES_FOOTER_COPY =
  "Independent fan project for Fallout 4 multiplayer framework research. Not affiliated with Bethesda or Microsoft. Supports both Fallout 4 Anniversary Edition and the original release. DLC, mods and Creation Club content are not required unless other players have them. Updates are served live from the project Gitea releases.";

const PUBLIC_PAGES = [
  {
    route: "/",
    template: "pages/home",
    output: "index.html",
    page: {
      title: "Commonwealth Online",
      description:
        "Commonwealth Online is a Fallout 4 multiplayer framework with dedicated servers, remote player sync, appearance and equipment replication, shared world state and custom PrismaUI menus. Supports both Fallout 4 Anniversary Edition and the original release. No DLC, mods or Creation Club content required unless other players have them.",
      bodyClass: "co-home-page",
      activeKey: "home",
      scripts: ["/static/js/links.js", "/static/js/navbar.js", "/static/js/script.js"],
    },
  },
  {
    route: "/media",
    template: "pages/media",
    output: "media/index.html",
    page: {
      title: "Media - Commonwealth Online",
      description:
        "In-game screenshots and multiplayer demonstrations from Commonwealth Online on Fallout 4 Anniversary Edition and the original release. Remote players, equipment sync, community characters and custom menus.",
      bodyClass: "co-media-page",
      activeKey: "media",
      scripts: ["/static/js/links.js", "/static/js/navbar.js", "/static/js/script.js"],
    },
  },
  {
    route: "/roadmap",
    template: "pages/roadmap",
    output: "roadmap/index.html",
    page: {
      title: "Roadmap - Commonwealth Online",
      description:
        "Track current, next and future Commonwealth Online multiplayer framework development milestones.",
      bodyClass: "co-roadmap-page",
      activeKey: "roadmap",
      scripts: ["/static/js/links.js", "/static/js/navbar.js", "/static/js/script.js"],
    },
  },
  {
    route: "/servers",
    template: "pages/servers",
    output: "servers/index.html",
    page: {
      title: "Servers - Commonwealth Online",
      description:
        "Browse public Commonwealth Online servers for Fallout 4 Anniversary Edition and the original release. DLC, mods and Creation Club content are not required unless other players have them.",
      bodyClass: "co-servers-page",
      activeKey: "servers",
      scripts: ["/static/js/links.js", "/static/js/navbar.js", "/static/js/script.js", "/static/js/servers.js"],
      footerCopy: SERVER_FOOTER_COPY,
    },
  },
  {
    route: "/updates",
    template: "pages/updates",
    output: "updates/index.html",
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
      footerCopy: UPDATES_FOOTER_COPY,
    },
  },
  {
    route: "/repo",
    template: "pages/repo",
    output: "repo/index.html",
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
        { label: "Roadmap", href: "/roadmap" },
        { label: "Updates", key: "updates" },
      ],
    },
  },
];

const LEGACY_REDIRECTS = {
  "/index.html": "/",
  "/media/index.html": "/media",
  "/servers/index.html": "/servers",
  "/updates/index.html": "/updates",
  "/repo/index.html": "/repo",
};

const APPLICATIONS_PAGE = {
  template: "pages/applications",
  output: "apply/index.html",
  page: {
    title: "Apply - Commonwealth Online",
    description: "Apply to join the Commonwealth Online team or become a beta tester.",
    bodyClass: "co-apply-page",
    activeKey: "applications",
    scripts: ["/static/js/links.js", "/static/js/navbar.js", "/static/js/script.js"],
  },
};

const FORUM_PAGE = {
  template: "pages/forum",
  output: "forum/index.html",
  page: {
    title: "Forum - Commonwealth Online",
    description: "Community discussion for the Commonwealth Online multiplayer framework.",
    bodyClass: "co-forum-page",
    activeKey: "forum",
    scripts: ["/static/js/links.js", "/static/js/navbar.js", "/static/js/script.js"],
  },
};

const STATIC_SHELL_PAGES = [APPLICATIONS_PAGE, FORUM_PAGE];

module.exports = {
  APPLICATIONS_PAGE,
  FORUM_PAGE,
  LEGACY_REDIRECTS,
  PUBLIC_PAGES,
  STATIC_SHELL_PAGES,
};
