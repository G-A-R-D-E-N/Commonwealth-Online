(() => {
  const root = document.querySelector("[data-member]");
  if (!root) return;

  const url = (root.dataset.supabaseUrl || "").replace(/\/+$/, "");
  const key = root.dataset.supabaseKey || "";
  const status = root.querySelector("[data-member-status]");
  const profile = root.querySelector("[data-member-profile]");
  const params = new URLSearchParams(window.location.search);
  const publicUsername = params.get("username");
  const legacyMemberId = params.get("id");
  let memberId = legacyMemberId || null;
  const brandLogo = document.querySelector(".site-brand__logo");
  const assetBase = brandLogo ? new URL(brandLogo.src).pathname.split("/assets/")[0] : "";

  if (!url || !key || (!publicUsername && !legacyMemberId)) {
    status.textContent = "Profile not found.";
    return;
  }

  const client = window.supabase?.createClient
    ? window.coSupabase || window.supabase.createClient(url, key)
    : null;
  if (client) window.coSupabase = client;

  const els = {
    avatar: root.querySelector("[data-member-avatar]"),
    name: root.querySelector("[data-member-name]"),
    presence: root.querySelector("[data-member-presence]"),
    bio: root.querySelector("[data-member-bio]"),
    faction: root.querySelector("[data-member-faction]"),
    playstyle: root.querySelector("[data-member-playstyle]"),
    joined: root.querySelector("[data-member-joined]"),
    joinedRow: root.querySelector("[data-member-joined-row]"),
    badgeChips: root.querySelector("[data-member-badge-chips]"),
    badgesCard: root.querySelector("[data-member-badges-card]"),
    badgesList: root.querySelector("[data-member-badges-list]"),
    usernameHistoryCard: root.querySelector("[data-member-username-history-card]"),
    usernameHistoryList: root.querySelector("[data-member-username-history-list]"),
    friendsCard: root.querySelector("[data-member-friends-card]"),
    friendsList: root.querySelector("[data-member-friends-list]"),
    friend: root.querySelector("[data-friend-action]"),
    block: root.querySelector("[data-block-action]"),
    actionStatus: root.querySelector("[data-member-action-status]"),
    editProfile: root.querySelector("[data-member-edit-profile]"),
  };

  let currentUser = null;
  let friendship = null;
  let blocked = false;
  let blockedByOwner = false;

  const profileHref = (username) =>
    assetBase + "/member/?username=" + encodeURIComponent(username);

  const restHeaders = {
    apikey: key,
    "Content-Type": "application/json",
  };

  const resolveMemberId = async () => {
    if (memberId) return memberId;

    if (client) {
      // `display_name` is not guaranteed unique, so resolve to a single row
      // explicitly instead of relying on `maybeSingle` (which errors on
      // duplicates). Require exactly one match, mirroring the REST fallback.
      const { data, error } = await client
        .from("profiles")
        .select("id,display_name")
        .eq("display_name", publicUsername)
        .limit(2);

      if (error || !Array.isArray(data) || data.length !== 1 || !data[0]?.id) {
        throw new Error("Profile not found");
      }
      memberId = data[0].id;
      return memberId;
    }

    const endpoint = new URL(url + "/rest/v1/profiles");
    endpoint.searchParams.set("select", "id,display_name");
    endpoint.searchParams.set("display_name", "eq." + publicUsername);
    const response = await fetch(endpoint, { headers: restHeaders });
    if (!response.ok) throw new Error("Profile not found");
    const rows = await response.json();
    if (!Array.isArray(rows) || rows.length !== 1 || !rows[0]?.id) {
      throw new Error("Profile not found");
    }
    memberId = rows[0].id;
    return memberId;
  };

  const callPublicRpc = async (name, args) => {
    if (client) return client.rpc(name, args);

    const response = await fetch(url + "/rest/v1/rpc/" + name, {
      method: "POST",
      headers: restHeaders,
      body: JSON.stringify(args),
    });
    if (!response.ok) return { data: null, error: true };
    return { data: await response.json(), error: null };
  };

  const resolveFactionSlug = async (id) => {
    if (!id) return null;

    if (client) {
      const { data, error } = await client
        .from("factions")
        .select("slug")
        .eq("id", id)
        .maybeSingle();
      return error ? null : data?.slug || null;
    }

    const endpoint = new URL(url + "/rest/v1/factions");
    endpoint.searchParams.set("select", "slug");
    endpoint.searchParams.set("id", "eq." + id);
    const response = await fetch(endpoint, { headers: restHeaders });
    if (!response.ok) return null;
    const rows = await response.json();
    return Array.isArray(rows) && rows.length === 1 ? rows[0]?.slug || null : null;
  };

  const renderActions = () => {
    const isOwner = currentUser && currentUser.id === memberId;
    const canAct = currentUser && !isOwner && !blockedByOwner;
    if (els.editProfile) els.editProfile.hidden = !isOwner;
    if (els.friend) els.friend.hidden = !canAct || blocked;
    if (els.block) els.block.hidden = !canAct;
  };

  const renderFriendButton = () => {
    if (!els.friend) return;

    const isOwner = currentUser && currentUser.id === memberId;
    if (!currentUser || isOwner || blocked || blockedByOwner) {
      els.friend.hidden = true;
      return;
    }

    els.friend.hidden = false;

    if (!friendship || friendship.status === "declined") {
      els.friend.textContent = "Add friend";
      els.friend.disabled = false;
      return;
    }

    if (friendship.status === "accepted") {
      els.friend.textContent = "Friends";
      els.friend.disabled = true;
      return;
    }

    if (friendship.requester_id === currentUser?.id) {
      els.friend.textContent = "Request sent";
      els.friend.disabled = true;
      return;
    }

    els.friend.textContent = "Accept friend";
    els.friend.disabled = false;
  };

  const loadRelationship = async () => {
    if (!currentUser || currentUser.id === memberId) return;

    const pairFilter =
      "and(requester_id.eq." + currentUser.id + ",addressee_id.eq." + memberId + ")," +
      "and(requester_id.eq." + memberId + ",addressee_id.eq." + currentUser.id + ")";

    const [friendResult, relationshipResult] = await Promise.all([
      client
        .from("user_friendships")
        .select("id,requester_id,addressee_id,status")
        .or(pairFilter)
        .maybeSingle(),
      client.rpc("get_member_relationship", {
        p_target_user_id: memberId,
      }),
    ]);

    friendship = friendResult.data || null;
    const relationship = relationshipResult.data || {};
    blocked = relationship.blocked_target === true;
    blockedByOwner = relationship.blocked_by_target === true;
    els.block.textContent = blocked ? "Unblock" : "Block";
    renderActions();
    renderFriendButton();
  };

  const loadPublicBadges = async () => {
    if (!els.badgesCard || !els.badgesList) return;

    const { data, error } = await client
      .from("user_badge_assignments")
      .select("badge_id,display_order,badge:user_badges!user_badge_assignments_badge_id_fkey(name,description)")
      .eq("user_id", memberId)
      .eq("is_displayed", true)
      .order("display_order", { ascending: true })
      .limit(3);

    els.badgesList.replaceChildren();
    if (els.badgeChips) els.badgeChips.replaceChildren();

    if (error) {
      els.badgesCard.hidden = true;
      return;
    }

    const rows = data || [];
    if (!rows.length) {
      els.badgesCard.hidden = true;
      return;
    }

    for (const row of rows) {
      const badge = row.badge;
      if (!badge) continue;

      if (els.badgeChips) {
        const chip = document.createElement("span");
        chip.className = "member-profile__badge-chip";
        chip.textContent = badge.name;
        chip.title = badge.description || badge.name;
        els.badgeChips.append(chip);
      }

      const item = document.createElement("div");
      item.className = "profile-social-row";

      const copy = document.createElement("span");
      const name = document.createElement("strong");
      const description = document.createElement("small");
      name.textContent = badge.name;
      description.textContent = badge.description || "Commonwealth Online badge";
      copy.append(name, description);
      item.append(copy);
      els.badgesList.append(item);
    }

    els.badgesCard.hidden = false;
  };

  const loadPublicUsernameHistory = async () => {
    if (!els.usernameHistoryCard || !els.usernameHistoryList) return;

    const { data, error } = await client.rpc("get_public_username_history", {
      p_user_id: memberId,
    });

    els.usernameHistoryList.replaceChildren();

    if (error) {
      els.usernameHistoryCard.hidden = true;
      return;
    }

    const rows = Array.isArray(data) ? data : [];
    if (!rows.length) {
      els.usernameHistoryCard.hidden = true;
      return;
    }

    for (const row of rows) {
      const item = document.createElement("div");
      item.className = "profile-social-row";

      const copy = document.createElement("span");
      const name = document.createElement("strong");
      const changed = document.createElement("small");
      name.textContent = row.username;
      changed.textContent = new Date(row.changed_at).toLocaleDateString();
      copy.append(name, changed);
      item.append(copy);
      els.usernameHistoryList.append(item);
    }

    els.usernameHistoryCard.hidden = false;
  };

  const loadPublicFriends = async (visible) => {
    if (!els.friendsCard || !els.friendsList || !visible) {
      if (els.friendsCard) els.friendsCard.hidden = true;
      return;
    }

    const { data, error } = await client.rpc("get_public_member_friends", {
      p_user_id: memberId,
    });

    els.friendsCard.hidden = false;
    els.friendsList.replaceChildren();

    if (error) {
      els.friendsList.textContent = "Friends are temporarily unavailable.";
      return;
    }

    const rows = Array.isArray(data) ? data : [];
    if (!rows.length) {
      els.friendsList.textContent = "No public friends to show.";
      return;
    }

    for (const row of rows) {
      const item = document.createElement("a");
      item.className = "profile-social-row";
      item.href = profileHref(row.display_name);

      const avatar = document.createElement("img");
      avatar.src = assetBase + row.avatar_url;
      avatar.alt = "";
      avatar.width = 40;
      avatar.height = 40;

      const name = document.createElement("strong");
      name.textContent = row.display_name;

      item.append(avatar, name);
      els.friendsList.append(item);
    }
  };

  const resetDeclinedFriendship = async () => {
    if (!friendship || friendship.status !== "declined") return true;

    const { error } = await client
      .from("user_friendships")
      .delete()
      .eq("id", friendship.id);

    if (error) return false;
    friendship = null;
    return true;
  };

  els.friend?.addEventListener("click", async () => {
    if (!currentUser) return;
    els.friend.disabled = true;

    if (!friendship || friendship.status === "declined") {
      const reset = await resetDeclinedFriendship();
      if (!reset) {
        els.actionStatus.textContent = "Could not reset the previous friend request.";
        els.friend.disabled = false;
        return;
      }

      const { data, error } = await client
        .from("user_friendships")
        .insert({ requester_id: currentUser.id, addressee_id: memberId })
        .select("id,requester_id,addressee_id,status")
        .single();

      if (error) {
        els.actionStatus.textContent = "Could not send friend request.";
        els.friend.disabled = false;
        return;
      }

      friendship = data;
    } else if (
      friendship.status === "pending" &&
      friendship.addressee_id === currentUser.id
    ) {
      const { data, error } = await client
        .from("user_friendships")
        .update({ status: "accepted" })
        .eq("id", friendship.id)
        .select("id,requester_id,addressee_id,status")
        .single();

      if (error) {
        els.actionStatus.textContent = "Could not accept friend request.";
        els.friend.disabled = false;
        return;
      }

      friendship = data;
    }

    els.actionStatus.textContent = "";
    renderFriendButton();
  });

  els.block?.addEventListener("click", async () => {
    if (!currentUser) return;
    els.block.disabled = true;

    const result = blocked
      ? await client
          .from("user_blocks")
          .delete()
          .eq("blocker_id", currentUser.id)
          .eq("blocked_id", memberId)
      : await client
          .from("user_blocks")
          .insert({ blocker_id: currentUser.id, blocked_id: memberId });

    if (result.error) {
      els.actionStatus.textContent = "Could not update block status.";
      els.block.disabled = false;
      return;
    }

    blocked = !blocked;
    if (blocked) friendship = null;
    els.block.textContent = blocked ? "Unblock" : "Block";
    els.block.disabled = false;
    els.actionStatus.textContent = "";
    renderActions();
    renderFriendButton();
  });

  const initialize = async () => {
    try {
      await resolveMemberId();
    } catch {
      status.textContent = "This profile is private or unavailable.";
      return;
    }

    const [memberResult, sessionResult] = await Promise.all([
      callPublicRpc("get_public_member_profile", { p_user_id: memberId }),
      client ? client.auth.getSession() : Promise.resolve({ data: { session: null } }),
    ]);

    const member = Array.isArray(memberResult.data)
      ? memberResult.data[0]
      : memberResult.data;
    if (memberResult.error || !member) {
      status.textContent = "This profile is private or unavailable.";
      return;
    }

    currentUser = sessionResult.data.session?.user || null;

    const canonicalUrl =
      assetBase + "/member/?username=" + encodeURIComponent(member.display_name);
    window.history?.replaceState(null, "", canonicalUrl);

    els.avatar.src = assetBase + member.avatar_url;
    els.name.textContent = member.display_name;
    els.bio.textContent = member.bio || "No bio provided.";
    els.faction.replaceChildren();
    if (member.faction_id && member.faction_name) {
      const factionSlug = await resolveFactionSlug(member.faction_id);
      if (factionSlug) {
        const factionLink = document.createElement("a");
        factionLink.href = assetBase + "/faction/?slug=" + encodeURIComponent(factionSlug);
        factionLink.textContent =
          member.faction_name + (member.faction_tag ? " [" + member.faction_tag + "]" : "");
        els.faction.append(factionLink);
      } else {
        els.faction.textContent =
          member.faction_name + (member.faction_tag ? " [" + member.faction_tag + "]" : "");
      }
    } else {
      els.faction.textContent = "Not set";
    }
    els.playstyle.textContent = member.playstyle || "Not set";
    els.joinedRow.hidden = !member.joined_at;
    els.joined.textContent = member.joined_at
      ? new Date(member.joined_at).toLocaleDateString()
      : "Hidden";

    if (member.presence_status) {
      els.presence.hidden = false;
      els.presence.textContent =
        member.presence_status === "in_game" && member.current_server
          ? "In game · " + member.current_server
          : String(member.presence_status).replace("_", " ");
      els.presence.classList.toggle(
        "is-online",
        member.presence_status === "online"
      );
      els.presence.classList.toggle(
        "is-in-game",
        member.presence_status === "in_game"
      );
    } else {
      els.presence.hidden = true;
      els.presence.textContent = "";
    }

    status.hidden = true;
    profile.hidden = false;
    renderActions();

    if (client) {
      await Promise.all([
        loadRelationship(),
        loadPublicBadges(),
        loadPublicUsernameHistory(),
        loadPublicFriends(Boolean(member.show_friends)),
      ]);
    }
  };

  initialize();
})();
