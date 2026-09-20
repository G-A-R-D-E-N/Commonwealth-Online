(() => {
  // Root-absolute hrefs: pages are served at clean URLs by the Node server
  // (/media, /servers, /updates, /repo) instead of nested index.html files.
  const CO_LINKS = {
    github: "https://github.com/G-A-R-D-E-N/Commonwealth-Online",
    repository: "https://github.com/G-A-R-D-E-N/Commonwealth-Online",
    githubReleases: "https://github.com/G-A-R-D-E-N/Commonwealth-Online/releases",
    nexus: "https://www.nexusmods.com/fallout4/mods/107542",
    discord: "https://discord.gg/GyfxYG2gzH",
    updates: "/updates",
    media: "/media",
    servers: "/servers",
    forum: "/forum",
    applications: "/apply",
    documentation: "#",
  };
  const isExternal = (url) => /^https?:\/\//i.test(url);

  document.querySelectorAll("[data-co-link]").forEach((link) => {
    const key = link.getAttribute("data-co-link");
    const url = CO_LINKS[key];

    if (!url) {
      return;
    }

    link.setAttribute("href", url);

    if (isExternal(url)) {
      link.setAttribute("target", "_blank");
      link.setAttribute("rel", "noopener noreferrer");
    }
  });
})();
