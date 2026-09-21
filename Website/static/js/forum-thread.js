(() => {
  const root = document.querySelector("[data-forum-thread]");
  if (!root) return;

  const url = (root.dataset.supabaseUrl || "").replace(/\/+$/, "");
  const key = root.dataset.supabaseKey || "";
  const statusEl = root.querySelector("[data-thread-status]");
  const postsEl = root.querySelector("[data-thread-posts]");
  const titleEl = root.querySelector("[data-thread-title]");
  const categoryEl = root.querySelector("[data-thread-category]");
  const metaEl = root.querySelector("[data-thread-meta]");
  const threadActions = root.querySelector("[data-thread-actions]");
  const reply = root.querySelector("[data-thread-reply]");
  const replyStatus = root.querySelector("[data-thread-reply-status]");
  const pagination = root.querySelector("[data-thread-pagination]");
  const previous = root.querySelector("[data-thread-prev]");
  const next = root.querySelector("[data-thread-next]");
  const pageLabel = root.querySelector("[data-thread-page]");
  const params = new URLSearchParams(window.location.search);
  const threadId = params.get("id");
  const PAGE_SIZE = 50;
  let currentPage = Math.max(1, Number.parseInt(params.get("page") || "1", 10) || 1);
  let postTotal = 0;
  const brandLogo = document.querySelector(".site-brand__logo");
  const assetBase = brandLogo ? new URL(brandLogo.src).pathname.split("/assets/")[0] : "";

  if (!url || !key || !threadId || !statusEl || !postsEl) {
    if (statusEl) statusEl.textContent = "Discussion not found.";
    return;
  }

  const headers = { apikey: key, Accept: "application/json" };
  const client = window.supabase?.createClient
    ? window.coSupabase || window.supabase.createClient(url, key)
    : null;
  if (client) window.coSupabase = client;
  let currentUser = null;
  let isModerator = false;
  let reactionRows = [];
  let reportRows = [];

  const request = async (table, select, filters = []) => {
    const endpoint = new URL(url + "/rest/v1/" + table);
    endpoint.searchParams.set("select", select);
    for (const [name, value] of filters) endpoint.searchParams.set(name, value);
    const response = await fetch(endpoint, { headers });
    if (!response.ok) throw new Error("Forum request failed");
    return response.json();
  };

  const requestPosts = async () => {
    const endpoint = new URL(url + "/rest/v1/forum_posts");
    endpoint.searchParams.set(
      "select",
      "id,body,edited_at,created_at,author:profiles!forum_posts_author_id_fkey(id,display_name,avatar_url)"
    );
    endpoint.searchParams.set("thread_id", "eq." + threadId);
    endpoint.searchParams.set("order", "created_at.asc");
    endpoint.searchParams.set("limit", String(PAGE_SIZE));
    endpoint.searchParams.set("offset", String((currentPage - 1) * PAGE_SIZE));

    const response = await fetch(endpoint, {
      headers: { ...headers, Prefer: "count=exact" },
    });
    if (!response.ok) throw new Error("Forum posts request failed");

    const range = response.headers?.get?.("content-range") || "";
    const total = Number.parseInt(range.split("/")[1] || "0", 10);
    return {
      rows: await response.json(),
      total: Number.isFinite(total) ? total : 0,
    };
  };

  const formatDate = (value) => {
    const date = new Date(value);
    return Number.isNaN(date.getTime())
      ? ""
      : new Intl.DateTimeFormat(undefined, { month: "short", day: "numeric", year: "numeric" }).format(date);
  };

  const reactionOptions = ["👍", "❤️", "😂", "🎉", "👀"];

  const reactionCount = (postId, emoji) =>
    reactionRows.filter((row) => row.post_id === postId && row.emoji === emoji).length;

  const hasReaction = (postId, emoji) =>
    Boolean(currentUser) &&
    reactionRows.some(
      (row) => row.post_id === postId && row.emoji === emoji && row.user_id === currentUser.id
    );

  const toggleReaction = async (postId, emoji) => {
    if (!client || !currentUser) return;

    const existing = hasReaction(postId, emoji);
    const result = existing
      ? await client
          .from("forum_reactions")
          .delete()
          .eq("post_id", postId)
          .eq("user_id", currentUser.id)
          .eq("emoji", emoji)
      : await client
          .from("forum_reactions")
          .insert({ post_id: postId, emoji });

    if (!result.error) window.location.reload();
  };

  const reportPost = async (postId) => {
    if (!client || !currentUser) return;
    const reason = window.prompt("Why are you reporting this post?");
    const message = String(reason || "").trim();
    if (message.length < 3 || message.length > 1000) return;

    const { error } = await client
      .from("forum_reports")
      .insert({ post_id: postId, reason: message });

    if (!error) {
      window.alert("Report sent.");
    } else if (error.code === "23505") {
      window.alert("You already have an open report for this post.");
    } else {
      window.alert("Could not send report.");
    }
  };

  const editPost = async (post) => {
    if (!client || !currentUser || currentUser.id !== post.author?.id) return;
    const body = window.prompt("Edit post", post.body);
    const value = String(body || "").trim();
    if (!value || value.length > 12000 || value === post.body) return;

    const { error } = await client
      .from("forum_posts")
      .update({ body: value })
      .eq("id", post.id);

    if (!error) window.location.reload();
  };

  const deletePost = async (post) => {
    if (!client || !currentUser || (!isModerator && currentUser.id !== post.author?.id)) return;
    if (!window.confirm("Delete this post?")) return;

    const { error } = await client
      .from("forum_posts")
      .delete()
      .eq("id", post.id);

    if (!error) window.location.reload();
  };

  const updateReports = async (reports, status) => {
    if (!client || !isModerator || !reports.length) return;
    const ids = reports.map((report) => report.id);
    const { error } = await client
      .from("forum_reports")
      .update({ status })
      .in("id", ids);
    if (!error) window.location.reload();
  };

  const renderPost = (post) => {
    const article = document.createElement("article");
    article.className = "forum-post";

    const aside = document.createElement("div");
    aside.className = "forum-post__author";
    const avatar = document.createElement("img");
    const name = document.createElement(post.author?.display_name ? "a" : "strong");
    avatar.src = post.author?.avatar_url || "/assets/profile-icons/armorer.png";
    avatar.alt = "";
    avatar.width = 48;
    avatar.height = 48;
    name.textContent = post.author?.display_name || "Member";
    if (post.author?.display_name) {
      name.href = assetBase + "/member/?username=" + encodeURIComponent(post.author.display_name);
    }
    aside.append(avatar, name);

    const body = document.createElement("div");
    body.className = "forum-post__body";
    const meta = document.createElement("p");
    const content = document.createElement("p");
    meta.className = "forum-post__meta";
    meta.textContent = formatDate(post.created_at) + (post.edited_at ? " · edited" : "");
    content.className = "forum-post__content";
    content.textContent = post.body;
    body.append(meta, content);

    const actions = document.createElement("div");
    actions.className = "forum-post__actions";

    for (const emoji of reactionOptions) {
      const button = document.createElement("button");
      const count = reactionCount(post.id, emoji);
      button.type = "button";
      button.className = "forum-reaction" + (hasReaction(post.id, emoji) ? " is-active" : "");
      button.textContent = count ? emoji + " " + count : emoji;
      button.disabled = !currentUser;
      button.setAttribute("aria-label", "React " + emoji);
      button.addEventListener("click", () => toggleReaction(post.id, emoji));
      actions.append(button);
    }

    const openReports = reportRows.filter(
      (report) => report.post_id === post.id && report.status === "open"
    );

    if (isModerator && openReports.length) {
      const reports = document.createElement("div");
      reports.className = "forum-post__reports";

      const summary = document.createElement("strong");
      summary.textContent = openReports.length + " open report" + (openReports.length === 1 ? "" : "s");

      const reasons = document.createElement("ul");
      for (const report of openReports) {
        const item = document.createElement("li");
        item.textContent = [
          report.reporter?.display_name || "Member",
          report.reason,
        ].join(": ");
        reasons.append(item);
      }

      const review = document.createElement("button");
      review.type = "button";
      review.className = "forum-report";
      review.textContent = "Mark reviewed";
      review.addEventListener("click", () => updateReports(openReports, "reviewed"));

      const dismiss = document.createElement("button");
      dismiss.type = "button";
      dismiss.className = "forum-report";
      dismiss.textContent = "Dismiss reports";
      dismiss.addEventListener("click", () => updateReports(openReports, "dismissed"));

      reports.append(summary, reasons, review, dismiss);
      body.append(reports);
    }

    if (currentUser?.id === post.author?.id) {
      const edit = document.createElement("button");
      edit.type = "button";
      edit.className = "forum-report";
      edit.textContent = "Edit";
      edit.addEventListener("click", () => editPost(post));

      const remove = document.createElement("button");
      remove.type = "button";
      remove.className = "forum-report";
      remove.textContent = "Delete";
      remove.addEventListener("click", () => deletePost(post));

      actions.append(edit, remove);
    } else if (currentUser) {
      const action = document.createElement("button");
      action.type = "button";
      action.className = "forum-report";
      action.textContent = isModerator ? "Delete post" : "Report";
      action.addEventListener(
        "click",
        () => isModerator ? deletePost(post) : reportPost(post.id)
      );
      actions.append(action);
    }

    body.append(actions);
    article.append(aside, body);
    return article;
  };

  const setupThreadActions = (thread) => {
    if (!threadActions || !client || !currentUser) return;
    threadActions.replaceChildren();

    const isAuthor = currentUser.id === thread.author?.id;

    if (!thread.is_locked && isAuthor) {
      const edit = document.createElement("button");
      edit.type = "button";
      edit.className = "co-btn co-btn--ghost";
      edit.textContent = "Edit title";
      edit.addEventListener("click", async () => {
        const title = window.prompt("Edit discussion title", thread.title);
        const value = String(title || "").trim();
        if (value.length < 3 || value.length > 160 || value === thread.title) return;

        const { error } = await client
          .from("forum_threads")
          .update({ title: value })
          .eq("id", thread.id);

        if (!error) window.location.reload();
      });
      threadActions.append(edit);
    }

    if ((!thread.is_locked && isAuthor) || isModerator) {
      const remove = document.createElement("button");
      remove.type = "button";
      remove.className = "co-btn co-btn--ghost";
      remove.textContent = "Delete discussion";
      remove.addEventListener("click", async () => {
        if (!window.confirm("Delete this discussion and all replies?")) return;

        const { error } = await client
          .from("forum_threads")
          .delete()
          .eq("id", thread.id);

        if (!error) window.location.assign(assetBase + "/forum/");
      });
      threadActions.append(remove);
    }

    if (isModerator) {
      const lock = document.createElement("button");
      lock.type = "button";
      lock.className = "co-btn co-btn--ghost";
      lock.textContent = thread.is_locked ? "Unlock" : "Lock";
      lock.addEventListener("click", async () => {
        const { error } = await client.rpc("moderate_forum_thread", {
          p_thread_id: thread.id,
          p_locked: !thread.is_locked,
          p_pinned: thread.is_pinned,
        });
        if (!error) window.location.reload();
      });

      const pin = document.createElement("button");
      pin.type = "button";
      pin.className = "co-btn co-btn--ghost";
      pin.textContent = thread.is_pinned ? "Unpin" : "Pin";
      pin.addEventListener("click", async () => {
        const { error } = await client.rpc("moderate_forum_thread", {
          p_thread_id: thread.id,
          p_locked: thread.is_locked,
          p_pinned: !thread.is_pinned,
        });
        if (!error) window.location.reload();
      });

      threadActions.append(lock, pin);
    }

    threadActions.hidden = threadActions.children.length === 0;
  };

  const setupReply = (thread) => {
    if (!reply || !client || !currentUser || thread.is_locked) return;
    reply.hidden = false;
  };

  reply?.addEventListener("submit", async (event) => {
    event.preventDefault();
    if (!client || !reply.checkValidity()) return;

    const submit = reply.querySelector('button[type="submit"]');
    submit.disabled = true;
    if (replyStatus) {
      replyStatus.hidden = false;
      replyStatus.textContent = "Posting reply…";
    }

    const { error } = await client
      .from("forum_posts")
      .insert({ thread_id: Number(threadId), body: reply.elements.body.value.trim() });

    if (error) {
      submit.disabled = false;
      if (replyStatus) replyStatus.textContent = error.message || "Could not post reply.";
      return;
    }

    const nextTotal = postTotal + 1;
    const destinationPage = Math.max(1, Math.ceil(nextTotal / PAGE_SIZE));
    window.location.assign(
      assetBase + "/forum/thread/?id=" + encodeURIComponent(threadId) + "&page=" + destinationPage
    );
  });

  const goToPage = (page) => {
    window.location.assign(
      assetBase + "/forum/thread/?id=" + encodeURIComponent(threadId) + "&page=" + page
    );
  };

  previous?.addEventListener("click", () => {
    if (currentPage > 1) goToPage(currentPage - 1);
  });

  next?.addEventListener("click", () => {
    goToPage(currentPage + 1);
  });

  Promise.all([
    request(
      "forum_threads",
      "id,title,is_locked,is_pinned,created_at,category:forum_categories!forum_threads_category_id_fkey(name),author:profiles!forum_threads_author_id_fkey(id,display_name)",
      [["id", "eq." + threadId]]
    ),
    requestPosts(),
    client ? client.auth.getSession() : Promise.resolve({ data: { session: null } }),
  ])
    .then(async ([threads, postsResult, sessionResult]) => {
      const posts = Array.isArray(postsResult.rows) ? postsResult.rows : [];
      postTotal = postsResult.total;
      const pageCount = Math.max(1, Math.ceil(postTotal / PAGE_SIZE));
      if (postTotal > 0 && currentPage > pageCount) {
        goToPage(pageCount);
        return;
      }

      currentUser = sessionResult.data.session?.user || null;
      if (client && currentUser) {
        const moderatorResult = await client.rpc("is_forum_moderator");
        isModerator = !moderatorResult.error && Boolean(moderatorResult.data);
      }

      const postIds = (posts || []).map((post) => post.id);
      if (postIds.length) {
        try {
          reactionRows = await request(
            "forum_reactions",
            "post_id,user_id,emoji",
            [["post_id", "in.(" + postIds.join(",") + ")"]]
          );
        } catch {
          reactionRows = [];
        }

        if (isModerator && client) {
          const reportResult = await client
            .from("forum_reports")
            .select(
              "id,post_id,reason,status,created_at,reporter:profiles!forum_reports_reporter_id_fkey(display_name)"
            )
            .in("post_id", postIds)
            .eq("status", "open");
          reportRows = reportResult.error ? [] : reportResult.data || [];
        }
      }
      const thread = Array.isArray(threads) ? threads[0] : null;
      if (!thread) throw new Error("Discussion not found");

      titleEl.textContent = thread.title;
      categoryEl.textContent = thread.category?.name || "Discussion";
      metaEl.textContent = [
        thread.author?.display_name,
        formatDate(thread.created_at),
        thread.is_pinned ? "Pinned" : null,
        thread.is_locked ? "Locked" : null,
      ].filter(Boolean).join(" · ");
      document.title = thread.title + " - Commonwealth Online";

      postsEl.replaceChildren();
      if (!posts?.length) {
        const empty = document.createElement("p");
        empty.className = "forum-empty";
        empty.textContent = "This discussion has no posts yet.";
        postsEl.append(empty);
      } else {
        for (const post of posts) postsEl.append(renderPost(post));
      }

      postsEl.hidden = false;
      statusEl.hidden = true;

      if (pagination && previous && next && pageLabel) {
        pagination.hidden = pageCount <= 1;
        previous.disabled = currentPage <= 1;
        next.disabled = currentPage >= pageCount;
        pageLabel.textContent = "Page " + currentPage + " of " + pageCount;
      }

      setupThreadActions(thread);
      setupReply(thread);
    })
    .catch(() => {
      statusEl.textContent = "Discussion not found or temporarily unavailable.";
    });
})();
