(() => {
  const root = document.querySelector("[data-profile]");
  if (!root) return;

  const list = root.querySelector("[data-notifications-list]");
  const markRead = root.querySelector("[data-mark-notifications-read]");
  const status = root.querySelector("[data-profile-status]");
  const brandLogo = document.querySelector(".site-brand__logo");
  const assetBase = brandLogo ? new URL(brandLogo.src).pathname.split("/assets/")[0] : "";
  const url = (root.dataset.supabaseUrl || "").replace(/\/+$/, "");
  const key = root.dataset.supabaseKey || "";
  if (!list || !url || !key || !window.supabase?.createClient) return;

  const client = window.coSupabase || window.supabase.createClient(url, key);
  window.coSupabase = client;
  let user = null;

  const setStatus = (message, error = false) => {
    if (!status) return;
    status.hidden = !message;
    status.textContent = message || "";
    status.classList.toggle("is-error", error);
  };

  const loadNotifications = async () => {
    const { data, error } = await client
      .from("user_notifications")
      .select("id,type,title,body,target_url,read_at,created_at,actor:profiles!user_notifications_actor_id_fkey(id,display_name,avatar_url)")
      .order("created_at", { ascending: false })
      .limit(20);

    list.replaceChildren();

    if (error) {
      list.textContent = "Notifications are temporarily unavailable.";
      return;
    }

    const rows = data || [];
    if (!rows.length) {
      list.textContent = "No notifications.";
      return;
    }

    for (const row of rows) {
      const item = document.createElement(row.target_url ? "a" : "div");
      item.className = "profile-social-row";
      if (row.target_url) {
        item.href = row.target_url.startsWith("/") ? assetBase + row.target_url : row.target_url;
      }

      if (row.actor?.avatar_url) {
        const avatar = document.createElement("img");
        avatar.src = assetBase + row.actor.avatar_url;
        avatar.alt = "";
        avatar.width = 40;
        avatar.height = 40;
        item.append(avatar);
      }

      const copy = document.createElement("span");
      const title = document.createElement("strong");
      const body = document.createElement("small");
      title.textContent = row.title + (row.read_at ? "" : " · New");
      body.textContent = row.body || new Date(row.created_at).toLocaleString();
      copy.append(title, body);
      item.append(copy);

      if (row.target_url && !row.read_at) {
        item.addEventListener("click", async (event) => {
          event.preventDefault();
          await client
            .from("user_notifications")
            .update({ read_at: new Date().toISOString() })
            .eq("id", row.id);
          window.location.assign(item.href);
        });
      }

      list.append(item);
    }
  };

  markRead?.addEventListener("click", async () => {
    if (!user) return;
    markRead.disabled = true;
    const { error } = await client
      .from("user_notifications")
      .update({ read_at: new Date().toISOString() })
      .eq("user_id", user.id)
      .is("read_at", null);

    if (error) {
      setStatus("Could not update notifications.", true);
    } else {
      document.dispatchEvent(new CustomEvent("co:notifications-cleared"));
    }

    await loadNotifications();
    markRead.disabled = false;
  });

  document.addEventListener("co:notifications-refresh", loadNotifications);

  const initialize = async () => {
    const { data } = await client.auth.getSession();
    user = data.session?.user || null;
    if (!user) return;
    await loadNotifications();
  };

  initialize();
})();
