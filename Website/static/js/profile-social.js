(() => {
  const root = document.querySelector("[data-profile]");
  if (!root) return;

  const url = (root.dataset.supabaseUrl || "").replace(/\/+$/, "");
  const key = root.dataset.supabaseKey || "";
  const form = root.querySelector("[data-community-profile-form]");
  const usernameHistoryList = root.querySelector("[data-username-history-list]");
  const friendsList = root.querySelector("[data-friends-list]");
  const blocksList = root.querySelector("[data-blocks-list]");
  const notificationsList = root.querySelector("[data-notifications-list]");
  const markRead = root.querySelector("[data-mark-notifications-read]");
  const status = root.querySelector("[data-profile-status]");
  const brandLogo = document.querySelector(".site-brand__logo");
  const assetBase = brandLogo ? new URL(brandLogo.src).pathname.split("/assets/")[0] : "";
  if (!url || !key || !form || !window.supabase?.createClient) return;

  const client = window.coSupabase || window.supabase.createClient(url, key);
  window.coSupabase = client;
  let user = null;

  const setStatus = (message, error = false) => {
    if (!status) return;
    status.hidden = !message;
    status.textContent = message || "";
    status.classList.toggle("is-error", error);
  };

  const profileHref = (id) => assetBase + "/member/?id=" + encodeURIComponent(id);

  const loadDetails = async () => {
    const { data, error } = await client
      .from("user_profile_details")
      .select("bio,faction,playstyle,is_public,show_presence,show_friends,show_joined_at,show_username_history,show_recent_servers")
      .eq("user_id", user.id)
      .single();

    if (error || !data) return;

    form.elements.bio.value = data.bio || "";
    form.elements.faction.value = data.faction || "";
    form.elements.playstyle.value = data.playstyle || "";
    form.elements.is_public.checked = data.is_public;
    form.elements.show_presence.checked = data.show_presence;
    form.elements.show_friends.checked = data.show_friends;
    form.elements.show_joined_at.checked = data.show_joined_at;
    form.elements.show_username_history.checked = data.show_username_history;
    form.elements.show_recent_servers.checked = data.show_recent_servers;
  };

  const loadUsernameHistory = async () => {
    if (!usernameHistoryList) return;

    const { data, error } = await client
      .from("user_username_history")
      .select("id,username,changed_at")
      .eq("user_id", user.id)
      .order("changed_at", { ascending: false })
      .limit(20);

    usernameHistoryList.replaceChildren();

    if (error) {
      usernameHistoryList.textContent = "Username history is temporarily unavailable.";
      return;
    }

    const rows = data || [];
    if (!rows.length) {
      usernameHistoryList.textContent = "No previous usernames.";
      return;
    }

    for (const row of rows) {
      const item = document.createElement("div");
      item.className = "profile-social-row";

      const copy = document.createElement("span");
      const name = document.createElement("strong");
      const changed = document.createElement("small");
      name.textContent = row.username;
      changed.textContent = new Date(row.changed_at).toLocaleString();
      copy.append(name, changed);
      item.append(copy);
      usernameHistoryList.append(item);
    }
  };

  const friendIdentity = (row) =>
    row.requester_id === user.id ? row.addressee : row.requester;

  const loadFriends = async () => {
    if (!friendsList) return;

    const { data, error } = await client
      .from("user_friendships")
      .select("id,requester_id,addressee_id,status,created_at,requester:profiles!user_friendships_requester_id_fkey(id,display_name,avatar_url),addressee:profiles!user_friendships_addressee_id_fkey(id,display_name,avatar_url)")
      .order("created_at", { ascending: false });

    friendsList.replaceChildren();

    if (error) {
      friendsList.textContent = "Friends are temporarily unavailable.";
      return;
    }

    const rows = data || [];
    if (!rows.length) {
      friendsList.textContent = "No friends or pending requests yet.";
      return;
    }

    for (const row of rows) {
      const person = friendIdentity(row);
      if (!person) continue;

      const item = document.createElement("div");
      item.className = "profile-social-row";

      const identity = document.createElement("a");
      identity.className = "profile-social-row__identity";
      identity.href = profileHref(person.id);

      const avatar = document.createElement("img");
      avatar.src = assetBase + person.avatar_url;
      avatar.alt = "";
      avatar.width = 40;
      avatar.height = 40;

      const copy = document.createElement("span");
      const name = document.createElement("strong");
      const detail = document.createElement("small");
      name.textContent = person.display_name;

      const incoming =
        row.status === "pending" &&
        row.addressee_id === user.id;

      detail.textContent =
        row.status === "accepted"
          ? "Friend"
          : incoming
            ? "Incoming request"
            : "Request sent";

      copy.append(name, detail);
      identity.append(avatar, copy);
      item.append(identity);

      if (incoming) {
        const accept = document.createElement("button");
        accept.className = "co-btn co-btn--primary";
        accept.type = "button";
        accept.textContent = "Accept";
        accept.addEventListener("click", async () => {
          accept.disabled = true;
          const { error: updateError } = await client
            .from("user_friendships")
            .update({ status: "accepted" })
            .eq("id", row.id);
          if (updateError) {
            accept.disabled = false;
            setStatus("Could not accept friend request.", true);
            return;
          }
          await Promise.all([loadFriends(), loadNotifications()]);
        });

        const decline = document.createElement("button");
        decline.className = "co-btn co-btn--ghost";
        decline.type = "button";
        decline.textContent = "Decline";
        decline.addEventListener("click", async () => {
          decline.disabled = true;
          const { error: updateError } = await client
            .from("user_friendships")
            .update({ status: "declined" })
            .eq("id", row.id);
          if (updateError) {
            decline.disabled = false;
            setStatus("Could not decline friend request.", true);
            return;
          }
          setStatus("");
          await loadFriends();
        });

        item.append(accept, decline);
      }

      if (row.status === "pending" && row.requester_id === user.id) {
        const cancel = document.createElement("button");
        cancel.className = "co-btn co-btn--ghost";
        cancel.type = "button";
        cancel.textContent = "Cancel request";
        cancel.addEventListener("click", async () => {
          cancel.disabled = true;
          const { error: deleteError } = await client
            .from("user_friendships")
            .delete()
            .eq("id", row.id);
          if (deleteError) {
            cancel.disabled = false;
            setStatus("Could not cancel friend request.", true);
            return;
          }
          setStatus("");
          await loadFriends();
        });
        item.append(cancel);
      }

      if (row.status === "accepted") {
        const remove = document.createElement("button");
        remove.className = "co-btn co-btn--ghost";
        remove.type = "button";
        remove.textContent = "Remove";
        remove.addEventListener("click", async () => {
          remove.disabled = true;
          const { error: deleteError } = await client
            .from("user_friendships")
            .delete()
            .eq("id", row.id);
          if (deleteError) {
            remove.disabled = false;
            setStatus("Could not remove friend.", true);
            return;
          }
          await loadFriends();
        });
        item.append(remove);
      }

      friendsList.append(item);
    }
  };

  const loadBlocks = async () => {
    if (!blocksList) return;

    const { data, error } = await client
      .from("user_blocks")
      .select("blocker_id,blocked_id,created_at,blocked:profiles!user_blocks_blocked_id_fkey(id,display_name,avatar_url)")
      .order("created_at", { ascending: false });

    blocksList.replaceChildren();

    if (error) {
      blocksList.textContent = "Blocked members are temporarily unavailable.";
      return;
    }

    const rows = data || [];
    if (!rows.length) {
      blocksList.textContent = "No blocked members.";
      return;
    }

    for (const row of rows) {
      const person = row.blocked;
      if (!person) continue;

      const item = document.createElement("div");
      item.className = "profile-social-row";

      const identity = document.createElement("a");
      identity.className = "profile-social-row__identity";
      identity.href = profileHref(person.id);

      const avatar = document.createElement("img");
      avatar.src = assetBase + person.avatar_url;
      avatar.alt = "";
      avatar.width = 40;
      avatar.height = 40;

      const copy = document.createElement("span");
      const name = document.createElement("strong");
      const detail = document.createElement("small");
      name.textContent = person.display_name;
      detail.textContent = "Blocked";
      copy.append(name, detail);
      identity.append(avatar, copy);

      const unblock = document.createElement("button");
      unblock.className = "co-btn co-btn--ghost";
      unblock.type = "button";
      unblock.textContent = "Unblock";
      unblock.addEventListener("click", async () => {
        unblock.disabled = true;
        const { error: deleteError } = await client
          .from("user_blocks")
          .delete()
          .eq("blocker_id", user.id)
          .eq("blocked_id", person.id);

        if (deleteError) {
          unblock.disabled = false;
          setStatus("Could not unblock member.", true);
          return;
        }

        setStatus("");
        await loadBlocks();
      });

      item.append(identity, unblock);
      blocksList.append(item);
    }
  };

  const loadNotifications = async () => {
    if (!notificationsList) return;

    const { data, error } = await client
      .from("user_notifications")
      .select("id,type,title,body,target_url,read_at,created_at,actor:profiles!user_notifications_actor_id_fkey(id,display_name,avatar_url)")
      .order("created_at", { ascending: false })
      .limit(20);

    notificationsList.replaceChildren();

    if (error) {
      notificationsList.textContent = "Notifications are temporarily unavailable.";
      return;
    }

    const rows = data || [];
    if (!rows.length) {
      notificationsList.textContent = "No notifications.";
      return;
    }

    for (const row of rows) {
      const item = document.createElement(row.target_url ? "a" : "div");
      item.className = "profile-social-row";
      if (row.target_url) {
        item.href = row.target_url.startsWith("/") ? assetBase + row.target_url : row.target_url;
      }

      if (row.actor?.avatar_url) {
        const avatar = document.createElement("img");
        avatar.src = assetBase + row.actor.avatar_url;
        avatar.alt = "";
        avatar.width = 40;
        avatar.height = 40;
        item.append(avatar);
      }

      const copy = document.createElement("span");
      const title = document.createElement("strong");
      const body = document.createElement("small");
      title.textContent = row.title + (row.read_at ? "" : " · New");
      body.textContent = row.body || new Date(row.created_at).toLocaleString();
      copy.append(title, body);
      item.append(copy);

      if (row.target_url && !row.read_at) {
        item.addEventListener("click", async (event) => {
          event.preventDefault();
          await client
            .from("user_notifications")
            .update({ read_at: new Date().toISOString() })
            .eq("id", row.id);
          window.location.assign(item.href);
        });
      }

      notificationsList.append(item);
    }
  };

  form.addEventListener("submit", async (event) => {
    event.preventDefault();
    if (!user || !form.checkValidity()) return;

    const payload = {
      bio: form.elements.bio.value.trim(),
      faction: form.elements.faction.value.trim() || null,
      playstyle: form.elements.playstyle.value.trim() || null,
      is_public: form.elements.is_public.checked,
      show_presence: form.elements.show_presence.checked,
      show_friends: form.elements.show_friends.checked,
      show_joined_at: form.elements.show_joined_at.checked,
      show_username_history: form.elements.show_username_history.checked,
      show_recent_servers: form.elements.show_recent_servers.checked,
      updated_at: new Date().toISOString(),
    };

    const { error } = await client
      .from("user_profile_details")
      .update(payload)
      .eq("user_id", user.id);

    setStatus(
      error ? "Could not save public profile settings." : "Public profile saved.",
      Boolean(error)
    );
  });

  markRead?.addEventListener("click", async () => {
    if (!user) return;
    markRead.disabled = true;
    const { error } = await client
      .from("user_notifications")
      .update({ read_at: new Date().toISOString() })
      .eq("user_id", user.id)
      .is("read_at", null);

    if (error) {
      setStatus("Could not update notifications.", true);
    } else {
      document.dispatchEvent(new CustomEvent("co:notifications-cleared"));
    }
    await loadNotifications();
    markRead.disabled = false;
  });

  const initialize = async () => {
    const { data } = await client.auth.getSession();
    user = data.session?.user || null;
    if (!user) return;

    await Promise.all([
      loadDetails(),
      loadUsernameHistory(),
      loadFriends(),
      loadBlocks(),
      loadNotifications(),
    ]);
  };

  initialize();
})();
