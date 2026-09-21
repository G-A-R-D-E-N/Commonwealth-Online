(() => {
  const root = document.querySelector("[data-member]");
  if (!root) return;

  const list = root.querySelector("[data-member-recent-servers-list]");
  const card = root.querySelector("[data-member-recent-servers-card]");
  const url = (root.dataset.supabaseUrl || "").replace(/\/+$/, "");
  const key = root.dataset.supabaseKey || "";
  const params = new URLSearchParams(window.location.search);
  const memberId = params.get("id");
  if (!list || !card || !url || !key || !memberId || !window.supabase?.createClient) return;

  const client = window.coSupabase || window.supabase.createClient(url, key);
  window.coSupabase = client;

  const formatDuration = (seconds) => {
    const total = Math.max(0, Number(seconds) || 0);
    const hours = Math.floor(total / 3600);
    const minutes = Math.floor((total % 3600) / 60);
    if (hours) return `${hours}h ${minutes}m`;
    return `${minutes}m`;
  };

  const load = async () => {
    const { data, error } = await client.rpc("get_public_recent_servers", {
      p_user_id: memberId,
    });

    list.replaceChildren();

    if (error) {
      card.hidden = true;
      return;
    }

    const rows = Array.isArray(data) ? data : [];
    if (!rows.length) {
      card.hidden = true;
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
      list.append(item);
    }

    card.hidden = false;
  };

  load();
})();
