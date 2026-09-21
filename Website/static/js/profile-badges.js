(() => {
  const root = document.querySelector("[data-profile]");
  if (!root) return;

  const list = root.querySelector("[data-badges-list]");
  const url = (root.dataset.supabaseUrl || "").replace(/\/+$/, "");
  const key = root.dataset.supabaseKey || "";
  if (!list || !url || !key || !window.supabase?.createClient) return;

  const client = window.coSupabase || window.supabase.createClient(url, key);
  window.coSupabase = client;
  let user = null;

  const loadBadges = async () => {
    const { data, error } = await client
      .from("user_badge_assignments")
      .select("badge_id,is_displayed,display_order,badge:user_badges!user_badge_assignments_badge_id_fkey(slug,name,description)")
      .eq("user_id", user.id)
      .order("display_order", { ascending: true })
      .order("badge_id", { ascending: true });

    list.replaceChildren();

    if (error) {
      list.textContent = "Badges are temporarily unavailable.";
      return;
    }

    const rows = data || [];
    if (!rows.length) {
      list.textContent = "No earned badges yet.";
      return;
    }

    const displayedCount = rows.filter((row) => row.is_displayed).length;

    for (const row of rows) {
      const badge = row.badge;
      if (!badge) continue;

      const item = document.createElement("div");
      item.className = "profile-social-row";

      const copy = document.createElement("span");
      const name = document.createElement("strong");
      const description = document.createElement("small");
      name.textContent = badge.name;
      description.textContent = badge.description || "Commonwealth Online badge";
      copy.append(name, description);

      const toggle = document.createElement("button");
      toggle.className = row.is_displayed ? "co-btn co-btn--ghost" : "co-btn co-btn--primary";
      toggle.type = "button";
      toggle.textContent = row.is_displayed ? "Hide" : "Show";
      toggle.disabled = !row.is_displayed && displayedCount >= 3;

      toggle.addEventListener("click", async () => {
        toggle.disabled = true;
        const { error: updateError } = await client
          .from("user_badge_assignments")
          .update({ is_displayed: !row.is_displayed })
          .eq("user_id", user.id)
          .eq("badge_id", row.badge_id);

        if (updateError) {
          toggle.disabled = false;
          return;
        }

        await loadBadges();
      });

      item.append(copy, toggle);
      list.append(item);
    }
  };

  const initialize = async () => {
    const { data } = await client.auth.getSession();
    user = data.session?.user || null;
    if (!user) return;
    await loadBadges();
  };

  initialize();
})();
