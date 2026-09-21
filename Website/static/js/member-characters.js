(() => {
  const root = document.querySelector("[data-member]");
  if (!root) return;

  const list = root.querySelector("[data-member-characters-list]");
  const card = root.querySelector("[data-member-characters-card]");
  const url = (root.dataset.supabaseUrl || "").replace(/\/+$/, "");
  const key = root.dataset.supabaseKey || "";
  const params = new URLSearchParams(window.location.search);
  const memberId = params.get("id");
  if (!list || !card || !url || !key || !memberId || !window.supabase?.createClient) return;

  const client = window.coSupabase || window.supabase.createClient(url, key);
  window.coSupabase = client;

  const load = async () => {
    const { data, error } = await client.rpc("get_public_user_characters", {
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
      name.textContent = row.character_name;
      detail.textContent = [
        `Level ${row.level}`,
        row.faction,
        row.server_name,
        row.last_played_at ? new Date(row.last_played_at).toLocaleDateString() : null,
      ].filter(Boolean).join(" · ");

      copy.append(name, detail);
      item.append(copy);
      list.append(item);
    }

    card.hidden = false;
  };

  load();
})();
