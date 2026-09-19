(() => {
  const CONFIG = {
    base: "https://git.zambazosmedia.group",
    owner: "Commonwealth-Online",
    repo: "Commonwealth-Online-Public",
    commitPreview: 6,
    commitPageSize: 6,
    iconThemeCdn: "https://cdn.jsdelivr.net/npm/material-icon-theme@5.36.1",
    textExtensions: new Set([
      "",
      "md",
      "txt",
      "json",
      "js",
      "ts",
      "css",
      "html",
      "htm",
      "xml",
      "yml",
      "yaml",
      "toml",
      "ini",
      "cfg",
      "conf",
      "bat",
      "cmd",
      "ps1",
      "sh",
      "py",
      "c",
      "cc",
      "cpp",
      "h",
      "hpp",
      "cs",
      "java",
      "go",
      "rs",
      "lua",
      "cmake",
      "psc",
      "papyrus",
      "gitignore",
      "gitattributes",
      "gitmodules",
      "editorconfig",
      "license",
      "dockerfile",
      "makefile",
    ]),
  };

  const apiRoot = `${CONFIG.base}/api/v1/repos/${CONFIG.owner}/${CONFIG.repo}`;
  const htmlRoot = `${CONFIG.base}/${CONFIG.owner}/${CONFIG.repo}`;

  const els = {
    status: document.getElementById("repo-status"),
    fullName: document.getElementById("repo-full-name"),
    title: document.getElementById("repo-title"),
    description: document.getElementById("repo-description"),
    openGitea: document.getElementById("repo-open-gitea"),
    copyClone: document.getElementById("repo-copy-clone"),
    cloneWrap: document.getElementById("repo-clone"),
    cloneInput: document.getElementById("repo-clone-input"),
    stats: {
      branch: document.getElementById("stat-branch"),
      language: document.getElementById("stat-language"),
      commits: document.getElementById("stat-commits"),
      branches: document.getElementById("stat-branches"),
      watchers: document.getElementById("stat-watchers"),
      updated: document.getElementById("stat-updated"),
    },
    breadcrumb: document.getElementById("repo-breadcrumb"),
    files: document.getElementById("repo-files"),
    commits: document.getElementById("repo-commits"),
    readmeSection: document.getElementById("repo-readme-section"),
    readmeTitle: document.getElementById("repo-readme-title"),
    docsSidebar: document.querySelector(".repo-docs__sidebar"),
    docTabs: document.getElementById("repo-doc-tabs"),
    outline: document.getElementById("repo-outline"),
    outlineList: document.getElementById("repo-outline-list"),
    readme: document.getElementById("repo-readme"),
    fileSection: document.getElementById("repo-file-section"),
    fileTitle: document.getElementById("repo-file-title"),
    fileRaw: document.getElementById("repo-file-raw"),
    fileContent: document.getElementById("repo-file-content"),
    langs: document.getElementById("repo-langs"),
  };

  if (!els.files || !els.commits || !els.readme) {
    return;
  }

  let defaultBranch = "main";
  let copyResetTimer = 0;
  let rootDocEntries = [];
  let activeDoc = "";
  let tabsWired = false;
  let docSwapToken = 0;
  let outlineObserver = null;
  let outlineWired = false;
  let iconTheme = null;
  let iconThemePromise = null;
  let paintedTree = { path: "", entries: null };
  let pageUnloading = false;
  let bootstrapToken = 0;
  const DOC_SWAP_MS = 220;
  const FETCH_RETRIES = 2;

  const prefersReducedMotion = () =>
    window.matchMedia("(prefers-reduced-motion: reduce)").matches;

  const waitForClassAnimation = (el, className, fallbackMs = DOC_SWAP_MS) =>
    new Promise((resolve) => {
      if (!el || prefersReducedMotion()) {
        el?.classList.remove("is-leaving", "is-entering");
        resolve();
        return;
      }

      let done = false;
      const finish = () => {
        if (done) {
          return;
        }
        done = true;
        el.removeEventListener("animationend", onEnd);
        el.classList.remove(className);
        resolve();
      };
      const onEnd = (event) => {
        if (event.target !== el) {
          return;
        }
        finish();
      };

      el.classList.remove("is-leaving", "is-entering");
      // Force reflow so repeated swaps restart cleanly.
      void el.offsetWidth;
      el.addEventListener("animationend", onEnd);
      el.classList.add(className);
      window.setTimeout(finish, fallbackMs + 80);
    });

  const setDocDirection = (fromName, toName) => {
    if (!els.readme) {
      return;
    }
    const fromIndex = rootDocEntries.findIndex((entry) => entry.name === fromName);
    const toIndex = rootDocEntries.findIndex((entry) => entry.name === toName);
    const forward = fromIndex < 0 || toIndex < 0 || toIndex >= fromIndex;
    els.readme.dataset.dir = forward ? "forward" : "back";
  };

  const slugifyHeading = (text) =>
    String(text || "")
      .trim()
      .toLowerCase()
      .replace(/[`*_~]/g, "")
      .replace(/[^a-z0-9\s-]/g, "")
      .replace(/\s+/g, "-")
      .replace(/-+/g, "-")
      .replace(/^-|-$/g, "") || "section";

  const clearOutline = () => {
    if (outlineObserver) {
      outlineObserver.disconnect();
      outlineObserver = null;
    }
    if (els.outlineList) {
      els.outlineList.innerHTML = "";
    }
    if (els.outline) {
      els.outline.hidden = true;
    }
    document.body.classList.remove("has-repo-outline");
  };

  const setActiveOutlineLink = (id) => {
    if (!els.outlineList || !id) {
      return;
    }
    els.outlineList.querySelectorAll("a").forEach((link) => {
      const active = link.getAttribute("href") === `#${id}`;
      link.classList.toggle("is-active", active);
      if (active) {
        link.setAttribute("aria-current", "true");
      } else {
        link.removeAttribute("aria-current");
      }
    });
  };

  const wireOutlineNav = () => {
    if (!els.outlineList || outlineWired) {
      return;
    }
    outlineWired = true;
    els.outlineList.addEventListener("click", (event) => {
      const link = event.target instanceof HTMLElement ? event.target.closest("a[href^='#']") : null;
      if (!link) {
        return;
      }
      const id = (link.getAttribute("href") || "").slice(1);
      const target = id ? document.getElementById(id) : null;
      if (!target) {
        return;
      }
      event.preventDefault();
      setActiveOutlineLink(id);
      target.scrollIntoView({
        behavior: prefersReducedMotion() ? "auto" : "smooth",
        block: "start",
      });
      history.replaceState(history.state, "", `#${encodeURIComponent(id)}`);
    });
  };

  const buildOutline = () => {
    if (!els.readme || !els.outline || !els.outlineList) {
      return;
    }

    clearOutline();
    wireOutlineNav();

    const headings = [...els.readme.querySelectorAll("h1, h2, h3, h4")];
    if (!headings.length) {
      return;
    }

    const usedIds = new Set();
    const items = headings.map((heading) => {
      const level = Number(heading.tagName.slice(1));
      const text = heading.textContent?.trim() || "Section";
      const baseId = heading.id || slugifyHeading(text);
      let id = baseId;
      let suffix = 2;
      while (usedIds.has(id) || (document.getElementById(id) && document.getElementById(id) !== heading)) {
        id = `${baseId}-${suffix++}`;
      }
      usedIds.add(id);
      heading.id = id;
      heading.classList.add("repo-readme__anchor");
      return { level, text, id, heading };
    });

    const minLevel = Math.min(...items.map((item) => item.level));
    els.outlineList.innerHTML = items
      .map(
        (item) => `
          <li class="repo-outline__item repo-outline__item--h${item.level}" style="--outline-depth:${item.level - minLevel}">
            <a href="#${escapeHtml(item.id)}">${escapeHtml(item.text)}</a>
          </li>
        `
      )
      .join("");

    els.outline.hidden = false;
    document.body.classList.add("has-repo-outline");

    if ("IntersectionObserver" in window) {
      const visible = new Map();
      outlineObserver = new IntersectionObserver(
        (entries) => {
          entries.forEach((entry) => {
            if (entry.isIntersecting) {
              visible.set(entry.target.id, entry.intersectionRatio);
            } else {
              visible.delete(entry.target.id);
            }
          });

          let bestId = "";
          let bestRatio = -1;
          visible.forEach((ratio, id) => {
            if (ratio > bestRatio) {
              bestRatio = ratio;
              bestId = id;
            }
          });

          if (!bestId && items.length) {
            const above = items
              .map((item) => item.heading)
              .filter((heading) => heading.getBoundingClientRect().top <= 120)
              .pop();
            bestId = above?.id || items[0].id;
          }

          if (bestId) {
            setActiveOutlineLink(bestId);
          }
        },
        {
          rootMargin: "-12% 0px -62% 0px",
          threshold: [0.1, 0.35, 0.6],
        }
      );

      items.forEach((item) => outlineObserver.observe(item.heading));
    } else if (items[0]) {
      setActiveOutlineLink(items[0].id);
    }

    const hashId = decodeURIComponent((window.location.hash || "").replace(/^#/, ""));
    if (hashId && document.getElementById(hashId)) {
      setActiveOutlineLink(hashId);
    }
  };

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

  const loadingMarkup = {
    files: () => `
      <div class="repo-loading" data-loading="files">
        <p class="repo-loading__label">
          <span class="status-pulse">Syncing file tree</span><span class="blink-cursor" aria-hidden="true"></span>
        </p>
        <div class="repo-file-list repo-file-list--skel" aria-hidden="true">
          <div class="repo-skel-row"><span class="repo-skel-icon"></span><span class="repo-skel-line repo-skel-line--md"></span><span class="repo-skel-line repo-skel-line--xs"></span></div>
          <div class="repo-skel-row"><span class="repo-skel-icon"></span><span class="repo-skel-line repo-skel-line--lg"></span><span class="repo-skel-line repo-skel-line--xs"></span></div>
          <div class="repo-skel-row"><span class="repo-skel-icon"></span><span class="repo-skel-line repo-skel-line--sm"></span><span class="repo-skel-line repo-skel-line--xs"></span></div>
          <div class="repo-skel-row"><span class="repo-skel-icon"></span><span class="repo-skel-line repo-skel-line--md"></span><span class="repo-skel-line repo-skel-line--xs"></span></div>
          <div class="repo-skel-row"><span class="repo-skel-icon"></span><span class="repo-skel-line repo-skel-line--lg"></span><span class="repo-skel-line repo-skel-line--xs"></span></div>
          <div class="repo-skel-row"><span class="repo-skel-icon"></span><span class="repo-skel-line repo-skel-line--sm"></span><span class="repo-skel-line repo-skel-line--xs"></span></div>
        </div>
      </div>
    `,
    readme: () => `
      <div class="repo-loading" data-loading="readme">
        <p class="repo-loading__label">
          <span class="status-pulse">Loading document</span><span class="blink-cursor" aria-hidden="true"></span>
        </p>
        <div class="repo-skel-doc" aria-hidden="true">
          <span class="repo-skel-line repo-skel-line--title"></span>
          <span class="repo-skel-line repo-skel-line--lg"></span>
          <span class="repo-skel-line repo-skel-line--lg"></span>
          <span class="repo-skel-line repo-skel-line--md"></span>
          <span class="repo-skel-line repo-skel-line--lg"></span>
          <span class="repo-skel-line repo-skel-line--sm"></span>
          <span class="repo-skel-line repo-skel-line--lg"></span>
          <span class="repo-skel-line repo-skel-line--md"></span>
        </div>
      </div>
    `,
    file: () => "Loading…",
  };

  const showFilesLoading = () => {
    setBusy(els.files, true);
    els.files.innerHTML = loadingMarkup.files();
  };

  const showReadmeLoading = () => {
    setBusy(els.readme, true);
    els.readme.innerHTML = loadingMarkup.readme();
  };

  const hideDocTabSkeleton = () => {
    const skel = document.getElementById("repo-doc-tabs-skel");
    if (skel) {
      skel.hidden = true;
      skel.remove();
    }
  };

  const escapeHtml = (value) =>
    String(value)
      .replaceAll("&", "&amp;")
      .replaceAll("<", "&lt;")
      .replaceAll(">", "&gt;")
      .replaceAll('"', "&quot;")
      .replaceAll("'", "&#39;");

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

  const getRoute = () => {
    const params = new URLSearchParams(window.location.search);
    return {
      path: (params.get("path") || "").replace(/^\/+|\/+$/g, ""),
      view: params.get("view") || "",
      doc: (params.get("doc") || "").replace(/^\/+|\/+$/g, ""),
    };
  };

  const routeHref = (path = "", view = "") => {
    const params = new URLSearchParams();
    if (path) {
      params.set("path", path);
    }
    if (view) {
      params.set("view", view);
    }
    const query = params.toString();
    return query ? `?${query}` : "./";
  };

  const docHref = (name) => `?doc=${encodeURIComponent(name)}`;

  const ROOT_DOC_BASENAMES = new Set([
    "readme",
    "changelog",
    "contributing",
    "disclaimer",
    "license",
    "licence",
    "copying",
    "authors",
    "credits",
    "notice",
    "security",
    "code_of_conduct",
    "code-of-conduct",
  ]);

  const isRootDocName = (name) => {
    if (!name || name.includes("/")) {
      return false;
    }

    if (/\.(md|txt)$/i.test(name)) {
      return true;
    }

    // Extensionless root docs (LICENSE, COPYING, AUTHORS, …).
    const base = name.toLowerCase();
    if (ROOT_DOC_BASENAMES.has(base)) {
      return true;
    }

    return /^(license|licence)([-_.].+)?$/i.test(name) && !name.includes(".");
  };

  const sortRootDocs = (a, b) => {
    const rank = (name) => {
      if (/^readme(\.md|\.txt)?$/i.test(name)) {
        return 0;
      }
      if (/^changelog(\.md|\.txt)?$/i.test(name)) {
        return 1;
      }
      if (/^contributing(\.md|\.txt)?$/i.test(name)) {
        return 2;
      }
      if (/^(license|licence)([-_.].+)?(\.md|\.txt)?$/i.test(name)) {
        return 3;
      }
      return 4;
    };
    const diff = rank(a.name) - rank(b.name);
    if (diff !== 0) {
      return diff;
    }
    return a.name.localeCompare(b.name, undefined, { sensitivity: "base" });
  };

  const defaultRootDoc = () => {
    const readme = rootDocEntries.find((entry) => /^readme\.md$/i.test(entry.name));
    return readme?.name || rootDocEntries[0]?.name || "README.md";
  };

  const rawUrl = (path) =>
    `${CONFIG.base}/${CONFIG.owner}/${CONFIG.repo}/raw/branch/${encodeURIComponent(defaultBranch)}/${path
      .split("/")
      .map(encodeURIComponent)
      .join("/")}`;

  const giteaFileUrl = (path) =>
    `${htmlRoot}/src/branch/${encodeURIComponent(defaultBranch)}/${path
      .split("/")
      .map(encodeURIComponent)
      .join("/")}`;

  const giteaCommitUrl = (sha) => `${htmlRoot}/commit/${encodeURIComponent(sha)}`;

  const extensionOf = (name) => {
    const base = name.split("/").pop() || "";
    if (!base.includes(".")) {
      return base.toLowerCase();
    }
    return (base.split(".").pop() || "").toLowerCase();
  };

  const isTextFile = (name) => CONFIG.textExtensions.has(extensionOf(name));

  const isAbortError = (error) =>
    error?.name === "AbortError" ||
    (typeof DOMException !== "undefined" && error instanceof DOMException && error.name === "AbortError");

  const isTransientFetchError = (error) => {
    if (isAbortError(error) || pageUnloading) {
      return false;
    }
    // Browsers surface cancelled / flaky cross-origin requests as TypeError.
    return error instanceof TypeError || error?.name === "TypeError";
  };

  const fetchWithRetry = async (url, options = {}, retries = FETCH_RETRIES) => {
    let lastError;
    for (let attempt = 0; attempt <= retries; attempt += 1) {
      if (pageUnloading) {
        const abort = new DOMException("Page is unloading", "AbortError");
        throw abort;
      }
      try {
        // Add cache-busting parameter on retries to bypass ServiceWorker cache
        let fetchUrl = url;
        if (attempt > 0) {
          const separator = url.includes("?") ? "&" : "?";
          fetchUrl = `${url}${separator}_retry=${attempt}`;
        }
        return await fetch(fetchUrl, options);
      } catch (error) {
        lastError = error;
        if (isAbortError(error) || pageUnloading || !isTransientFetchError(error) || attempt === retries) {
          throw error;
        }
        await new Promise((resolve) => {
          window.setTimeout(resolve, 150 * (attempt + 1));
        });
      }
    }
    throw lastError;
  };

  const apiFetchResponse = async (path) => {
    const response = await fetchWithRetry(`${apiRoot}${path}`, {
      headers: { Accept: "application/json" },
      cache: "no-store",
    });
    if (!response.ok) {
      throw new Error(`Gitea API ${response.status} for ${path}`);
    }
    return response;
  };

  const apiFetch = async (path) => {
    const response = await apiFetchResponse(path);
    return response.json();
  };

  const parseTotalCount = (response) => {
    const raw =
      response.headers.get("X-Total-Count") ||
      response.headers.get("X-Total") ||
      "";
    const total = Number(raw);
    return Number.isFinite(total) && total >= 0 ? total : null;
  };

  const fetchText = async (path) => {
    // Gitea /raw responses are Cache-Control: max-age=21600 — bypass browser cache.
    const response = await fetchWithRetry(
      `${apiRoot}/raw/${path.split("/").map(encodeURIComponent).join("/")}?ref=${encodeURIComponent(defaultBranch)}`,
      { cache: "no-store" }
    );
    if (!response.ok) {
      throw new Error(`Could not load ${path}`);
    }
    return response.text();
  };

  const sanitizeReadmeHtml = (html) => {
    const template = document.createElement("template");
    template.innerHTML = html;

    template.content.querySelectorAll("script, iframe, object, embed, form, link, meta").forEach((node) => {
      node.remove();
    });

    template.content.querySelectorAll("*").forEach((node) => {
      [...node.attributes].forEach((attr) => {
        const name = attr.name.toLowerCase();
        const value = attr.value.trim();
        if (name.startsWith("on") || name === "srcdoc") {
          node.removeAttribute(attr.name);
          return;
        }
        if ((name === "href" || name === "src" || name === "xlink:href") && /^javascript:/i.test(value)) {
          node.removeAttribute(attr.name);
        }
      });
    });

    return template.innerHTML;
  };

  const rewriteReadmeUrls = (html, readmeDir) => {
    const template = document.createElement("template");
    template.innerHTML = html;
    const baseDir = readmeDir ? `${readmeDir}/` : "";

    const resolveRepoPath = (value) => {
      if (!value || /^(https?:|mailto:|data:|#)/i.test(value)) {
        return null;
      }
      const cleaned = value.replace(/^\.\//, "");
      if (cleaned.startsWith("../")) {
        return null;
      }
      return `${baseDir}${cleaned}`.replace(/^\/+/, "");
    };

    template.content.querySelectorAll("img[src]").forEach((img) => {
      const resolved = resolveRepoPath(img.getAttribute("src") || "");
      if (resolved) {
        img.setAttribute("src", rawUrl(resolved));
        img.setAttribute("loading", "lazy");
      }
    });

    template.content.querySelectorAll("a[href]").forEach((anchor) => {
      const href = anchor.getAttribute("href") || "";
      const resolved = resolveRepoPath(href);
      if (resolved) {
        if (isTextFile(resolved) || !resolved.includes(".")) {
          anchor.setAttribute("href", routeHref(resolved, resolved.includes(".") ? "file" : ""));
        } else {
          anchor.setAttribute("href", giteaFileUrl(resolved));
          anchor.setAttribute("target", "_blank");
          anchor.setAttribute("rel", "noopener noreferrer");
        }
        return;
      }
      if (/^https?:/i.test(href)) {
        anchor.setAttribute("target", "_blank");
        anchor.setAttribute("rel", "noopener noreferrer");
      }
    });

    return template.innerHTML;
  };

  const renderMarkdown = (markdown, readmePath = "README.md") => {
    if (typeof marked === "undefined" || typeof marked.parse !== "function") {
      return `<pre>${escapeHtml(markdown)}</pre>`;
    }

    marked.setOptions({
      gfm: true,
      breaks: false,
    });

    const readmeDir = readmePath.includes("/") ? readmePath.split("/").slice(0, -1).join("/") : "";
    const html = marked.parse(markdown);
    return rewriteReadmeUrls(sanitizeReadmeHtml(html), readmeDir);
  };

  const renderBreadcrumb = (path) => {
    if (!els.breadcrumb) {
      return;
    }

    const parts = path ? path.split("/").filter(Boolean) : [];
    const crumbs = [`<a href="${routeHref()}">root</a>`];
    let current = "";

    parts.forEach((part, index) => {
      current = current ? `${current}/${part}` : part;
      const isLast = index === parts.length - 1;
      crumbs.push(
        isLast
          ? `<span aria-current="page">${escapeHtml(part)}</span>`
          : `<a href="${routeHref(current)}">${escapeHtml(part)}</a>`
      );
    });

    els.breadcrumb.innerHTML = crumbs.join('<span class="repo-breadcrumb__sep">/</span>');
  };

  const loadIconTheme = () => {
    if (iconTheme) {
      return Promise.resolve(iconTheme);
    }
    if (iconThemePromise) {
      return iconThemePromise;
    }

    iconThemePromise = (async () => {
      try {
        const response = await fetch(`${CONFIG.iconThemeCdn}/dist/material-icons.json`);
        if (!response.ok) {
          throw new Error(`Icon theme ${response.status}`);
        }
        iconTheme = await response.json();
      } catch (error) {
        console.warn("Material Icon Theme unavailable; using text labels.", error);
        iconTheme = null;
      }
      return iconTheme;
    })();

    return iconThemePromise;
  };

  const refreshFileIconsWhenReady = () => {
    void loadIconTheme().then(() => {
      if (iconTheme && paintedTree.entries) {
        renderFiles(paintedTree.entries, paintedTree.path, { quiet: true });
      }
    });
  };

  const resolveMaterialIconKey = (name, isDir) => {
    if (!iconTheme) {
      return isDir ? "folder" : "file";
    }

    const lower = String(name || "").toLowerCase();

    if (isDir) {
      return iconTheme.folderNames?.[lower] || iconTheme.folder || "folder";
    }

    if (iconTheme.fileNames?.[lower]) {
      return iconTheme.fileNames[lower];
    }

    // Prefer the longest matching compound extension (e.g. .d.ts, .cmake.in).
    const parts = lower.split(".");
    if (parts.length > 1) {
      for (let i = 1; i < parts.length; i += 1) {
        const ext = parts.slice(i).join(".");
        if (iconTheme.fileExtensions?.[ext]) {
          return iconTheme.fileExtensions[ext];
        }
      }
    }

    return iconTheme.file || "file";
  };

  const materialIconUrl = (iconKey) => {
    const definition = iconTheme?.iconDefinitions?.[iconKey];
    const iconPath = definition?.iconPath || "";
    const match = iconPath.match(/icons\/(.+)$/);
    if (match) {
      return `${CONFIG.iconThemeCdn}/icons/${match[1]}`;
    }
    return `${CONFIG.iconThemeCdn}/icons/${encodeURIComponent(iconKey)}.svg`;
  };

  const fileIconMarkup = (name, isDir) => {
    if (!iconTheme) {
      return `<span class="repo-file-row__type">${isDir ? "DIR" : "FILE"}</span>`;
    }

    const iconKey = resolveMaterialIconKey(name, isDir);
    const src = materialIconUrl(iconKey);
    const label = isDir ? "Directory" : "File";

    return `
      <span class="repo-file-row__icon-wrap" title="${escapeHtml(label)}">
        <img
          class="repo-file-row__icon"
          src="${escapeHtml(src)}"
          alt=""
          width="18"
          height="18"
          loading="lazy"
          decoding="async"
        />
      </span>
    `;
  };

  const renderFiles = (entries, path, { quiet = false } = {}) => {
    const items = Array.isArray(entries) ? [...entries] : [entries];
    paintedTree = { path: path || "", entries: items };
    items.sort((a, b) => {
      if (a.type !== b.type) {
        return a.type === "dir" ? -1 : 1;
      }
      return a.name.localeCompare(b.name, undefined, { sensitivity: "base" });
    });

    setBusy(els.files, false);

    if (!items.length) {
      els.files.innerHTML = '<p class="repo-empty">This directory is empty.</p>';
      if (!quiet) {
        revealContent(els.files);
      }
      return;
    }

    const rows = items
      .map((entry) => {
        const isDir = entry.type === "dir";
        const entryPath = entry.path || (path ? `${path}/${entry.name}` : entry.name);
        const href = isDir ? routeHref(entryPath) : routeHref(entryPath, "file");
        const meta = isDir ? "-" : formatBytes(entry.size);

        return `
          <a class="repo-file-row" href="${href}">
            ${fileIconMarkup(entry.name, isDir)}
            <span class="repo-file-row__name">${escapeHtml(entry.name)}</span>
            <span class="repo-file-row__meta">${escapeHtml(meta)}</span>
          </a>
        `;
      })
      .join("");

    els.files.innerHTML = `<div class="repo-file-list">${rows}</div>`;
    if (!quiet) {
      revealContent(els.files);
    }
  };

  let commitCache = [];
  let commitVisibleCount = 0;
  let commitTotalCount = null;
  let commitPage = 0;
  let commitHasMore = false;
  let commitFetchBusy = false;

  const commitItemHtml = (commit) => {
    const sha = commit.sha || "";
    const short = sha.slice(0, 7);
    const message = (commit.commit?.message || "Untitled commit").split("\n")[0];
    const author = commit.commit?.author?.name || commit.author?.login || "unknown";
    const when = commit.commit?.author?.date || commit.created;
    return `
      <li class="repo-commit">
        <a class="repo-commit__sha" href="${giteaCommitUrl(sha)}" target="_blank" rel="noopener noreferrer">${escapeHtml(short)}</a>
        <div class="repo-commit__body">
          <p class="repo-commit__message">${escapeHtml(message)}</p>
          <p class="repo-commit__meta">${escapeHtml(author)} · ${escapeHtml(formatRelative(when))}</p>
        </div>
      </li>
    `;
  };

  const updateCommitsMoreButton = () => {
    const moreBtn = document.getElementById("repo-commits-more");
    if (!moreBtn) return;

    const preview = CONFIG.commitPreview;
    const remaining =
      commitTotalCount != null
        ? Math.max(0, commitTotalCount - commitVisibleCount)
        : Math.max(0, commitCache.length - commitVisibleCount);
    const canExpand =
      commitVisibleCount < commitCache.length ||
      commitHasMore ||
      (commitTotalCount != null && commitVisibleCount < commitTotalCount);
    const canCollapse = commitVisibleCount > preview;

    if (!canExpand && !canCollapse) {
      moreBtn.hidden = true;
      return;
    }

    moreBtn.hidden = false;
    if (canExpand) {
      const next = Math.min(CONFIG.commitPageSize, remaining || CONFIG.commitPageSize);
      moreBtn.dataset.action = "more";
      if (remaining > 0) {
        moreBtn.textContent =
          remaining > next ? `See more (${remaining} more)` : `See more (${remaining})`;
      } else {
        moreBtn.textContent = "See more";
      }
    } else {
      moreBtn.dataset.action = "less";
      moreBtn.textContent = "See less";
    }
  };

  const paintCommits = () => {
    const visible = commitCache.slice(0, commitVisibleCount);
    els.commits.innerHTML = visible.map(commitItemHtml).join("");
    updateCommitsMoreButton();
  };

  const fetchCommitsPage = async (page) => {
    const response = await apiFetchResponse(
      `/commits?limit=${CONFIG.commitPageSize}&page=${page}`
    );
    const commits = await response.json();
    const hasMoreHeader = response.headers.get("X-HasMore");
    return {
      commits: Array.isArray(commits) ? commits : [],
      total: parseTotalCount(response),
      hasMore:
        hasMoreHeader != null
          ? hasMoreHeader === "true"
          : (Array.isArray(commits) ? commits.length : 0) >= CONFIG.commitPageSize,
      page: Number(response.headers.get("X-Page")) || page,
    };
  };

  const renderCommits = (commits, meta = {}) => {
    setBusy(els.commits, false);
    commitPage = meta.page || 1;
    commitHasMore = Boolean(meta.hasMore);
    commitTotalCount = meta.total ?? null;

    if (!Array.isArray(commits) || !commits.length) {
      commitCache = [];
      commitVisibleCount = 0;
      commitHasMore = false;
      commitTotalCount = null;
      commitPage = 0;
      els.commits.innerHTML = '<li class="repo-empty">No commits found.</li>';
      const moreBtn = document.getElementById("repo-commits-more");
      if (moreBtn) moreBtn.hidden = true;
      revealContent(els.commits);
      return;
    }

    commitCache = commits;
    commitVisibleCount = Math.min(CONFIG.commitPreview, commits.length);
    paintCommits();
    revealContent(els.commits);
  };

  const commitsMoreBtn = document.getElementById("repo-commits-more");
  if (commitsMoreBtn) {
    commitsMoreBtn.addEventListener("click", async () => {
      if (commitFetchBusy) {
        return;
      }

      if (commitsMoreBtn.dataset.action === "less") {
        commitVisibleCount = Math.min(CONFIG.commitPreview, commitCache.length);
        paintCommits();
        return;
      }

      if (commitVisibleCount < commitCache.length) {
        commitVisibleCount = Math.min(
          commitVisibleCount + CONFIG.commitPageSize,
          commitCache.length
        );
        paintCommits();
        return;
      }

      if (!commitHasMore) {
        commitVisibleCount = commitCache.length;
        paintCommits();
        return;
      }

      commitFetchBusy = true;
      const prevLabel = commitsMoreBtn.textContent;
      commitsMoreBtn.disabled = true;
      commitsMoreBtn.textContent = "Loading…";
      try {
        const page = await fetchCommitsPage(commitPage + 1);
        commitCache = commitCache.concat(page.commits);
        commitPage = page.page;
        commitHasMore = page.hasMore;
        if (page.total != null) {
          commitTotalCount = page.total;
        }
        commitVisibleCount = Math.min(
          commitVisibleCount + CONFIG.commitPageSize,
          commitCache.length
        );
        paintCommits();
      } catch (error) {
        console.error(error);
        commitsMoreBtn.textContent = prevLabel;
      } finally {
        commitFetchBusy = false;
        commitsMoreBtn.disabled = false;
        updateCommitsMoreButton();
      }
    });
  }

  const renderLanguages = (languages) => {
    setBusy(els.langs, false);
    const entries = Object.entries(languages || {});
    if (!entries.length) {
      els.langs.innerHTML = '<p class="repo-empty">No language data.</p>';
      revealContent(els.langs);
      return;
    }

    const total = entries.reduce((sum, [, bytes]) => sum + Number(bytes || 0), 0) || 1;
    entries.sort((a, b) => Number(b[1]) - Number(a[1]));

    els.langs.innerHTML = `
      <div class="repo-lang-bar" aria-hidden="true">
        ${entries
          .map(([name, bytes], index) => {
            const pct = (Number(bytes) / total) * 100;
            return `<span class="repo-lang-bar__seg repo-lang-bar__seg--${index % 6}" style="width:${pct}%" title="${escapeHtml(name)}"></span>`;
          })
          .join("")}
      </div>
      <ul class="repo-lang-list">
        ${entries
          .map(([name, bytes]) => {
            const pct = ((Number(bytes) / total) * 100).toFixed(1);
            return `<li><span>${escapeHtml(name)}</span><strong>${pct}%</strong></li>`;
          })
          .join("")}
      </ul>
    `;
    revealContent(els.langs);
  };

  const renderDocTabs = (activeName = "") => {
    if (!els.docTabs) {
      return;
    }

    hideDocTabSkeleton();

    if (!rootDocEntries.length) {
      els.docTabs.hidden = true;
      els.docTabs.innerHTML = "";
      return;
    }

    els.docTabs.hidden = false;
    els.docTabs.innerHTML = rootDocEntries
      .map((entry) => {
        const selected = entry.name === activeName;
        const label = entry.name.replace(/\.(md|txt)$/i, "").toUpperCase();
        return `
          <button
            class="repo-doc-tab${selected ? " is-selected" : ""}"
            type="button"
            role="tab"
            id="repo-doc-tab-${escapeHtml(entry.name)}"
            data-doc="${escapeHtml(entry.name)}"
            aria-selected="${selected ? "true" : "false"}"
            tabindex="${selected ? "0" : "-1"}"
            title="${escapeHtml(entry.name)}"
          >
            <span class="repo-doc-tab__bar" aria-hidden="true"></span>
            <span class="repo-doc-tab__label">${escapeHtml(label)}</span>
          </button>
        `;
      })
      .join("");
  };

  const wireDocTabs = () => {
    if (!els.docTabs || tabsWired) {
      return;
    }

    tabsWired = true;
    els.docTabs.addEventListener("click", (event) => {
      const tab = event.target instanceof HTMLElement ? event.target.closest("[data-doc]") : null;
      if (!tab) {
        return;
      }
      const name = tab.getAttribute("data-doc");
      if (!name || name === activeDoc) {
        return;
      }
      showRootDoc(name, { pushUrl: true });
    });
  };

  const collectRootDocs = (entries) => {
    const list = Array.isArray(entries) ? entries : entries ? [entries] : [];
    rootDocEntries = list
      .filter((entry) => entry.type === "file" && isRootDocName(entry.name))
      .sort(sortRootDocs);
    wireDocTabs();
    renderDocTabs(activeDoc);
  };

  const showRootDoc = async (name, { pushUrl = false, animate = true } = {}) => {
    if (!els.readmeSection || !els.fileSection) {
      return;
    }

    const docName = name || defaultRootDoc();
    const previousDoc = activeDoc;
    const token = ++docSwapToken;
    const shouldAnimate = Boolean(animate && previousDoc && previousDoc !== docName);

    activeDoc = docName;
    renderDocTabs(docName);
    setDocDirection(previousDoc, docName);

    els.fileSection.hidden = true;
    els.readmeSection.hidden = false;

    if (els.readmeTitle) {
      if (shouldAnimate && !prefersReducedMotion()) {
        els.readmeTitle.classList.remove("is-title-swap");
        void els.readmeTitle.offsetWidth;
        els.readmeTitle.classList.add("is-title-swap");
      }
      els.readmeTitle.textContent = docName;
    }

    if (pushUrl) {
      history.pushState({ doc: docName }, "", docHref(docName));
    }

    const contentPromise = (async () => {
      try {
        const text = await fetchText(docName);
        if (/\.md$/i.test(docName)) {
          return renderMarkdown(text, docName);
        }
        return `<pre class="repo-readme__plain">${escapeHtml(text)}</pre>`;
      } catch {
        return `<p class="repo-empty">${escapeHtml(docName)} could not be loaded.</p>`;
      }
    })();

    if (shouldAnimate) {
      await waitForClassAnimation(els.readme, "is-leaving", DOC_SWAP_MS);
      clearOutline();
    } else {
      clearOutline();
      showReadmeLoading();
    }

    if (token !== docSwapToken) {
      return;
    }

    const html = await contentPromise;
    if (token !== docSwapToken) {
      return;
    }

    els.readme.innerHTML = html;
    setBusy(els.readme, false);

    if (/\.md$/i.test(docName)) {
      buildOutline();
    } else {
      clearOutline();
    }

    if (shouldAnimate || (animate && !previousDoc)) {
      await waitForClassAnimation(els.readme, "is-entering", DOC_SWAP_MS + 40);
    } else {
      revealContent(els.readme);
    }

    if (els.readmeTitle) {
      els.readmeTitle.classList.remove("is-title-swap");
    }

    scheduleDockUpdate();
  };

  const showReadme = async () => showRootDoc(defaultRootDoc());

  const showFile = async (path) => {
    if (!els.fileSection || !els.readmeSection) {
      return;
    }

    const baseName = path.split("/").pop() || path;
    const isRootDoc = !path.includes("/") && isRootDocName(baseName);

    if (isRootDoc) {
      await showRootDoc(baseName);
      return;
    }

    activeDoc = "";
    renderDocTabs("");
    clearOutline();

    els.readmeSection.hidden = true;
    els.fileSection.hidden = false;
    els.fileTitle.textContent = baseName;
    els.fileRaw.href = rawUrl(path);
    els.fileContent.textContent = loadingMarkup.file();

    if (!isTextFile(path)) {
      els.fileContent.textContent = "Binary or unsupported preview. Open the raw file on Gitea.";
      return;
    }

    try {
      const text = await fetchText(path);
      if (extensionOf(path) === "md") {
        els.readmeSection.hidden = false;
        els.fileSection.hidden = true;
        if (els.readmeTitle) {
          els.readmeTitle.textContent = baseName;
        }
        els.readme.innerHTML = renderMarkdown(text, path);
        setBusy(els.readme, false);
        buildOutline();
        revealContent(els.readme);
        return;
      }
      clearOutline();
      els.fileContent.textContent = text;
      revealContent(els.fileContent);
    } catch {
      els.fileContent.textContent = "Could not load this file.";
    }
  };

  const loadTree = async (path, prefetched) => {
    renderBreadcrumb(path);
    if (prefetched) {
      if (!path) {
        collectRootDocs(prefetched);
      }
      renderFiles(prefetched, path);
      return prefetched;
    }

    showFilesLoading();
    const suffix = path
      ? `/contents/${path.split("/").map(encodeURIComponent).join("/")}?ref=${encodeURIComponent(defaultBranch)}`
      : `/contents?ref=${encodeURIComponent(defaultBranch)}`;
    const entries = await apiFetch(suffix);
    if (!path) {
      collectRootDocs(entries);
    }
    renderFiles(entries, path);
    return entries;
  };

  const wireCloneCopy = (cloneUrl) => {
    if (!els.copyClone || !els.cloneWrap || !els.cloneInput) {
      return;
    }

    els.cloneInput.value = cloneUrl;
    els.cloneWrap.hidden = false;
    els.copyClone.hidden = false;

    els.copyClone.addEventListener("click", async () => {
      try {
        await navigator.clipboard.writeText(cloneUrl);
        els.copyClone.textContent = "Copied";
      } catch {
        els.cloneInput.select();
        els.copyClone.textContent = "Select & copy";
      }

      window.clearTimeout(copyResetTimer);
      copyResetTimer = window.setTimeout(() => {
        els.copyClone.textContent = "Copy Clone URL";
      }, 1800);
    });
  };

  const bootstrap = async () => {
    const token = ++bootstrapToken;
    const route = getRoute();
    pageUnloading = false;
    setStatus("Connecting to Gitea", false, { busy: true });
    refreshFileIconsWhenReady();

    try {
      const repoPromise = apiFetch("");
      const commitsPromise = fetchCommitsPage(1);
      const languagesPromise = apiFetch("/languages");

      const repo = await repoPromise;
      if (token !== bootstrapToken || pageUnloading) {
        return;
      }

      defaultBranch = repo.default_branch || "main";

      const [commitPageData, languages, rootEntries] = await Promise.all([
        commitsPromise,
        languagesPromise,
        apiFetch(`/contents?ref=${encodeURIComponent(defaultBranch)}`),
      ]);

      if (token !== bootstrapToken || pageUnloading) {
        return;
      }

      collectRootDocs(rootEntries);

      if (els.fullName) {
        els.fullName.textContent = `${CONFIG.owner} / ${CONFIG.repo}`;
      }
      if (els.title) {
        els.title.textContent = repo.name || CONFIG.repo;
      }
      if (els.description) {
        els.description.classList.remove("repo-hero__desc--loading");
        els.description.textContent =
          repo.description?.trim() ||
          "Dedicated-server Fallout 4 multiplayer for Anniversary Edition and the original release. Live source mirror from the project Gitea service.";
        revealContent(els.description);
      }
      if (els.openGitea) {
        els.openGitea.href = repo.html_url || htmlRoot;
      }

      const statsRoot = document.getElementById("repo-stats");
      if (statsRoot) {
        statsRoot.classList.remove("is-loading");
        statsRoot.removeAttribute("aria-busy");
      }

      els.stats.branch.textContent = defaultBranch;
      els.stats.language.textContent = repo.language || "-";
      els.stats.commits.textContent = String(
        commitPageData.total ??
          (commitPageData.commits?.length ? commitPageData.commits.length : "-")
      );
      els.stats.branches.textContent = String(repo.branch_count ?? "-");
      els.stats.watchers.textContent = String(repo.watchers_count ?? "-");
      els.stats.updated.textContent = formatDate(repo.updated_at);
      if (statsRoot) {
        revealContent(statsRoot);
      }

      wireCloneCopy(repo.clone_url || `${htmlRoot}.git`);
      renderCommits(commitPageData.commits, {
        total: commitPageData.total,
        hasMore: commitPageData.hasMore,
        page: commitPageData.page,
      });
      renderLanguages(languages);

      const requestedDoc =
        route.doc ||
        (!route.path.includes("/") && isRootDocName(route.path) ? route.path : "");

      if (route.view === "file" && route.path && !requestedDoc) {
        const parent = route.path.includes("/") ? route.path.split("/").slice(0, -1).join("/") : "";
        await loadTree(parent, parent === "" ? rootEntries : undefined);
        renderBreadcrumb(route.path);
        await showFile(route.path);
      } else if (requestedDoc) {
        await loadTree("", rootEntries);
        await showRootDoc(requestedDoc);
      } else if (!route.path) {
        await loadTree("", rootEntries);
        await showRootDoc(defaultRootDoc());
      } else {
        await loadTree(route.path);
        await showRootDoc(defaultRootDoc());
      }

      if (token !== bootstrapToken || pageUnloading) {
        return;
      }

      setStatus(`Live from ${CONFIG.base.replace(/^https?:\/\//, "")} · branch ${defaultBranch}`);
    } catch (error) {
      // Navigating away aborts in-flight fetches; don't paint a false "offline" state
      // that the browser may restore from bfcache when the user returns.
      if (
        token !== bootstrapToken ||
        pageUnloading ||
        document.visibilityState === "hidden" ||
        isAbortError(error)
      ) {
        return;
      }
      console.error(error);
      setStatus("Could not reach the Gitea API. The repository host may be offline.", true);
      hideDocTabSkeleton();
      if (els.description) {
        els.description.classList.remove("repo-hero__desc--loading");
        els.description.textContent = "Repository metadata unavailable while Gitea is unreachable.";
      }
      const statsRoot = document.getElementById("repo-stats");
      if (statsRoot) {
        statsRoot.classList.remove("is-loading");
        statsRoot.removeAttribute("aria-busy");
      }
      Object.values(els.stats).forEach((stat) => {
        if (stat) {
          stat.textContent = "-";
        }
      });
      setBusy(els.files, false);
      setBusy(els.commits, false);
      setBusy(els.readme, false);
      setBusy(els.langs, false);
      els.files.innerHTML =
        '<p class="repo-empty">File browser unavailable. <a href="' +
        escapeHtml(htmlRoot) +
        '" target="_blank" rel="noopener noreferrer">Open the repo on Gitea</a>.</p>';
      commitCache = [];
      commitVisibleCount = 0;
      commitTotalCount = null;
      commitPage = 0;
      commitHasMore = false;
      els.commits.innerHTML = '<li class="repo-empty">Commit history unavailable.</li>';
      const moreBtn = document.getElementById("repo-commits-more");
      if (moreBtn) moreBtn.hidden = true;
      els.readme.innerHTML = '<p class="repo-empty">README unavailable while Gitea is unreachable.</p>';
      els.langs.innerHTML = '<p class="repo-empty">Language data unavailable.</p>';
    }
  };

  window.addEventListener("popstate", () => {
    const route = getRoute();
    if (route.doc && isRootDocName(route.doc)) {
      showRootDoc(route.doc);
      return;
    }
    if (route.view === "file" && route.path) {
      showFile(route.path);
      return;
    }
    showRootDoc(defaultRootDoc());
  });

  // Simulates position:sticky for the floating gutter dock:
  // - pinned at DOCK_TOP while reading inside the MD section
  // - travels with the section top when scrolling back above it
  // - clamps to the section bottom so it leaves with the panel
  // - fades out once the dock itself has left the viewport
  const DOCK_QUERY = window.matchMedia("(min-width: 1820px)");
  const DOCK_TOP_PX = 102; // matches the CSS `top: 6.4rem` offset at 16px root font-size
  let dockTicking = false;

  const clearDockInline = () => {
    if (!els.docsSidebar) {
      return;
    }
    els.docsSidebar.classList.remove("is-docked", "is-pinned");
    els.docsSidebar.style.removeProperty("top");
    els.docsSidebar.style.removeProperty("max-height");
  };

  const updateSidebarDock = () => {
    if (!els.docsSidebar || !els.readmeSection) {
      return;
    }

    if (!DOCK_QUERY.matches || els.readmeSection.hidden) {
      clearDockInline();
      return;
    }

    const rect = els.readmeSection.getBoundingClientRect();
    const viewportH = window.innerHeight || document.documentElement.clientHeight;

    // Measure natural height without the pinned max-height clamp, otherwise
    // bottom-edge math and follow-mode sizing fight the scrollbar.
    const prevMaxHeight = els.docsSidebar.style.maxHeight;
    els.docsSidebar.style.maxHeight = "none";
    const naturalHeight = els.docsSidebar.scrollHeight || els.docsSidebar.offsetHeight || 280;
    els.docsSidebar.style.maxHeight = prevMaxHeight;

    let top;
    let pinned = false;

    if (rect.top > DOCK_TOP_PX) {
      // Still above the pin line — dock rides the section top.
      top = rect.top;
    } else if (rect.bottom - naturalHeight < DOCK_TOP_PX) {
      // Reached the section bottom — dock leaves with the panel.
      top = rect.bottom - naturalHeight;
    } else {
      // Inside the sticky range — pinned under the site header.
      top = DOCK_TOP_PX;
      pinned = true;
    }

    const dockBottom = top + naturalHeight;
    const onScreen = dockBottom > 0 && top < viewportH;
    const sectionVisible = rect.bottom > 0 && rect.top < viewportH;

    els.docsSidebar.style.top = `${Math.round(top)}px`;

    if (pinned) {
      els.docsSidebar.style.maxHeight = `${Math.round(Math.max(120, viewportH - DOCK_TOP_PX - 24))}px`;
    } else {
      // Follow / exit modes must not clip into a scrollbar.
      els.docsSidebar.style.maxHeight = "none";
    }

    els.docsSidebar.classList.toggle("is-docked", onScreen && sectionVisible);
    els.docsSidebar.classList.toggle("is-pinned", pinned && onScreen && sectionVisible);
  };

  const scheduleDockUpdate = () => {
    if (dockTicking) {
      return;
    }
    dockTicking = true;
    window.requestAnimationFrame(() => {
      dockTicking = false;
      updateSidebarDock();
    });
  };

  if (els.docsSidebar) {
    window.addEventListener("scroll", scheduleDockUpdate, { passive: true });
    window.addEventListener("resize", scheduleDockUpdate);
    if (typeof DOCK_QUERY.addEventListener === "function") {
      DOCK_QUERY.addEventListener("change", scheduleDockUpdate);
    }
    if ("ResizeObserver" in window) {
      new ResizeObserver(scheduleDockUpdate).observe(els.readmeSection);
    }
    scheduleDockUpdate();
  }

  const FOLD_MQ = window.matchMedia("(max-width: 980px)");
  let foldWasMobile = FOLD_MQ.matches;

  const setFoldOpen = (panel, open) => {
    if (!panel) {
      return;
    }
    panel.classList.toggle("is-collapsed", !open);
    const body = panel.querySelector(".repo-fold__panel");
    if (body) {
      body.toggleAttribute("inert", !open);
      if (open) {
        body.removeAttribute("aria-hidden");
      } else {
        body.setAttribute("aria-hidden", "true");
      }
    }
    const toggle = panel.querySelector("[data-repo-fold-toggle]");
    if (!toggle) {
      return;
    }
    toggle.setAttribute("aria-expanded", open ? "true" : "false");
    const label = toggle.querySelector(".visually-hidden");
    if (label) {
      const title =
        panel.querySelector(".repo-fold__titles h2")?.textContent?.trim() || "section";
      label.textContent = open ? `Hide ${title}` : `Show ${title}`;
    }
  };

  const syncFoldsToViewport = () => {
    const isMobile = FOLD_MQ.matches;
    document.querySelectorAll("[data-repo-fold]").forEach((panel) => {
      if (!isMobile) {
        setFoldOpen(panel, true);
        return;
      }
      if (!foldWasMobile) {
        // Entered the mobile breakpoint — collapse so docs stay nearby.
        setFoldOpen(panel, false);
        return;
      }
      setFoldOpen(panel, !panel.classList.contains("is-collapsed"));
    });
    foldWasMobile = isMobile;
  };

  document.querySelectorAll("[data-repo-fold]").forEach((panel) => {
    const toggle = panel.querySelector("[data-repo-fold-toggle]");
    const head = panel.querySelector(".repo-fold__head");
    if (!toggle || !head) {
      return;
    }

    const flip = () => {
      if (!FOLD_MQ.matches) {
        return;
      }
      setFoldOpen(panel, panel.classList.contains("is-collapsed"));
    };

    toggle.addEventListener("click", (event) => {
      event.stopPropagation();
      flip();
    });

    head.addEventListener("click", (event) => {
      if (event.target.closest("a, button")) {
        return;
      }
      flip();
    });
  });

  if (typeof FOLD_MQ.addEventListener === "function") {
    FOLD_MQ.addEventListener("change", syncFoldsToViewport);
  } else if (typeof FOLD_MQ.addListener === "function") {
    FOLD_MQ.addListener(syncFoldsToViewport);
  }
  syncFoldsToViewport();

  window.addEventListener("pagehide", () => {
    pageUnloading = true;
    bootstrapToken += 1;
  });

  // Back/forward cache can restore a page that was frozen mid-fetch (or after an
  // aborted request painted an error). Re-run bootstrap so returning to the repo works.
  window.addEventListener("pageshow", (event) => {
    if (!event.persisted) {
      return;
    }
    pageUnloading = false;
    setStatus("Connecting to Gitea", false, { busy: true });
    bootstrap().then(scheduleDockUpdate);
  });

  const start = async () => {
    // This site no longer ships a service worker. Remove registrations left by
    // older deployments because they can intercept and reject Gitea API calls.
    if ("serviceWorker" in navigator) {
      try {
        const registrations = await navigator.serviceWorker.getRegistrations();
        if (registrations.length) {
          await Promise.allSettled(registrations.map((registration) => registration.unregister()));

          if (navigator.serviceWorker.controller) {
            const reloadKey = "co-legacy-service-worker-removed";
            if (sessionStorage.getItem(reloadKey) !== "true") {
              sessionStorage.setItem(reloadKey, "true");
              window.location.reload();
              return;
            }
          }
        }
      } catch (error) {
        console.warn("Could not remove a legacy service worker.", error);
      }
    }

    bootstrap().then(scheduleDockUpdate);
  };

  start();
})();
