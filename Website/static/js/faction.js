(() => {
  const root = document.querySelector("[data-faction]");
  if (!root) return;

  const url = (root.dataset.supabaseUrl || "").replace(/\/+$/, "");
  const key = root.dataset.supabaseKey || "";
  const status = root.querySelector("[data-faction-status]");
  const members = root.querySelector("[data-faction-members]");
  const brandLogo = document.querySelector(".site-brand__logo");
  const assetBase = brandLogo ? new URL(brandLogo.src).pathname.split("/assets/")[0] : "";
  if (!url || !key || !window.supabase?.createClient) return;

  const client = window.coSupabase || window.supabase.createClient(url, key);
  window.coSupabase = client;

  const setStatus = (message, error = false) => {
    if (!status) return;
    status.hidden = !message;
    status.textContent = message || "";
    status.classList.toggle("is-error", error);
  };

  const label = (value) =>
    String(value || "")
      .replaceAll("_", " ")
      .replace(/\b\w/g, (character) => character.toUpperCase());

  const factionId = new URLSearchParams(window.location.search).get("id");
  if (!factionId) {
    setStatus("Faction not found.", true);
    return;
  }

  const loadFaction = async () => {
    const { data, error } = await client
      .from("factions")
      .select("id,name,tag,summary,lore,focus,recruitment,status")
      .eq("id", factionId)
      .single();

    if (error || !data) {
      setStatus("Faction not found.", true);
      return null;
    }

    root.querySelector("[data-faction-name]").textContent = data.name;
    root.querySelector("[data-faction-tag]").textContent = data.tag;
    root.querySelector("[data-faction-summary]").textContent = data.summary;
    root.querySelector("[data-faction-lore]").textContent = data.lore;
    root.querySelector("[data-faction-focus]").textContent = label(data.focus);
    root.querySelector("[data-faction-recruitment]").textContent = label(data.recruitment);
    document.title = data.name + " - Commonwealth Online";
    return data;
  };

  const loadMembers = async () => {
    if (!members) return;

    const [{ data: roleRows, error: roleError }, { data: memberRows, error: memberError }] =
      await Promise.all([
        client
          .from("faction_roles")
          .select("id,name,priority")
          .eq("faction_id", factionId),
        client
          .from("faction_members")
          .select("user_id,role_id,joined_at,user:profiles!faction_members_user_id_fkey(id,display_name,avatar_url)")
          .eq("faction_id", factionId)
          .eq("status", "active")
          .order("joined_at", { ascending: true }),
      ]);

    members.replaceChildren();

    if (roleError || memberError) {
      members.textContent = "Faction roster is temporarily unavailable.";
      return;
    }

    if (!memberRows?.length) {
      members.textContent = "No active members.";
      return;
    }

    const roles = new Map((roleRows || []).map((role) => [role.id, role]));

    for (const row of memberRows) {
      if (!row.user) continue;
      const item = document.createElement("a");
      item.className = "profile-social-row";
      item.href = assetBase + "/member/?id=" + encodeURIComponent(row.user.id);

      const identity = document.createElement("span");
      identity.className = "profile-social-row__identity";

      const avatar = document.createElement("img");
      avatar.src = assetBase + row.user.avatar_url;
      avatar.alt = "";
      avatar.width = 40;
      avatar.height = 40;

      const copy = document.createElement("span");
      const name = document.createElement("strong");
      const rank = document.createElement("small");
      name.textContent = row.user.display_name;
      rank.textContent = roles.get(row.role_id)?.name || "Member";

      copy.append(name, rank);
      identity.append(avatar, copy);
      item.append(identity);
      members.append(item);
    }
  };

  const initialize = async () => {
    const faction = await loadFaction();
    if (!faction) return;
    await loadMembers();
  };

  initialize();
})();
