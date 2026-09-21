(() => {
  const root = document.querySelector("[data-member]");
  if (!root) return;

  const url = (root.dataset.supabaseUrl || "").replace(/\/+$/, "");
  const key = root.dataset.supabaseKey || "";
  const status = root.querySelector("[data-member-status]");
  const profile = root.querySelector("[data-member-profile]");
  const params = new URLSearchParams(window.location.search);
  const memberId = params.get("id");
  const brandLogo = document.querySelector(".site-brand__logo");
  const assetBase = brandLogo ? new URL(brandLogo.src).pathname.split("/assets/")[0] : "";

  if (!url || !key || !memberId || !window.supabase?.createClient) {
    status.textContent = "Profile not found.";
    return;
  }

  const client = window.coSupabase || window.supabase.createClient(url, key);
  window.coSupabase = client;

  const els = {
    avatar: root.querySelector("[data-member-avatar]"),
    name: root.querySelector("[data-member-name]"),
    presence: root.querySelector("[data-member-presence]"),
    bio: root.querySelector("[data-member-bio]"),
    faction: root.querySelector("[data-member-faction]"),
    playstyle: root.querySelector("[data-member-playstyle]"),
    joined: root.querySelector("[data-member-joined]"),
    joinedRow: root.querySelector("[data-member-joined-row]"),
    badgesCard: root.querySelector("[data-member-badges-card]"),
    badgesList: root.querySelector("[data-member-badges-list]"),
    usernameHistoryCard: root.querySelector("[data-member-username-history-card]"),
    usernameHistoryList: root.querySelector("[data-member-username-history-list]"),
    friendsCard: root.querySelector("[data-member-friends-card]"),
    friendsList: root.querySelector("[data-member-friends-list]"),
    actionsCard: root.querySelector("[data-member-actions-card]"),
    friend: root.querySelector("[data-friend-action]"),
    block: root.querySelector("[data-block-action]"),
    actionStatus: root.querySelector("[data-member-action-status]"),
  };

  let currentUser = null;
  let friendship = null;
  let blocked = false;

  const profileHref = (id) => assetBase + "/member/?id=" + encodeURIComponent(id);

  const renderFriendButton = () => {
    if (!els.friend) return;

    if (blocked) {
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

    const [friendResult, blockResult] = await Promise.all([
      client
        .from("user_friendships")
        .select("id,requester_id,addressee_id,status")
        .or(pairFilter)
        .maybeSingle(),
      client
        .from("user_blocks")
        .select("blocker_id,blocked_id")
        .eq("blocker_id", currentUser.id)
        .eq("blocked_id", memberId)
        .maybeSingle(),
    ]);

    friendship = friendResult.data || null;
    blocked = Boolean(blockResult.data);
    els.actionsCard.hidden = false;
    els.block.textContent = blocked ? "Unblock" : "Block";
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
      item.href = profileHref(row.id);

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
    renderFriendButton();
  });

  const initialize = async () => {
    const [memberResult, sessionResult] = await Promise.all([
      client.rpc("get_public_member_profile", { p_user_id: memberId }),
      client.auth.getSession(),
    ]);

    const member = Array.isArray(memberResult.data)
      ? memberResult.data[0]
      : memberResult.data;
    if (memberResult.error || !member) {
      status.textContent = "This profile is private or unavailable.";
      return;
    }

    currentUser = sessionResult.data.session?.user || null;

    els.avatar.src = assetBase + member.avatar_url;
    els.name.textContent = member.display_name;
    els.bio.textContent = member.bio || "No bio provided.";
    els.faction.replaceChildren();
    if (member.faction_id && member.faction_name) {
      const factionLink = document.createElement("a");
      factionLink.href = assetBase + "/faction/?id=" + encodeURIComponent(member.faction_id);
      factionLink.textContent =
        member.faction_name + (member.faction_tag ? " [" + member.faction_tag + "]" : "");
      els.faction.append(factionLink);
    } else {
      els.faction.textContent = "Not set";
    }
    els.playstyle.textContent = member.playstyle || "Not set";
    els.joinedRow.hidden = !member.joined_at;
    els.joined.textContent = member.joined_at
      ? new Date(member.joined_at).toLocaleDateString()
      : "Hidden";

    if (member.presence_status) {
      els.presence.textContent =
        member.presence_status === "in_game" && member.current_server
          ? "In game · " + member.current_server
          : String(member.presence_status).replace("_", " ");
    } else {
      els.presence.textContent = "";
    }

    status.hidden = true;
    profile.hidden = false;

    await Promise.all([
      loadRelationship(),
      loadPublicBadges(),
      loadPublicUsernameHistory(),
      loadPublicFriends(Boolean(member.show_friends)),
    ]);
  };

  initialize();
})();
