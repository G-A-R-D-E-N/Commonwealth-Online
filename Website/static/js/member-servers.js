(() => {
  const root = document.querySelector("[data-member]");
  if (!root) return;

  const list = root.querySelector("[data-member-recent-servers-list]");
  const card = root.querySelector("[data-member-recent-servers-card]");
  const url = (root.dataset.supabaseUrl || "").replace(/\/+$/, "");
  const key = root.dataset.supabaseKey || "";
  const params = new URLSearchParams(window.location.search);
  const publicUsername = params.get("username");
  const legacyMemberId = params.get("id");
  if (!list || !card || !url || !key || (!publicUsername && !legacyMemberId) || !window.supabase?.createClient) return;

  const client = window.coSupabase || window.supabase.createClient(url, key);
  window.coSupabase = client;

  const formatDuration = (seconds) => {
    const total = Math.max(0, Number(seconds) || 0);
    const hours = Math.floor(total / 3600);
    const minutes = Math.floor((total % 3600) / 60);
    if (hours) return `${hours}h ${minutes}m`;
    return `${minutes}m`;
  };

  const resolveMemberId = async () => {
    if (legacyMemberId) return legacyMemberId;

    // `display_name` is not guaranteed unique, so resolve to a single row
    // explicitly rather than relying on `maybeSingle` (which errors on
    // duplicate display names).
    const { data, error } = await client
      .from("profiles")
      .select("id")
      .eq("display_name", publicUsername)
      .limit(2);

    if (error || !Array.isArray(data) || data.length !== 1 || !data[0]?.id) return null;
    return data[0].id;
  };

  const load = async () => {
    const memberId = await resolveMemberId();
    if (!memberId) {
      card.hidden = true;
      return;
    }

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
