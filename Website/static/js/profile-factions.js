(() => {
  const root = document.querySelector("[data-profile]");
  const list = root?.querySelector("[data-profile-factions-list]");
  if (!root || !list) return;

  const url = (root.dataset.supabaseUrl || "").replace(/\/+$/, "");
  const key = root.dataset.supabaseKey || "";
  const brandLogo = document.querySelector(".site-brand__logo");
  const assetBase = brandLogo ? new URL(brandLogo.src).pathname.split("/assets/")[0] : "";
  if (!url || !key || !window.supabase?.createClient) return;

  const client = window.coSupabase || window.supabase.createClient(url, key);
  window.coSupabase = client;
  let user = null;
  let primaryFactionId = null;

  const factionHref = (slug) => assetBase + "/faction/?slug=" + encodeURIComponent(slug);
  const label = (value) => String(value || "").replaceAll("_", " ");

  const actionButton = (text, handler, primary = false) => {
    const button = document.createElement("button");
    button.className = primary ? "co-btn co-btn--primary" : "co-btn co-btn--ghost";
    button.type = "button";
    button.textContent = text;
    button.addEventListener("click", async () => {
      button.disabled = true;
      await handler(button);
    });
    return button;
  };

  const load = async () => {
    if (!user) return;

    const [detailsResult, membershipsResult] = await Promise.all([
      client
        .from("user_profile_details")
        .select("primary_faction_id")
        .eq("user_id", user.id)
        .single(),
      client
        .from("faction_members")
        .select("faction_id,status,joined_at,faction:factions!faction_members_faction_id_fkey(id,slug,name,tag,recruitment)")
        .eq("user_id", user.id)
        .in("status", ["active", "pending", "invited"])
        .order("created_at", { ascending: true }),
    ]);

    list.replaceChildren();

    if (detailsResult.error || membershipsResult.error) {
      list.textContent = "Faction memberships are temporarily unavailable.";
      return;
    }

    primaryFactionId = detailsResult.data?.primary_faction_id || null;
    const rows = membershipsResult.data || [];

    if (!rows.length) {
      list.textContent = "You are not currently part of a faction.";
      return;
    }

    for (const row of rows) {
      if (!row.faction) continue;

      const item = document.createElement("div");
      item.className = "profile-social-row";

      const identity = document.createElement("a");
      identity.className = "profile-social-row__identity";
      identity.href = factionHref(row.faction.slug);

      const copy = document.createElement("span");
      const name = document.createElement("strong");
      const detail = document.createElement("small");
      name.textContent = row.faction.name + " [" + row.faction.tag + "]";
      detail.textContent =
        row.status === "active" && row.faction_id === primaryFactionId
          ? "Primary faction"
          : label(row.status);
      copy.append(name, detail);
      identity.append(copy);
      item.append(identity);

      if (row.status === "invited") {
        item.append(
          actionButton("Accept", async () => {
            const { error } = await client.rpc("respond_faction_invite", {
              p_faction_id: row.faction_id,
              p_accept: true,
            });
            if (!error) await load();
          }, true),
          actionButton("Decline", async () => {
            const { error } = await client.rpc("respond_faction_invite", {
              p_faction_id: row.faction_id,
              p_accept: false,
            });
            if (!error) await load();
          })
        );
      } else if (row.status === "pending") {
        item.append(
          actionButton("Cancel request", async () => {
            const { error } = await client.rpc("cancel_faction_membership_request", {
              p_faction_id: row.faction_id,
            });
            if (!error) await load();
          })
        );
      } else if (row.status === "active") {
        if (row.faction_id !== primaryFactionId) {
          item.append(
            actionButton("Set primary", async () => {
              const { error } = await client.rpc("set_primary_faction", {
                p_faction_id: row.faction_id,
              });
              if (!error) await load();
            }, true)
          );
        }

        item.append(
          actionButton("Leave", async (button) => {
            const { error } = await client.rpc("leave_faction", {
              p_faction_id: row.faction_id,
            });
            if (error) {
              button.disabled = false;
              button.textContent = error.message.includes("leaders must transfer")
                ? "Transfer leadership first"
                : "Could not leave";
              return;
            }
            await load();
          })
        );
      }

      list.append(item);
    }
  };

  const initialize = async () => {
    const { data } = await client.auth.getSession();
    user = data.session?.user || null;
    if (!user) return;
    await load();
  };

  initialize();
})();
