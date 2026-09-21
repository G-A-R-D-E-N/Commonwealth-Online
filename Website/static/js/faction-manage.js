(() => {
  const root = document.querySelector("[data-faction-manage]");
  if (!root) return;

  const url = (root.dataset.supabaseUrl || "").replace(/\/+$/, "");
  const key = root.dataset.supabaseKey || "";
  const status = root.querySelector("[data-faction-manage-status]");
  const contentRoot = root.querySelector("[data-faction-manage-content]");
  const requestList = root.querySelector("[data-faction-request-list]");
  const memberList = root.querySelector("[data-faction-manage-members]");
  const inviteForm = root.querySelector("[data-faction-invite-search]");
  const inviteResults = root.querySelector("[data-faction-invite-results]");
  const factionName = root.querySelector("[data-faction-manage-name]");
  const backLink = root.querySelector("[data-faction-manage-back]");
  const brandLogo = document.querySelector(".site-brand__logo");
  const assetBase = brandLogo ? new URL(brandLogo.src).pathname.split("/assets/")[0] : "";
  const factionId = new URLSearchParams(window.location.search).get("id");
  if (!url || !key || !factionId || !window.supabase?.createClient) {
    status.textContent = "Faction management is unavailable.";
    return;
  }

  const client = window.coSupabase || window.supabase.createClient(url, key);
  window.coSupabase = client;
  let user = null;

  const profileHref = (id) => assetBase + "/member/?id=" + encodeURIComponent(id);

  const setStatus = (message, error = false) => {
    status.hidden = !message;
    status.textContent = message || "";
    status.classList.toggle("is-error", error);
  };

  const createIdentity = (person, detail) => {
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
    const small = document.createElement("small");
    name.textContent = person.display_name;
    small.textContent = detail;
    copy.append(name, small);
    identity.append(avatar, copy);
    return identity;
  };

  const actionButton = (label, handler, primary = false) => {
    const button = document.createElement("button");
    button.className = primary ? "co-btn co-btn--primary" : "co-btn co-btn--ghost";
    button.type = "button";
    button.textContent = label;
    button.addEventListener("click", async () => {
      button.disabled = true;
      const ok = await handler();
      if (!ok) button.disabled = false;
    });
    return button;
  };

  const loadRequests = async () => {
    const { data, error } = await client
      .from("faction_members")
      .select("user_id,status,user:profiles!faction_members_user_id_fkey(id,display_name,avatar_url)")
      .eq("faction_id", factionId)
      .eq("status", "pending")
      .order("created_at", { ascending: true });

    requestList.replaceChildren();

    if (error) {
      requestList.textContent = "Membership requests are temporarily unavailable.";
      return;
    }

    if (!data?.length) {
      requestList.textContent = "No pending membership requests.";
      return;
    }

    for (const row of data) {
      if (!row.user) continue;

      const item = document.createElement("div");
      item.className = "profile-social-row";
      item.append(createIdentity(row.user, "Membership request"));

      item.append(
        actionButton("Approve", async () => {
          const { error: actionError } = await client.rpc("respond_faction_membership", {
            p_faction_id: factionId,
            p_user_id: row.user_id,
            p_accept: true,
          });
          if (actionError) {
            setStatus(actionError.message, true);
            return false;
          }
          setStatus("");
          await Promise.all([loadRequests(), loadMembers()]);
          return true;
        }, true),
        actionButton("Reject", async () => {
          const { error: actionError } = await client.rpc("respond_faction_membership", {
            p_faction_id: factionId,
            p_user_id: row.user_id,
            p_accept: false,
          });
          if (actionError) {
            setStatus(actionError.message, true);
            return false;
          }
          setStatus("");
          await loadRequests();
          return true;
        })
      );

      requestList.append(item);
    }
  };

  const loadMembers = async () => {
    const [membersResult, rolesResult] = await Promise.all([
      client
        .from("faction_members")
        .select("user_id,role_id,status,user:profiles!faction_members_user_id_fkey(id,display_name,avatar_url)")
        .eq("faction_id", factionId)
        .eq("status", "active")
        .order("joined_at", { ascending: true }),
      client
        .from("faction_roles")
        .select("id,name,priority")
        .eq("faction_id", factionId),
    ]);

    memberList.replaceChildren();

    if (membersResult.error || rolesResult.error) {
      memberList.textContent = "Faction roster is temporarily unavailable.";
      return;
    }

    const roles = new Map((rolesResult.data || []).map((role) => [role.id, role]));

    for (const row of membersResult.data || []) {
      if (!row.user) continue;

      const role = roles.get(row.role_id);
      const item = document.createElement("div");
      item.className = "profile-social-row";
      item.append(createIdentity(row.user, role?.name || "Member"));

      if (row.user_id !== user.id && role?.name !== "Leader") {
        item.append(
          actionButton("Remove", async () => {
            const { error } = await client.rpc("remove_faction_member", {
              p_faction_id: factionId,
              p_user_id: row.user_id,
            });
            if (error) {
              setStatus(error.message, true);
              return false;
            }
            setStatus("");
            await loadMembers();
            return true;
          })
        );
      }

      memberList.append(item);
    }
  };

  inviteForm.addEventListener("submit", async (event) => {
    event.preventDefault();
    if (!inviteForm.checkValidity()) return;

    const query = inviteForm.elements.query.value.trim();
    inviteResults.replaceChildren();

    const { data, error } = await client
      .from("profiles")
      .select("id,display_name,avatar_url,user_profile_details!inner(is_public)")
      .eq("user_profile_details.is_public", true)
      .ilike("display_name", "%" + query + "%")
      .limit(10);

    if (error) {
      inviteResults.textContent = "Member search is temporarily unavailable.";
      return;
    }

    const matches = (data || []).filter((person) => person.id !== user.id);
    if (!matches.length) {
      inviteResults.textContent = "No public members matched that username.";
      return;
    }

    for (const person of matches) {
      const item = document.createElement("div");
      item.className = "profile-social-row";
      item.append(createIdentity(person, "Commonwealth Online member"));

      item.append(
        actionButton("Invite", async () => {
          const { error: inviteError } = await client.rpc("invite_faction_member", {
            p_faction_id: factionId,
            p_user_id: person.id,
          });
          if (inviteError) {
            setStatus(inviteError.message, true);
            return false;
          }
          setStatus("Invitation sent.");
          return true;
        }, true)
      );

      inviteResults.append(item);
    }
  });

  const initialize = async () => {
    const { data: sessionData } = await client.auth.getSession();
    user = sessionData.session?.user || null;

    if (!user) {
      setStatus("Sign in to manage a faction.", true);
      return;
    }

    const [factionResult, membershipResult] = await Promise.all([
      client
        .from("factions")
        .select("id,name,tag")
        .eq("id", factionId)
        .single(),
      client
        .from("faction_members")
        .select("role_id,status")
        .eq("faction_id", factionId)
        .eq("user_id", user.id)
        .eq("status", "active")
        .maybeSingle(),
    ]);

    if (factionResult.error || !factionResult.data || !membershipResult.data?.role_id) {
      setStatus("You do not have permission to manage this faction.", true);
      return;
    }

    const { data: role, error: roleError } = await client
      .from("faction_roles")
      .select("can_manage_members")
      .eq("id", membershipResult.data.role_id)
      .eq("faction_id", factionId)
      .maybeSingle();

    if (roleError || !role?.can_manage_members) {
      setStatus("You do not have permission to manage this faction.", true);
      return;
    }

    factionName.textContent = factionResult.data.name + " [" + factionResult.data.tag + "]";
    backLink.href = assetBase + "/faction/?id=" + encodeURIComponent(factionId);
    setStatus("");
    contentRoot.hidden = false;

    await Promise.all([loadRequests(), loadMembers()]);
  };

  initialize();
})();
