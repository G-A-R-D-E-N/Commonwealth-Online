(() => {
  const root = document.querySelector("[data-faction]");
  if (!root) return;

  const url = (root.dataset.supabaseUrl || "").replace(/\/+$/, "");
  const key = root.dataset.supabaseKey || "";
  const status = root.querySelector("[data-faction-status]");
  const members = root.querySelector("[data-faction-members]");
  const membershipCard = root.querySelector("[data-faction-membership-card]");
  const membershipCopy = root.querySelector("[data-faction-membership-copy]");
  const membershipAction = root.querySelector("[data-faction-membership-action]");
  const membershipSecondary = root.querySelector("[data-faction-membership-secondary]");
  const manageLink = root.querySelector("[data-faction-manage-link]");
  const brandLogo = document.querySelector(".site-brand__logo");
  const assetBase = brandLogo ? new URL(brandLogo.src).pathname.split("/assets/")[0] : "";
  if (!url || !key || !window.supabase?.createClient) return;

  const client = window.coSupabase || window.supabase.createClient(url, key);
  window.coSupabase = client;

  const params = new URLSearchParams(window.location.search);
  const factionSlug = params.get("slug");
  const legacyFactionId = params.get("id");
  let factionId = legacyFactionId || null;
  let currentUser = null;
  let faction = null;
  let membership = null;

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

  if (!factionSlug && !legacyFactionId) {
    setStatus("Faction not found.", true);
    return;
  }

  const loadFaction = async () => {
    let query = client
      .from("factions")
      .select("id,slug,name,tag,summary,lore,focus,recruitment,status");

    query = factionSlug
      ? query.eq("slug", factionSlug)
      : query.eq("id", legacyFactionId);

    const { data, error } = await query.single();

    if (error || !data) {
      setStatus("Faction not found.", true);
      return null;
    }

    factionId = data.id;
    root.querySelector("[data-faction-name]").textContent = data.name;
    root.querySelector("[data-faction-tag]").textContent = data.tag;
    root.querySelector("[data-faction-summary]").textContent = data.summary;
    root.querySelector("[data-faction-lore]").textContent = data.lore;
    root.querySelector("[data-faction-focus]").textContent = label(data.focus);
    root.querySelector("[data-faction-recruitment]").textContent = label(data.recruitment);
    document.title = data.name + " - Commonwealth Online";
    window.history?.replaceState(
      null,
      "",
      assetBase + "/faction/?slug=" + encodeURIComponent(data.slug)
    );
    faction = data;
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
      item.href =
        assetBase + "/member/?username=" + encodeURIComponent(row.user.display_name);

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

  const loadMembership = async () => {
    if (!membershipCard || !membershipAction || !membershipCopy || !faction) return;

    const { data: sessionData } = await client.auth.getSession();
    currentUser = sessionData.session?.user || null;
    membershipCard.hidden = false;
    membershipSecondary.hidden = true;
    membershipSecondary.onclick = null;

    if (!currentUser) {
      membership = null;
      membershipCopy.textContent = "Sign in to join or request membership.";
      membershipAction.textContent = "Sign in";
      membershipAction.disabled = false;
      membershipAction.onclick = () => {
        window.location.assign(assetBase + "/account/");
      };
      return;
    }

    const { data } = await client
      .from("faction_members")
      .select("faction_id,status,role_id")
      .eq("faction_id", factionId)
      .eq("user_id", currentUser.id)
      .maybeSingle();

    membership = data || null;
    membershipAction.disabled = false;

    if (!membership) {
      if (faction.recruitment === "closed") {
        membershipCopy.textContent = "This faction is not recruiting.";
        membershipAction.textContent = "Recruitment closed";
        membershipAction.disabled = true;
        membershipAction.onclick = null;
        return;
      }

      if (faction.recruitment === "invite_only") {
        membershipCopy.textContent = "Membership is currently invite only.";
        membershipAction.textContent = "Invite only";
        membershipAction.disabled = true;
        membershipAction.onclick = null;
        return;
      }

      const open = faction.recruitment === "open";
      membershipCopy.textContent = open
        ? "This faction allows immediate membership."
        : "Send a membership request to faction leadership.";
      membershipAction.textContent = open ? "Join faction" : "Request to join";
      membershipAction.onclick = async () => {
        membershipAction.disabled = true;
        const { error } = await client.rpc("request_faction_membership", {
          p_faction_id: factionId,
        });
        if (error) {
          membershipCopy.textContent = error.message;
          membershipAction.disabled = false;
          return;
        }
        await Promise.all([loadMembership(), loadMembers()]);
      };
      return;
    }

    if (membership.status === "pending") {
      membershipCopy.textContent = "Your membership request is awaiting review.";
      membershipAction.textContent = "Request pending";
      membershipAction.disabled = true;
      membershipSecondary.hidden = false;
      membershipSecondary.textContent = "Cancel request";
      membershipSecondary.onclick = async () => {
        membershipSecondary.disabled = true;
        const { error } = await client.rpc("cancel_faction_membership_request", {
          p_faction_id: factionId,
        });
        membershipSecondary.disabled = false;
        if (!error) await loadMembership();
      };
      return;
    }

    if (membership.status === "invited") {
      membershipCopy.textContent = "Faction leadership invited you to join.";
      membershipAction.textContent = "Accept invitation";
      membershipAction.onclick = async () => {
        membershipAction.disabled = true;
        const { error } = await client.rpc("respond_faction_invite", {
          p_faction_id: factionId,
          p_accept: true,
        });
        if (!error) await Promise.all([loadMembership(), loadMembers()]);
      };
      membershipSecondary.hidden = false;
      membershipSecondary.textContent = "Decline";
      membershipSecondary.onclick = async () => {
        membershipSecondary.disabled = true;
        const { error } = await client.rpc("respond_faction_invite", {
          p_faction_id: factionId,
          p_accept: false,
        });
        membershipSecondary.disabled = false;
        if (!error) await loadMembership();
      };
      return;
    }

    if (membership.status === "active") {
      if (manageLink && membership.role_id) {
        const { data: role } = await client
          .from("faction_roles")
          .select("can_manage_members")
          .eq("id", membership.role_id)
          .eq("faction_id", factionId)
          .maybeSingle();

        if (role?.can_manage_members) {
          manageLink.href =
            assetBase + "/profile/?manageFaction=" + encodeURIComponent(faction.slug);
          manageLink.hidden = false;
        }
      }

      membershipCopy.textContent = "You are an active member of this faction.";
      membershipAction.textContent = "Member";
      membershipAction.disabled = true;
      membershipAction.onclick = null;
      membershipSecondary.hidden = false;
      membershipSecondary.textContent = "Leave faction";
      membershipSecondary.onclick = async () => {
        membershipSecondary.disabled = true;
        const { error } = await client.rpc("leave_faction", {
          p_faction_id: factionId,
        });
        if (error) {
          membershipCopy.textContent = error.message.includes("leaders must transfer")
            ? "Transfer leadership before leaving this faction."
            : error.message;
          membershipSecondary.disabled = false;
          return;
        }
        await Promise.all([loadMembership(), loadMembers()]);
      };
      return;
    }

    membership = null;
    await loadMembership();
  };

  const initialize = async () => {
    const loadedFaction = await loadFaction();
    if (!loadedFaction) return;
    await Promise.all([loadMembers(), loadMembership()]);
  };

  initialize();
})();
