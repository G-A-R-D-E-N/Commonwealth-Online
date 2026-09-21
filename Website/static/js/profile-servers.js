(() => {
  const root = document.querySelector("[data-profile]");
  if (!root) return;

  const favoritesList = root.querySelector("[data-server-favorites-list]");
  const historyList = root.querySelector("[data-server-history-list]");
  const url = (root.dataset.supabaseUrl || "").replace(/\/+$/, "");
  const key = root.dataset.supabaseKey || "";
  if (!url || !key || !window.supabase?.createClient) return;

  const client = window.coSupabase || window.supabase.createClient(url, key);
  window.coSupabase = client;
  let user = null;

  const formatDuration = (seconds) => {
    const total = Math.max(0, Number(seconds) || 0);
    const hours = Math.floor(total / 3600);
    const minutes = Math.floor((total % 3600) / 60);
    if (hours) return `${hours}h ${minutes}m`;
    return `${minutes}m`;
  };

  const loadFavorites = async () => {
    if (!favoritesList) return;

    const { data, error } = await client
      .from("user_server_favorites")
      .select("server_id,server_name,created_at")
      .eq("user_id", user.id)
      .order("created_at", { ascending: false });

    favoritesList.replaceChildren();

    if (error) {
      favoritesList.textContent = "Favorite servers are temporarily unavailable.";
      return;
    }

    const rows = data || [];
    if (!rows.length) {
      favoritesList.textContent = "No favorite servers yet.";
      return;
    }

    for (const row of rows) {
      const item = document.createElement("div");
      item.className = "profile-social-row";

      const copy = document.createElement("span");
      const name = document.createElement("strong");
      const detail = document.createElement("small");
      name.textContent = row.server_name;
      detail.textContent = row.server_id;
      copy.append(name, detail);

      const remove = document.createElement("button");
      remove.className = "co-btn co-btn--ghost";
      remove.type = "button";
      remove.textContent = "Remove";
      remove.addEventListener("click", async () => {
        remove.disabled = true;
        const { error: deleteError } = await client
          .from("user_server_favorites")
          .delete()
          .eq("user_id", user.id)
          .eq("server_id", row.server_id);

        if (deleteError) {
          remove.disabled = false;
          return;
        }

        await loadFavorites();
      });

      item.append(copy, remove);
      favoritesList.append(item);
    }
  };

  const loadHistory = async () => {
    if (!historyList) return;

    const { data, error } = await client
      .from("user_server_history")
      .select("server_id,server_name,last_joined_at,session_count,total_seconds")
      .eq("user_id", user.id)
      .order("last_joined_at", { ascending: false })
      .limit(10);

    historyList.replaceChildren();

    if (error) {
      historyList.textContent = "Server history is temporarily unavailable.";
      return;
    }

    const rows = data || [];
    if (!rows.length) {
      historyList.textContent = "No multiplayer sessions recorded yet.";
      return;
    }

    for (const row of rows) {
      const item = document.createElement("div");
      item.className = "profile-social-row";

      const copy = document.createElement("span");
      const name = document.createElement("strong");
      const detail = document.createElement("small");
      name.textContent = row.server_name;
      detail.textContent =
        `${formatDuration(row.total_seconds)} · ${row.session_count} session${row.session_count === 1 ? "" : "s"} · ` +
        new Date(row.last_joined_at).toLocaleDateString();
      copy.append(name, detail);
      item.append(copy);
      historyList.append(item);
    }
  };

  const initialize = async () => {
    const { data } = await client.auth.getSession();
    user = data.session?.user || null;
    if (!user) return;

    await Promise.all([loadFavorites(), loadHistory()]);
  };

  initialize();
})();
