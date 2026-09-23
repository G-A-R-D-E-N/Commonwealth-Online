(() => {
  const root = document.querySelector("[data-member]");
  if (!root) return;

  const list = root.querySelector("[data-member-characters-list]");
  const card = root.querySelector("[data-member-characters-card]");
  const url = (root.dataset.supabaseUrl || "").replace(/\/+$/, "");
  const key = root.dataset.supabaseKey || "";
  const params = new URLSearchParams(window.location.search);
  const publicUsername = params.get("username");
  const legacyMemberId = params.get("id");
  if (!list || !card || !url || !key || (!publicUsername && !legacyMemberId) || !window.supabase?.createClient) return;

  const client = window.coSupabase || window.supabase.createClient(url, key);
  window.coSupabase = client;

  const resolveMemberId = async () => {
    if (legacyMemberId) return legacyMemberId;

    const { data, error } = await client
      .from("profiles")
      .select("id")
      .eq("display_name", publicUsername)
      .maybeSingle();

    if (error || !data?.id) return null;
    return data.id;
  };

  const load = async () => {
    const memberId = await resolveMemberId();
    if (!memberId) {
      card.hidden = true;
      return;
    }

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
