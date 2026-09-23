(() => {
  const root = document.querySelector("[data-profile]");
  if (!root) return;

  const list = root.querySelector("[data-badges-list]");
  const manageButton = root.querySelector("[data-badges-manage]");
  const modal = root.querySelector("[data-badges-modal]");
  const modalList = root.querySelector("[data-badges-modal-list]");
  const count = root.querySelector("[data-badges-count]");
  const status = root.querySelector("[data-profile-status]");
  const url = (root.dataset.supabaseUrl || "").replace(/\/+$/, "");
  const key = root.dataset.supabaseKey || "";
  if (!list || !url || !key || !window.supabase?.createClient) return;

  const client = window.coSupabase || window.supabase.createClient(url, key);
  window.coSupabase = client;
  let user = null;
  let badges = [];
  let selectedIds = new Set();

  const setStatus = (message, error = false) => {
    if (!status) return;
    status.hidden = !message;
    status.textContent = message || "";
    status.classList.toggle("is-error", error);
  };

  const updateCount = () => {
    if (!count) return;
    const displayedCount = selectedIds.size;
    count.textContent = `${displayedCount}/3 selected`;
    count.classList.toggle("is-full", displayedCount >= 3);
  };

  const renderDisplayed = () => {
    list.replaceChildren();

    if (!badges.length) {
      list.textContent = "No earned badges yet.";
      return;
    }

    const displayed = badges.filter((row) => row.is_displayed);
    if (!displayed.length) {
      list.textContent = "No badges selected to display.";
      return;
    }

    for (const row of displayed) {
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
      list.append(item);
    }
  };

  const renderModal = () => {
    if (!modalList) return;
    modalList.replaceChildren();

    if (!badges.length) {
      modalList.textContent = "No earned badges yet.";
      updateCount();
      return;
    }

    for (const row of badges) {
      const badge = row.badge;
      if (!badge) continue;

      const item = document.createElement("button");
      item.className = "badge-picker-row";
      item.type = "button";
      item.dataset.badgeId = row.badge_id;
      item.setAttribute("aria-pressed", String(selectedIds.has(row.badge_id)));
      item.classList.toggle("is-selected", selectedIds.has(row.badge_id));

      const copy = document.createElement("span");
      copy.className = "badge-picker-row__copy";
      const name = document.createElement("strong");
      const description = document.createElement("small");
      name.textContent = badge.name;
      description.textContent = badge.description || "Commonwealth Online badge";
      copy.append(name, description);

      const mark = document.createElement("span");
      mark.className = "badge-picker-row__mark";
      mark.textContent = "✓";
      mark.setAttribute("aria-hidden", "true");

      item.append(copy, mark);

      item.addEventListener("click", () => {
        const badgeId = row.badge_id;
        const isSelected = selectedIds.has(badgeId);
        if (isSelected) {
          selectedIds.delete(badgeId);
        } else {
          const displayedCount = selectedIds.size;
          if (displayedCount >= 3) {
            setStatus("You can display at most three badges.", true);
            return;
          }
          selectedIds.add(badgeId);
        }
        setStatus("");
        renderModal();
      });

      modalList.append(item);
    }

    updateCount();
  };

  const openModal = () => {
    selectedIds = new Set(badges.filter((row) => row.is_displayed).map((row) => row.badge_id));
    setStatus("");
    renderModal();
    if (typeof modal.showModal === "function") {
      modal.showModal();
    } else {
      modal.setAttribute("open", "");
    }
  };

  const closeModal = () => {
    if (typeof modal.close === "function") {
      modal.close();
    } else {
      modal.removeAttribute("open");
    }
  };

  const saveSelection = async () => {
    const ordered = badges.filter((row) => selectedIds.has(row.badge_id));
    const updates = [];

    for (const row of badges) {
      const shouldDisplay = selectedIds.has(row.badge_id);
      if (shouldDisplay === row.is_displayed) continue;

      if (shouldDisplay) {
        updates.push(
          client
            .from("user_badge_assignments")
            .update({ is_displayed: true, display_order: ordered.findIndex((entry) => entry.badge_id === row.badge_id) })
            .eq("user_id", user.id)
            .eq("badge_id", row.badge_id)
        );
      } else {
        updates.push(
          client
            .from("user_badge_assignments")
            .update({ is_displayed: false })
            .eq("user_id", user.id)
            .eq("badge_id", row.badge_id)
        );
      }
    }

    const results = await Promise.all(updates);
    const failed = results.some((result) => result.error);
    if (failed) {
      setStatus("Could not save your badges.", true);
      return false;
    }

    setStatus("Badges updated.");
    return true;
  };

  const loadBadges = async () => {
    const { data, error } = await client
      .from("user_badge_assignments")
      .select("badge_id,is_displayed,display_order,badge:user_badges!user_badge_assignments_badge_id_fkey(slug,name,description)")
      .eq("user_id", user.id)
      .order("display_order", { ascending: true })
      .order("badge_id", { ascending: true });

    if (error) {
      list.textContent = "Badges are temporarily unavailable.";
      return;
    }

    badges = data || [];
    renderDisplayed();
  };

  manageButton?.addEventListener("click", openModal);
  root.querySelector("[data-badges-modal-close]")?.addEventListener("click", closeModal);
  root.querySelector("[data-badges-modal-cancel]")?.addEventListener("click", closeModal);
  root.querySelector("[data-badges-modal-confirm]")?.addEventListener("click", async () => {
    const saved = await saveSelection();
    if (saved) {
      closeModal();
      await loadBadges();
    }
  });
  modal?.addEventListener("cancel", (event) => {
    event.preventDefault();
    closeModal();
  });

  const initialize = async () => {
    const { data } = await client.auth.getSession();
    user = data.session?.user || null;
    if (!user) return;
    await loadBadges();
  };

  initialize();
})();
