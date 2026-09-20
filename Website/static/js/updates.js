(() => {
  const CONFIG = {
    base: "https://git.zambazosmedia.group",
    owner: "Commonwealth-Online",
    repo: "Commonwealth-Online-Public",
    pageSize: 30,
    fetchRetries: 2,
  };

  const apiRoot = `${CONFIG.base}/api/v1/repos/${CONFIG.owner}/${CONFIG.repo}`;
  const htmlRoot = `${CONFIG.base}/${CONFIG.owner}/${CONFIG.repo}`;

  const els = {
    status: document.getElementById("updates-status"),
    feed: document.getElementById("updates-feed"),
    openGitea: document.getElementById("updates-open-gitea"),
  };

  if (!els.feed) {
    return;
  }

  const prefersReducedMotion = () =>
    window.matchMedia("(prefers-reduced-motion: reduce)").matches;

  const escapeHtml = (value) =>
    String(value)
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

  const setBusy = (el, busy) => {
    if (!el) {
      return;
    }
    if (busy) {
      el.setAttribute("aria-busy", "true");
    } else {
      el.removeAttribute("aria-busy");
    }
  };

  const revealContent = (el) => {
    if (!el || prefersReducedMotion()) {
      return;
    }
    el.classList.remove("repo-reveal");
    void el.offsetWidth;
    el.classList.add("repo-reveal");
  };

  const formatDate = (iso) => {
    if (!iso) {
      return "-";
    }
    const date = new Date(iso);
    if (Number.isNaN(date.getTime())) {
      return "-";
    }
    return new Intl.DateTimeFormat(undefined, {
      year: "numeric",
      month: "short",
      day: "numeric",
    }).format(date);
  };

  const formatRelative = (iso) => {
    if (!iso) {
      return "";
    }
    const date = new Date(iso);
    if (Number.isNaN(date.getTime())) {
      return "";
    }
    const deltaSec = Math.round((date.getTime() - Date.now()) / 1000);
    const abs = Math.abs(deltaSec);
    const rtf = new Intl.RelativeTimeFormat(undefined, { numeric: "auto" });
    const units = [
      ["year", 31536000],
      ["month", 2592000],
      ["week", 604800],
      ["day", 86400],
      ["hour", 3600],
      ["minute", 60],
    ];
    for (const [unit, seconds] of units) {
      if (abs >= seconds || unit === "minute") {
        return rtf.format(Math.round(deltaSec / seconds), unit);
      }
    }
    return rtf.format(deltaSec, "second");
  };

  const formatBytes = (bytes) => {
    const n = Number(bytes) || 0;
    if (n < 1024) {
      return `${n} B`;
    }
    if (n < 1024 * 1024) {
      return `${(n / 1024).toFixed(1)} KB`;
    }
    return `${(n / (1024 * 1024)).toFixed(1)} MB`;
  };

  const fetchWithRetry = async (url, options = {}, retries = CONFIG.fetchRetries) => {
    let lastError;
    for (let attempt = 0; attempt <= retries; attempt += 1) {
      try {
        const response = await fetch(url, {
          ...options,
          cache: attempt === 0 ? options.cache : "no-store",
        });
        if (response.ok || response.status < 500 || attempt === retries) {
          return response;
        }
        lastError = new Error(`HTTP ${response.status}`);
      } catch (error) {
        lastError = error;
        if (attempt === retries) {
          throw lastError;
        }
      }
      await new Promise((resolve) => window.setTimeout(resolve, 280 * (attempt + 1)));
    }
    throw lastError || new Error("Request failed");
  };

  const sanitizeHtml = (html) => {
    const template = document.createElement("template");
    template.innerHTML = html;

    const allowedTags = new Set([
      "a", "blockquote", "br", "code", "del", "em", "h1", "h2", "h3", "h4", "h5", "h6",
      "hr", "img", "li", "ol", "p", "pre", "strong", "table", "tbody", "td", "th", "thead",
      "tr", "ul",
    ]);
    const allowedAttributes = {
      a: new Set(["href", "title"]),
      code: new Set(["class"]),
      img: new Set(["alt", "height", "loading", "src", "title", "width"]),
      td: new Set(["align"]),
      th: new Set(["align"]),
    };

    template.content.querySelectorAll("*").forEach((node) => {
      const tag = node.tagName.toLowerCase();
      if (!allowedTags.has(tag)) {
        node.remove();
        return;
      }

      const allowed = allowedAttributes[tag] || new Set();
      [...node.attributes].forEach((attr) => {
        if (!allowed.has(attr.name.toLowerCase())) {
          node.removeAttribute(attr.name);
        }
      });

      if (tag === "a") {
        node.removeAttribute("target");
        node.removeAttribute("rel");
        if (node.hasAttribute("href")) {
          const href = node.getAttribute("href") || "";
          if (!globalThis.CoUrlPolicy?.isSafeLink(href, window.location.href)) {
            node.removeAttribute("href");
          } else if (globalThis.CoUrlPolicy?.isExternalHttp(href, window.location.href)) {
            node.setAttribute("target", "_blank");
            node.setAttribute("rel", "noopener noreferrer");
          }
        }
      }

      if (tag === "img" && node.hasAttribute("src")) {
        const src = node.getAttribute("src") || "";
        if (!globalThis.CoUrlPolicy?.isSafeImage(src, window.location.href)) {
          node.removeAttribute("src");
        } else {
          node.setAttribute("loading", "lazy");
        }
      }
    });

    return template.innerHTML;
  };

  const renderMarkdown = (markdown) => {
    const source = String(markdown || "").trim();
    if (!source) {
      return `<p class="updates-entry__empty-body">No release notes were published for this tag.</p>`;
    }

    if (typeof marked === "undefined" || typeof marked.parse !== "function") {
      return `<pre class="repo-readme__plain">${escapeHtml(source)}</pre>`;
    }

    marked.setOptions({
      gfm: true,
      breaks: false,
    });

    return sanitizeHtml(marked.parse(source));
  };

  const releaseSlug = (release) => {
    const tag = String(release.tag_name || release.name || release.id || "release")
      .trim()
      .toLowerCase()
      .replace(/[^a-z0-9._-]+/g, "-")
      .replace(/-+/g, "-")
      .replace(/^-|-$/g, "");
    return tag || `release-${release.id || "0"}`;
  };

  const releaseTitle = (release) => {
    const name = String(release.name || "").trim();
    const tag = String(release.tag_name || "").trim();
    if (name) {
      return name;
    }
    if (tag) {
      return tag;
    }
    return "Untitled release";
  };

  const releaseBadges = (release) => {
    const badges = [];
    const tag = String(release.tag_name || "").trim();
    if (tag) {
      badges.push({ label: tag, kind: "tag" });
    }
    if (release.draft) {
      badges.push({ label: "draft", kind: "draft" });
    }
    if (release.prerelease) {
      badges.push({ label: "pre-release", kind: "pre" });
    }
    if (!release.draft && !release.prerelease && tag) {
      badges.push({ label: "stable", kind: "stable" });
    }
    return badges;
  };

  const assetDownloadUrl = (asset) =>
    asset.browser_download_url ||
    asset.download_url ||
    (asset.id ? `${apiRoot}/releases/assets/${asset.id}` : "#");

  const renderAssets = (release) => {
    const assets = Array.isArray(release.assets) ? release.assets : [];
    const rows = [];

    if (assets.length) {
      assets.forEach((asset) => {
        const name = asset.name || "download";
        const size = formatBytes(asset.size);
        const downloads = Number(asset.download_count) || 0;
        const href = assetDownloadUrl(asset);
        rows.push(`
          <li>
            <a class="updates-asset" href="${escapeHtml(href)}" target="_blank" rel="noopener noreferrer">
              <span class="updates-asset__name">${escapeHtml(name)}</span>
              <span class="updates-asset__meta">${escapeHtml(size)} · ${downloads} download${downloads === 1 ? "" : "s"}</span>
            </a>
          </li>
        `);
      });
    }

    if (release.zipball_url) {
      rows.push(`
        <li>
          <a class="updates-asset" href="${escapeHtml(release.zipball_url)}" target="_blank" rel="noopener noreferrer">
            <span class="updates-asset__name">Source code (zip)</span>
            <span class="updates-asset__meta">archive</span>
          </a>
        </li>
      `);
    }

    if (release.tarball_url) {
      rows.push(`
        <li>
          <a class="updates-asset" href="${escapeHtml(release.tarball_url)}" target="_blank" rel="noopener noreferrer">
            <span class="updates-asset__name">Source code (tar.gz)</span>
            <span class="updates-asset__meta">archive</span>
          </a>
        </li>
      `);
    }

    if (!rows.length) {
      return "";
    }

    return `
      <div class="updates-entry__files">
        <p class="updates-entry__files-label">Files</p>
        <ul class="updates-assets">${rows.join("")}</ul>
      </div>
    `;
  };

  const renderEntry = (release) => {
    const slug = releaseSlug(release);
    const title = releaseTitle(release);
    const when = release.published_at || release.created_at;
    const author = release.author?.login || release.author?.username || "";
    const htmlUrl = release.html_url || `${htmlRoot}/releases/tag/${encodeURIComponent(release.tag_name || "")}`;
    const badges = releaseBadges(release)
      .map(
        (badge) =>
          `<span class="updates-badge updates-badge--${escapeHtml(badge.kind)}">${escapeHtml(badge.label)}</span>`
      )
      .join("");

    const metaParts = [formatDate(when)];
    const relative = formatRelative(when);
    if (relative) {
      metaParts.push(relative);
    }
    if (author) {
      metaParts.push(author);
    }

    return `
      <article class="updates-entry" id="${escapeHtml(slug)}" data-tag="${escapeHtml(release.tag_name || "")}">
        <header class="updates-entry__header">
          <div class="updates-entry__badges">${badges}</div>
          <h3 class="updates-entry__title">
            <a href="#${escapeHtml(slug)}">${escapeHtml(title)}</a>
          </h3>
          <p class="updates-entry__meta">${escapeHtml(metaParts.join(" · "))}</p>
        </header>
        <div class="updates-entry__body repo-readme">
          ${renderMarkdown(release.body)}
        </div>
        ${renderAssets(release)}
        <footer class="updates-entry__footer">
          <a class="co-btn co-btn--ghost" href="${escapeHtml(htmlUrl)}" target="_blank" rel="noopener noreferrer">
            View on Gitea
          </a>
        </footer>
      </article>
    `;
  };

  const renderEmpty = () => `
    <div class="updates-empty">
      <p class="repo-empty">
        There are no updates yet. When releases are published on
        <a href="${escapeHtml(htmlRoot)}/releases" target="_blank" rel="noopener noreferrer">Gitea</a>,
        they will appear here.
      </p>
    </div>
  `;

  const scrollToHash = () => {
    const hash = decodeURIComponent((window.location.hash || "").replace(/^#/, ""));
    if (!hash) {
      return;
    }
    const target = document.getElementById(hash);
    if (!target) {
      return;
    }
    target.classList.add("is-targeted");
    target.scrollIntoView({
      behavior: prefersReducedMotion() ? "auto" : "smooth",
      block: "start",
    });
  };

  const showEmpty = () => {
    els.feed.innerHTML = renderEmpty();
    setStatus("There are no updates.");
    setBusy(els.feed, false);
    revealContent(els.feed);
  };

  const loadReleases = async () => {
    setBusy(els.feed, true);
    setStatus("Fetching releases", false, { busy: true });

    try {
      const response = await fetchWithRetry(`${apiRoot}/releases?limit=${CONFIG.pageSize}`);

      // Gitea/Cloudflare may 403/404 the releases API when none are published publicly.
      if (response.status === 403 || response.status === 404) {
        showEmpty();
        return;
      }

      if (!response.ok) {
        throw new Error(`Releases API returned ${response.status}`);
      }

      const releases = await response.json();
      const list = Array.isArray(releases) ? releases : [];
      const published = list.filter((release) => !release.draft);

      if (!published.length) {
        showEmpty();
        return;
      }

      els.feed.innerHTML = `<div class="updates-list">${published.map(renderEntry).join("")}</div>`;
      setStatus(`${published.length} update${published.length === 1 ? "" : "s"} synced from Gitea.`);
      setBusy(els.feed, false);
      revealContent(els.feed);
      scrollToHash();
    } catch (error) {
      console.error(error);
      els.feed.innerHTML = `
        <p class="repo-empty">
          Could not load releases from Gitea.
          <a href="${escapeHtml(htmlRoot)}/releases" target="_blank" rel="noopener noreferrer">Open releases on Gitea</a>
          instead.
        </p>
      `;
      setStatus("Release sync failed.", true);
      setBusy(els.feed, false);
    }
  };

  if (els.openGitea) {
    els.openGitea.href = `${htmlRoot}/releases`;
  }

  window.addEventListener("hashchange", scrollToHash);
  loadReleases();
})();
