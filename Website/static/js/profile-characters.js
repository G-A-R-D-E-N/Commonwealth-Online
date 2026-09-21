(() => {
  const root = document.querySelector("[data-profile]");
  if (!root) return;

  const list = root.querySelector("[data-characters-list]");
  const url = (root.dataset.supabaseUrl || "").replace(/\/+$/, "");
  const key = root.dataset.supabaseKey || "";
  if (!list || !url || !key || !window.supabase?.createClient) return;

  const client = window.coSupabase || window.supabase.createClient(url, key);
  window.coSupabase = client;

  const renderCharacters = (rows) => {
    list.replaceChildren();

    if (!rows.length) {
      list.textContent = "No synchronized characters yet.";
      return;
    }

    for (const row of rows) {
      const item = document.createElement("div");
      item.className = "profile-social-row";

      const copy = document.createElement("span");
      const name = document.createElement("strong");
      const detail = document.createElement("small");
      name.textContent = row.character_name;

      const parts = [
        `Level ${row.level}`,
        row.faction,
        row.server_name,
        row.last_played_at ? new Date(row.last_played_at).toLocaleDateString() : null,
      ].filter(Boolean);
      detail.textContent = parts.join(" · ");

      copy.append(name, detail);
      item.append(copy);
      list.append(item);
    }
  };

  const initialize = async () => {
    const { data: sessionData } = await client.auth.getSession();
    const user = sessionData.session?.user || null;
    if (!user) return;

    const { data, error } = await client
      .from("user_characters")
      .select("id,server_id,server_name,character_name,level,faction,last_played_at")
      .eq("user_id", user.id)
      .order("last_played_at", { ascending: false, nullsFirst: false })
      .limit(20);

    if (error) {
      list.textContent = "Characters are temporarily unavailable.";
      return;
    }

    renderCharacters(data || []);
  };

  initialize();
})();
