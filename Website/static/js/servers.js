(() => {
  const DATA_URL = "/data/servers.json";

  const els = {
    status: document.getElementById("servers-status"),
    feed: document.getElementById("servers-feed"),
    stats: document.getElementById("servers-stats"),
    count: document.getElementById("stat-server-count"),
    updated: document.getElementById("stat-server-updated"),
  };

  if (!els.feed) {
    return;
  }

  const prefersReducedMotion = () =>
    window.matchMedia("(prefers-reduced-motion: reduce)").matches;

  const escapeHtml = (value) =>
    String(value ?? "")
      .replaceAll("&", "&amp;")
      .replaceAll("<", "&lt;")
      .replaceAll(">", "&gt;")
      .replaceAll('"', "&quot;")
      .replaceAll("'", "&#39;");

  const setStatus = (message, isError = false, { busy = false } = {}) => {
    if (!els.status) {
      return;
    }
    els.status.classList.toggle("is-error", isError);
    els.status.classList.toggle("is-busy", busy && !isError);
    if (busy && !isError) {
      els.status.innerHTML = `<span class="status-pulse">${escapeHtml(message)}</span><span class="blink-cursor" aria-hidden="true"></span>`;
      return;
    }
    els.status.textContent = message;
  };

  const revealContent = (el) => {
    if (!el || prefersReducedMotion()) {
      return;
    }
    el.classList.remove("repo-reveal");
    void el.offsetWidth;
    el.classList.add("repo-reveal");
  };

  const formatUpdated = (value) => {
    if (!value) {
      return "-";
    }
    const date = new Date(value);
    if (Number.isNaN(date.getTime())) {
      return String(value);
    }
    return new Intl.DateTimeFormat(undefined, {
      year: "numeric",
      month: "short",
      day: "numeric",
    }).format(date);
  };

  const normalizeServer = (raw, index) => {
    if (!raw || typeof raw !== "object") {
      return null;
    }

    const host = String(raw.host ?? raw.address ?? "").trim();
    const port = Number(raw.port);
    const name = String(raw.name ?? "").trim();

    if (!host || !name || !Number.isFinite(port) || port <= 0) {
      return null;
    }

    const tags = Array.isArray(raw.tags)
      ? raw.tags.map((tag) => String(tag).trim()).filter(Boolean)
      : [];

    return {
      id: String(raw.id ?? `server-${index + 1}`).trim() || `server-${index + 1}`,
      name,
      host,
      port,
      address: `${host}:${port}`,
      description: String(raw.description ?? "").trim(),
      region: String(raw.region ?? "").trim(),
      tags,
      password: Boolean(raw.password),
      modlist: String(raw.modlist ?? "").trim(),
      discord: String(raw.discord ?? "").trim(),
    };
  };

  const renderTags = (server) => {
    const badges = [];
    if (server.region) {
      badges.push(`<span class="servers-badge servers-badge--region">${escapeHtml(server.region)}</span>`);
    }
    if (server.password) {
      badges.push(`<span class="servers-badge servers-badge--password">Password</span>`);
    }
    for (const tag of server.tags) {
      badges.push(`<span class="servers-badge">${escapeHtml(tag)}</span>`);
    }
    if (!badges.length) {
      return "";
    }
    return `<div class="servers-entry__badges">${badges.join("")}</div>`;
  };

  const renderServer = (server) => {
    const discordProtocol = globalThis.CoUrlPolicy?.protocolOf(server.discord, window.location.href);
    const discordLink = server.discord && ["http:", "https:"].includes(discordProtocol)
      ? `<a class="co-btn co-btn--ghost" href="${escapeHtml(server.discord)}" target="_blank" rel="noopener noreferrer">Discord</a>`
      : "";

    const modlist = server.modlist
      ? `<p class="servers-entry__modlist"><span>Mod notes</span>${escapeHtml(server.modlist)}</p>`
      : "";

    const description = server.description
      ? `<p class="servers-entry__desc">${escapeHtml(server.description)}</p>`
      : `<p class="servers-entry__desc servers-entry__desc--empty">No description provided.</p>`;

    return `
      <article class="servers-entry" id="${escapeHtml(server.id)}">
        <header class="servers-entry__header">
          ${renderTags(server)}
          <h3 class="servers-entry__title">${escapeHtml(server.name)}</h3>
          <p class="servers-entry__meta">
            <span class="servers-entry__address">${escapeHtml(server.address)}</span>
          </p>
        </header>
        ${description}
        ${modlist}
        <footer class="servers-entry__footer">
          <button class="co-btn co-btn--primary" type="button" data-copy-address="${escapeHtml(server.address)}">
            Copy address
          </button>
          <button
            class="co-btn co-btn--ghost"
            type="button"
            data-favorite-server
            data-server-id="${escapeHtml(server.id)}"
            data-server-name="${escapeHtml(server.name)}"
            hidden
          >Favorite</button>
          ${discordLink}
        </footer>
      </article>
    `;
  };

  const renderEmpty = () => `
    <div class="servers-empty">
      <p>No public servers are listed yet. Entries are curated in <code>servers/server.json</code>.</p>
    </div>
  `;

  const bindCopyButtons = () => {
    els.feed.querySelectorAll("[data-copy-address]").forEach((button) => {
      button.addEventListener("click", async () => {
        const address = button.getAttribute("data-copy-address");
        if (!address) {
          return;
        }

        const label = button.textContent;
        try {
          await navigator.clipboard.writeText(address);
          button.textContent = "Copied";
        } catch {
          button.textContent = "Copy failed";
        }

        window.setTimeout(() => {
          button.textContent = label;
        }, 1600);
      });
    });
  };

  const render = (payload) => {
    const servers = Array.isArray(payload?.servers)
      ? payload.servers.map(normalizeServer).filter(Boolean)
      : [];

    if (els.count) {
      els.count.textContent = String(servers.length);
    }
    if (els.updated) {
      els.updated.textContent = formatUpdated(payload?.updatedAt);
    }
    if (els.stats) {
      els.stats.hidden = false;
    }

    els.feed.removeAttribute("aria-busy");
    els.feed.innerHTML = servers.length
      ? `<div class="servers-list">${servers.map(renderServer).join("")}</div>`
      : renderEmpty();

    bindCopyButtons();
    document.dispatchEvent(new CustomEvent("co:servers-rendered"));
    revealContent(els.feed);

    setStatus(
      servers.length
        ? `${servers.length} public server${servers.length === 1 ? "" : "s"} listed`
        : "No public servers listed"
    );
  };

  const load = async () => {
    setStatus("Loading server list", false, { busy: true });
    els.feed.setAttribute("aria-busy", "true");

    try {
      const response = await fetch(DATA_URL, { cache: "no-cache" });
      if (!response.ok) {
        throw new Error(`HTTP ${response.status}`);
      }
      const payload = await response.json();
      render(payload);
    } catch (error) {
      els.feed.removeAttribute("aria-busy");
      els.feed.innerHTML = `
        <div class="servers-empty">
          <p>Could not reach the server list API.</p>
        </div>
      `;
      setStatus(`Failed to load server list${error?.message ? `: ${error.message}` : ""}`, true);
    }
  };

  load();
})();
