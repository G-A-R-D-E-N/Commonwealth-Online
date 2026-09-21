(() => {
  const root = document.querySelector("[data-forum]");
  if (!root) return;

  const url = (root.dataset.supabaseUrl || "").replace(/\/+$/, "");
  const key = root.dataset.supabaseKey || "";
  const categoriesEl = root.querySelector("[data-forum-categories]");
  const threadsEl = root.querySelector("[data-forum-threads]");
  const statusEl = root.querySelector("[data-forum-status]");
  const headingEl = root.querySelector("[data-forum-heading]");
  const compose = root.querySelector("[data-forum-compose]");
  const composeCategory = root.querySelector("[data-forum-compose-category]");
  const composeStatus = root.querySelector("[data-forum-compose-status]");
  const search = root.querySelector("[data-forum-search]");
  const pagination = root.querySelector("[data-forum-pagination]");
  const previous = root.querySelector("[data-forum-prev]");
  const next = root.querySelector("[data-forum-next]");
  const pageLabel = root.querySelector("[data-forum-page]");
  const signIn = root.querySelector("[data-forum-signin]");
  const selectedSlug = new URLSearchParams(window.location.search).get("category");
  const brandLogo = document.querySelector(".site-brand__logo");
  const assetBase = brandLogo ? new URL(brandLogo.src).pathname.split("/assets/")[0] : "";

  if (!url || !key || !categoriesEl || !threadsEl || !statusEl) {
    if (statusEl) statusEl.textContent = "Forum is temporarily unavailable.";
    return;
  }

  const headers = { apikey: key, Accept: "application/json" };
  const client = window.supabase?.createClient
    ? window.coSupabase || window.supabase.createClient(url, key)
    : null;
  if (client) window.coSupabase = client;
  const PAGE_SIZE = 25;
  let currentPage = 1;
  let categoriesState = [];
  let searchTimer = null;

  const request = async (table, select, order) => {
    const endpoint = new URL(url + "/rest/v1/" + table);
    endpoint.searchParams.set("select", select);
    if (order) endpoint.searchParams.set("order", order);
    const response = await fetch(endpoint, { headers });
    if (!response.ok) throw new Error("Forum request failed");
    return response.json();
  };

  const requestThreads = async () => {
    const endpoint = new URL(url + "/rest/v1/forum_threads");
    endpoint.searchParams.set(
      "select",
      "id,title,is_pinned,is_locked,created_at,updated_at,category:forum_categories!inner(slug,name),author:profiles!forum_threads_author_id_fkey(display_name,avatar_url)"
    );
    endpoint.searchParams.set("order", "is_pinned.desc,updated_at.desc");
    endpoint.searchParams.set("limit", String(PAGE_SIZE + 1));
    endpoint.searchParams.set("offset", String((currentPage - 1) * PAGE_SIZE));

    if (selectedSlug) endpoint.searchParams.set("category.slug", "eq." + selectedSlug);

    const query = String(search?.value || "").trim();
    if (query) endpoint.searchParams.set("title", "ilike.*" + query.replaceAll("*", "") + "*");

    const response = await fetch(endpoint, { headers });
    if (!response.ok) throw new Error("Forum request failed");

    const rows = await response.json();
    return {
      rows: Array.isArray(rows) ? rows.slice(0, PAGE_SIZE) : [],
      hasNext: Array.isArray(rows) && rows.length > PAGE_SIZE,
    };
  };

  const formatDate = (value) => {
    const date = new Date(value);
    return Number.isNaN(date.getTime())
      ? ""
      : new Intl.DateTimeFormat(undefined, { month: "short", day: "numeric", year: "numeric" }).format(date);
  };

  const renderCategories = (categories) => {
    categoriesEl.replaceChildren();

    const all = document.createElement("a");
    all.className = "forum-category-link" + (selectedSlug ? "" : " is-active");
    all.href = assetBase + "/forum/";
    all.textContent = "All discussions";
    categoriesEl.append(all);

    for (const category of categories) {
      const link = document.createElement("a");
      link.className = "forum-category-link" + (selectedSlug === category.slug ? " is-active" : "");
      link.href = assetBase + "/forum/?category=" + encodeURIComponent(category.slug);

      const name = document.createElement("strong");
      const description = document.createElement("span");
      name.textContent = category.name;
      description.textContent = category.description || (category.is_locked ? "Read-only board" : "Community discussion");

      link.append(name, description);
      categoriesEl.append(link);

    }
  };

  const renderThreads = (threads, hasNext) => {
    const selected = categoriesState.find((category) => category.slug === selectedSlug);

    if (headingEl) headingEl.textContent = selected ? selected.name : "Latest discussions";
    threadsEl.replaceChildren();

    if (!threads.length) {
      const empty = document.createElement("div");
      empty.className = "forum-empty";
      empty.textContent =
        String(search?.value || "").trim()
          ? "No discussions matched your search."
          : selected
            ? "No discussions in this category yet."
            : "No discussions have been started yet.";
      threadsEl.append(empty);
    } else {
      for (const thread of threads) {
        const link = document.createElement("a");
        link.className = "forum-thread-row";
        link.href = assetBase + "/forum/thread/?id=" + encodeURIComponent(thread.id);

        const marker = document.createElement("span");
        marker.className = "forum-thread-row__marker";
        marker.textContent = thread.is_pinned ? "PIN" : thread.is_locked ? "LOCK" : "POST";

        const copy = document.createElement("span");
        copy.className = "forum-thread-row__copy";
        const title = document.createElement("strong");
        const meta = document.createElement("span");
        title.textContent = thread.title;
        meta.textContent = [
          thread.category?.name,
          thread.author?.display_name,
          formatDate(thread.updated_at || thread.created_at),
        ].filter(Boolean).join(" · ");

        copy.append(title, meta);
        link.append(marker, copy);
        threadsEl.append(link);
      }
    }

    threadsEl.hidden = false;
    statusEl.hidden = true;

    if (pagination && previous && next && pageLabel) {
      pagination.hidden = currentPage === 1 && !hasNext;
      previous.disabled = currentPage <= 1;
      next.disabled = !hasNext;
      pageLabel.textContent = "Page " + currentPage;
    }
  };

  const loadThreads = async () => {
    statusEl.hidden = false;
    statusEl.textContent = "Loading discussions…";

    try {
      const result = await requestThreads();
      renderThreads(result.rows, result.hasNext);
    } catch {
      threadsEl.replaceChildren();
      statusEl.hidden = false;
      statusEl.textContent = "Forum discussions are temporarily unavailable.";
    }
  };
  const setupCompose = async (categories) => {
    if (!client || !compose || !composeCategory) return;

    const { data } = await client.auth.getSession();
    if (!data.session?.user) return;

    const moderatorResult = await client.rpc("is_forum_moderator");
    const canPostLocked = !moderatorResult.error && Boolean(moderatorResult.data);
    const available = categories.filter((category) => canPostLocked || !category.is_locked);

    composeCategory.replaceChildren();
    for (const category of available) {
      const option = document.createElement("option");
      option.value = String(category.id);
      option.textContent = category.name + (category.is_locked ? " · Staff" : "");
      option.selected = selectedSlug === category.slug;
      composeCategory.append(option);
    }

    if (signIn) signIn.hidden = true;
    compose.hidden = available.length === 0;
  };

  search?.addEventListener("input", () => {
    currentPage = 1;
    window.clearTimeout(searchTimer);
    searchTimer = window.setTimeout(loadThreads, 250);
  });

  previous?.addEventListener("click", () => {
    if (currentPage <= 1) return;
    currentPage -= 1;
    loadThreads();
  });

  next?.addEventListener("click", () => {
    currentPage += 1;
    loadThreads();
  });

  compose?.addEventListener("submit", async (event) => {
    event.preventDefault();
    if (!client || !compose.checkValidity()) return;

    const submit = compose.querySelector('button[type="submit"]');
    submit.disabled = true;
    if (composeStatus) {
      composeStatus.hidden = false;
      composeStatus.textContent = "Creating discussion…";
    }

    const { data, error } = await client.rpc("create_forum_thread", {
      p_category_id: Number(compose.elements.category_id.value),
      p_title: compose.elements.title.value.trim(),
      p_body: compose.elements.body.value.trim(),
    });

    if (error || !data) {
      submit.disabled = false;
      if (composeStatus) composeStatus.textContent = error?.message || "Could not create discussion.";
      return;
    }

    window.location.assign(assetBase + "/forum/thread/?id=" + encodeURIComponent(data));
  });

  request("forum_categories", "id,slug,name,description,position,is_locked", "position.asc,name.asc")
    .then(async (categories) => {
      const rows = Array.isArray(categories) ? categories : [];
      categoriesState = rows;
      renderCategories(rows);
      setupCompose(rows);
      await loadThreads();
    })
    .catch(() => {
      statusEl.textContent = "Forum data is temporarily unavailable.";
    });
})();
