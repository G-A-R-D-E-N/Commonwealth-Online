(() => {
  const root = document.querySelector("[data-members]");
  if (!root) return;

  const url = (root.dataset.supabaseUrl || "").replace(/\/+$/, "");
  const key = root.dataset.supabaseKey || "";
  const status = root.querySelector("[data-members-status]");
  const list = root.querySelector("[data-members-list]");
  const search = root.querySelector("[data-members-search]");
  const count = root.querySelector("[data-members-count]");
  const brandLogo = document.querySelector(".site-brand__logo");
  const assetBase = brandLogo ? new URL(brandLogo.src).pathname.split("/assets/")[0] : "";
  let rows = [];

  if (!status || !list) return;

  const setStatus = (message, tone = "") => {
    status.textContent = message;
    status.dataset.tone = tone;
    status.hidden = !message;
  };

  const firstRelated = (value) => Array.isArray(value) ? value[0] : value;

  const render = () => {
    const needle = String(search?.value || "").trim().toLowerCase();
    const filtered = rows.filter((row) =>
      String(row.display_name || "").toLowerCase().includes(needle)
    );

    list.replaceChildren();
    for (const row of filtered) {
      const details = firstRelated(row.user_profile_details);
      const presence = firstRelated(row.user_presence);
      const card = document.createElement("a");
      card.className = "member-tile";
      card.href = assetBase + "/member/?username=" + encodeURIComponent(row.display_name);

      const img = document.createElement("img");
      const avatar = row.avatar_url || "/assets/branding/CommonwealthOnlineIconLogo.svg";
      img.src = avatar.startsWith("/") ? assetBase + avatar : avatar;
      img.alt = "";
      img.width = 64;
      img.height = 64;

      const copy = document.createElement("div");
      copy.className = "member-tile__copy";
      const name = document.createElement("strong");
      const meta = document.createElement("span");
      name.textContent = row.display_name || "Unnamed member";
      meta.textContent = [details?.faction, presence?.status === "in_game" ? "In game" : null]
        .filter(Boolean)
        .join(" · ") || "Commonwealth Online member";

      copy.append(name, meta);
      card.append(img, copy);
      list.append(card);
    }

    list.hidden = false;
    if (count) count.textContent = String(rows.length);
    if (filtered.length) {
      setStatus("");
    } else {
      setStatus(needle ? "No members match that search." : "No public members found.", "empty");
    }
  };

  const fetchWithRest = async () => {
    const endpoint = new URL(url + "/rest/v1/profiles");
    endpoint.searchParams.set(
      "select",
      "id,display_name,avatar_url,user_profile_details!inner(faction,is_public),user_presence(status)"
    );
    endpoint.searchParams.set("user_profile_details.is_public", "eq.true");
    endpoint.searchParams.set("order", "display_name.asc");

    const response = await fetch(endpoint, {
      headers: {
        apikey: key,
      },
    });
    if (!response.ok) {
      throw new Error("Member directory request failed");
    }
    return response.json();
  };

  const fetchMembers = async () => {
    if (window.supabase?.createClient) {
      const client = window.coSupabase || window.supabase.createClient(url, key);
      window.coSupabase = client;
      const { data, error } = await client
        .from("profiles")
        .select("id,display_name,avatar_url,user_profile_details!inner(faction,is_public),user_presence(status)")
        .eq("user_profile_details.is_public", true)
        .order("display_name");
      if (error) throw error;
      return data || [];
    }

    return fetchWithRest();
  };

  search?.addEventListener("input", render);

  if (!url || !key) {
    if (count) count.textContent = "—";
    setStatus("Member directory is not configured.", "error");
    return;
  }

  setStatus("Loading member directory…", "loading");
  fetchMembers()
    .then((data) => {
      rows = Array.isArray(data) ? data : [];
      render();
    })
    .catch(() => {
      list.hidden = true;
      if (count) count.textContent = "—";
      setStatus("Member directory is temporarily unavailable.", "error");
    });
})();
