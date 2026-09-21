(() => {
  const mount = document.querySelector("[data-co-navbar]");
  if (!mount) {
    return;
  }

  const AVATARS = [
    "/assets/profile-icons/armorer.png",
    "/assets/profile-icons/hacker.png",
    "/assets/profile-icons/rifleman.png",
    "/assets/profile-icons/medic.png",
    "/assets/profile-icons/scrapper.png",
    "/assets/profile-icons/cap_collector.png",
    "/assets/profile-images/Icon__Brotherhood.png",
    "/assets/profile-images/Icon__Institute.png",
    "/assets/profile-images/Icon__Minutemen.png",
    "/assets/profile-images/Icon__Railroad.png",
  ];

  const toggle = mount.querySelector(".site-nav-toggle");
  const nav = mount.querySelector(".site-nav");
  const accountLink = mount.querySelector("[data-account-nav]");
  const accountIcon = mount.querySelector("[data-account-nav-icon]");
  const accountAvatar = mount.querySelector("[data-account-nav-avatar]");
  const notificationBadge = mount.querySelector("[data-account-nav-notifications]");
  const brandLogo = document.querySelector(".site-brand__logo");
  const assetBase = brandLogo ? new URL(brandLogo.src).pathname.split("/assets/")[0] : "";
  const accountHref = `${assetBase}/account/`;
  const profileHref = `${assetBase}/profile/`;
  const groups = mount.querySelectorAll ? [...mount.querySelectorAll("[data-nav-group]")] : [];
  let notificationUserId = null;

  if (!toggle || !nav) {
    return;
  }

  const setOpen = (open) => {
    toggle.setAttribute("aria-expanded", String(open));
    toggle.setAttribute("aria-label", open ? "Close navigation" : "Open navigation");
    nav.classList.toggle("is-open", open);
  };

  const closeGroup = (group) => {
    const trigger = group.querySelector("[data-nav-trigger]");
    if (trigger && trigger.getAttribute("aria-expanded") === "true") {
      trigger.setAttribute("aria-expanded", "false");
    }
    group.classList.remove("is-open");
  };

  const closeAllGroups = () => groups.forEach(closeGroup);

  groups.forEach((group) => {
    const trigger = group.querySelector("[data-nav-trigger]");
    if (!trigger) {
      return;
    }
    trigger.addEventListener("click", (event) => {
      event.stopPropagation();
      const expanded = trigger.getAttribute("aria-expanded") === "true";
      closeAllGroups();
      if (!expanded) {
        trigger.setAttribute("aria-expanded", "true");
        group.classList.add("is-open");
      }
    });
  });

  document.addEventListener("click", (event) => {
    if (event.target instanceof HTMLElement && event.target.closest("[data-nav-group]")) {
      return;
    }
    closeAllGroups();
  });

  const showSignedOut = () => {
    if (!accountLink) {
      return;
    }
    accountLink.href = accountHref;
    accountLink.classList.remove("site-nav__profile");
    accountLink.setAttribute("aria-label", "Login or sign up");
    accountLink.title = "Login or sign up";
    if (accountIcon) {
      accountIcon.hidden = false;
    }
    if (accountAvatar) {
      accountAvatar.hidden = true;
    }
    if (notificationBadge) {
      notificationBadge.hidden = true;
      notificationBadge.textContent = "";
    }
    notificationUserId = null;
  };

  toggle.addEventListener("click", () => {
    const expanded = toggle.getAttribute("aria-expanded") === "true";
    setOpen(!expanded);
  });

  nav.addEventListener("click", (event) => {
    const clickedLink = event.target instanceof HTMLElement && event.target.closest("a");
    if (!clickedLink || window.matchMedia("(min-width: 761px)").matches) {
      return;
    }
    setOpen(false);
  });

  document.addEventListener("keydown", (event) => {
    if (event.key !== "Escape") {
      return;
    }
    closeAllGroups();
    if (toggle.getAttribute("aria-expanded") === "true") {
      setOpen(false);
    }
  });

  window.matchMedia("(min-width: 761px)").addEventListener("change", (event) => {
    if (event.matches) {
      setOpen(false);
      closeAllGroups();
    }
  });

  const url = (mount.dataset.supabaseUrl || "").replace(/\/+$/, "");
  const key = mount.dataset.supabaseKey || "";
  if (!accountLink || !url || !key || !window.supabase?.createClient) {
    showSignedOut();
    return;
  }

  const client = window.coSupabase || window.supabase.createClient(url, key);
  window.coSupabase = client;

  const loadUnreadNotifications = async (user) => {
    if (!notificationBadge || notificationUserId === user.id) {
      return;
    }

    notificationUserId = user.id;
    const { count, error } = await client
      .from("user_notifications")
      .select("id", { count: "exact", head: true })
      .is("read_at", null);

    if (error || !count) {
      notificationBadge.hidden = true;
      notificationBadge.textContent = "";
      return;
    }

    notificationBadge.textContent = count > 99 ? "99+" : String(count);
    notificationBadge.setAttribute(
      "aria-label",
      count === 1 ? "1 unread notification" : `${count} unread notifications`
    );
    notificationBadge.hidden = false;
  };

  const showSignedIn = (user) => {
    const displayName = user.user_metadata?.display_name || "Profile";
    let avatar = user.user_metadata?.avatar_url || AVATARS[0];

    if (!AVATARS.includes(avatar)) {
      avatar = AVATARS[0];
    }

    accountLink.href = profileHref;
    accountLink.classList.add("site-nav__profile");
    accountLink.setAttribute("aria-label", `${displayName} profile`);
    accountLink.title = "Profile";
    if (accountIcon) {
      accountIcon.hidden = true;
    }
    if (accountAvatar) {
      accountAvatar.src = `${assetBase}${avatar}`;
      accountAvatar.hidden = false;
    }
    loadUnreadNotifications(user);
  };

  const renderSession = (session) => {
    if (!session?.user) {
      showSignedOut();
      return;
    }
    showSignedIn(session.user);
  };

  document.addEventListener("co:notifications-cleared", () => {
    if (!notificationBadge) return;
    notificationBadge.hidden = true;
    notificationBadge.textContent = "";
  });

  client.auth.getSession().then(({ data }) => {
    renderSession(data.session);
  });

  client.auth.onAuthStateChange((_event, session) => {
    window.setTimeout(() => renderSession(session), 0);
  });
})();
