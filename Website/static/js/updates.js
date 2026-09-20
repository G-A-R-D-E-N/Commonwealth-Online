(() => {
  const DATA_URL = "/data/changelogs.json";

  const els = {
    status: document.getElementById("updates-status"),
    feed: document.getElementById("updates-feed"),
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
    const date = new Date(/^\d{4}-\d{2}-\d{2}$/.test(String(iso)) ? `${iso}T00:00:00` : iso);
    if (Number.isNaN(date.getTime())) {
      return "-";
    }
    return new Intl.DateTimeFormat(undefined, {
      year: "numeric",
      month: "short",
      day: "numeric",
    }).format(date);
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

  const releaseSlug = (version) => {
    const tag = String(version || "release")
      .trim()
      .toLowerCase()
      .replace(/[^a-z0-9._-]+/g, "-")
      .replace(/-+/g, "-")
      .replace(/^-|-$/g, "");
    return tag || "release";
  };

  const renderLink = (url, label) =>
    url
      ? `<a class="co-btn co-btn--ghost" href="${escapeHtml(url)}" target="_blank" rel="noopener noreferrer">${label}</a>`
      : "";

  const renderEntry = (changelog) => {
    const slug = releaseSlug(changelog.version);
    const title = changelog.title || `Commonwealth Online ${changelog.version}`;
    const badges = `<span class="updates-badge updates-badge--tag">v${escapeHtml(changelog.version)}</span>`;

    return `
      <article class="updates-entry" id="${escapeHtml(slug)}" data-version="${escapeHtml(changelog.version)}">
        <header class="updates-entry__header">
          <div class="updates-entry__badges">${badges}</div>
          <h3 class="updates-entry__title">
            <a href="#${escapeHtml(slug)}">${escapeHtml(title)}</a>
          </h3>
          <p class="updates-entry__meta">${escapeHtml(formatDate(changelog.date))} · Version ${escapeHtml(changelog.version)}</p>
        </header>
        <div class="updates-entry__body repo-readme">
          ${renderMarkdown(changelog.markdown)}
        </div>
        <footer class="updates-entry__footer">
          ${renderLink(changelog.github, "View on GitHub")}
          ${renderLink(changelog.nexus, "View on Nexus Mods")}
        </footer>
      </article>
    `;
  };

  const renderEmpty = () => `
    <div class="updates-empty">
      <p class="repo-empty">
        No release notes have been published yet. Check back soon.
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

  // The repository browser now lives on GitHub; resolve it from the shared
  // link registry, falling back to the canonical URL when unavailable.
  // The repository browser now lives on GitHub.
  const repositoryUrl = "https://github.com/G-A-R-D-E-N/Commonwealth-Online";
  const loadReleases = async () => {
    setBusy(els.feed, true);
    setStatus("Loading changelogs", false, { busy: true });

    try {
      const response = await fetch(DATA_URL, { cache: "no-cache" });
      if (!response.ok) {
        throw new Error(`Changelog data returned ${response.status}`);
      }

      const list = await response.json();
      const changelogs = Array.isArray(list) ? list : [];

      if (!changelogs.length) {
        showEmpty();
        return;
      }

      els.feed.innerHTML = `<div class="updates-list">${changelogs.map(renderEntry).join("")}</div>`;
      setStatus(`${changelogs.length} changelog${changelogs.length === 1 ? "" : "s"} synced from GitHub.`);
      setBusy(els.feed, false);
      revealContent(els.feed);
      scrollToHash();
    } catch (error) {
      console.error(error);
      els.feed.innerHTML = `
        <p class="repo-empty">
          Could not load the release notes.
          <a href="${repositoryUrl}">Open the repository on GitHub</a> instead.
        </p>
      `;
      setStatus("Release sync failed.", true);
      setBusy(els.feed, false);
    }
  };

  window.addEventListener("hashchange", scrollToHash);
  loadReleases();
})();
