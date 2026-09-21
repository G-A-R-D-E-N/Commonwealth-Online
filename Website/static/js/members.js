(() => {
  const root = document.querySelector("[data-members]");
  if (!root) return;

  const url = (root.dataset.supabaseUrl || "").replace(/\/+$/, "");
  const key = root.dataset.supabaseKey || "";
  const status = root.querySelector("[data-members-status]");
  const list = root.querySelector("[data-members-list]");
  const search = root.querySelector("[data-members-search]");
  const brandLogo = document.querySelector(".site-brand__logo");
  const assetBase = brandLogo ? new URL(brandLogo.src).pathname.split("/assets/")[0] : "";
  if (!url || !key || !window.supabase?.createClient || !status || !list) return;

  const client = window.coSupabase || window.supabase.createClient(url, key);
  window.coSupabase = client;
  let rows = [];

  const render = () => {
    const needle = String(search?.value || "").trim().toLowerCase();
    const filtered = rows.filter((row) =>
      String(row.display_name || "").toLowerCase().includes(needle)
    );

    list.replaceChildren();
    for (const row of filtered) {
      const details = row.user_profile_details?.[0];
      const presence = row.user_presence?.[0];
      const card = document.createElement("a");
      card.className = "member-tile";
      card.href = assetBase + "/member/?id=" + encodeURIComponent(row.id);

      const img = document.createElement("img");
      img.src = assetBase + row.avatar_url;
      img.alt = "";
      img.width = 72;
      img.height = 72;

      const copy = document.createElement("div");
      const name = document.createElement("strong");
      const meta = document.createElement("span");
      name.textContent = row.display_name;
      meta.textContent = [details?.faction, presence?.status === "in_game" ? "In game" : null]
        .filter(Boolean)
        .join(" · ") || "Commonwealth Online member";

      copy.append(name, meta);
      card.append(img, copy);
      list.append(card);
    }

    list.hidden = false;
    status.textContent = filtered.length ? "" : "No public members found.";
    status.hidden = filtered.length > 0;
  };

  search?.addEventListener("input", render);

  client
    .from("profiles")
    .select("id,display_name,avatar_url,user_profile_details!inner(faction,is_public),user_presence(status)")
    .eq("user_profile_details.is_public", true)
    .order("display_name")
    .then(({ data, error }) => {
      if (error) {
        status.textContent = "Member directory is temporarily unavailable.";
        return;
      }
      rows = data || [];
      render();
    });
})();
