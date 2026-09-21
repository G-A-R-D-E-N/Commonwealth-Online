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
    actionsCard: root.querySelector("[data-member-actions-card]"),
    friend: root.querySelector("[data-friend-action]"),
    block: root.querySelector("[data-block-action]"),
    actionStatus: root.querySelector("[data-member-action-status]"),
  };

  let currentUser = null;
  let friendship = null;
  let blocked = false;

  const renderFriendButton = () => {
    if (!els.friend) return;

    if (blocked) {
      els.friend.hidden = true;
      return;
    }

    els.friend.hidden = false;

    if (!friendship) {
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

  els.friend?.addEventListener("click", async () => {
    if (!currentUser) return;
    els.friend.disabled = true;

    if (!friendship) {
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
      client
        .from("profiles")
        .select("id,display_name,avatar_url,created_at,user_profile_details!inner(bio,faction,playstyle,is_public,show_presence,show_friends,show_joined_at),user_presence(status,current_server,last_seen_at)")
        .eq("id", memberId)
        .eq("user_profile_details.is_public", true)
        .maybeSingle(),
      client.auth.getSession(),
    ]);

    const member = memberResult.data;
    if (memberResult.error || !member) {
      status.textContent = "This profile is private or unavailable.";
      return;
    }

    currentUser = sessionResult.data.session?.user || null;
    const details = member.user_profile_details?.[0] || {};
    const presence = member.user_presence?.[0];

    els.avatar.src = assetBase + member.avatar_url;
    els.name.textContent = member.display_name;
    els.bio.textContent = details.bio || "No bio provided.";
    els.faction.textContent = details.faction || "Not set";
    els.playstyle.textContent = details.playstyle || "Not set";
    els.joinedRow.hidden = !details.show_joined_at;
    els.joined.textContent = details.show_joined_at
      ? new Date(member.created_at).toLocaleDateString()
      : "Hidden";

    if (details.show_presence && presence) {
      els.presence.textContent =
        presence.status === "in_game" && presence.current_server
          ? "In game · " + presence.current_server
          : String(presence.status || "").replace("_", " ");
    } else {
      els.presence.textContent = "";
    }

    status.hidden = true;
    profile.hidden = false;
    await loadRelationship();
  };

  initialize();
})();
