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
  ];

  const toggle = mount.querySelector(".site-nav-toggle");
  const nav = mount.querySelector(".site-nav");
  const accountLink = mount.querySelector("[data-account-nav]");
  const accountLabel = mount.querySelector("[data-account-nav-label]");
  const accountAvatar = mount.querySelector("[data-account-nav-avatar]");
  const brandLogo = document.querySelector(".site-brand__logo");
  const assetBase = brandLogo ? new URL(brandLogo.src).pathname.split("/assets/")[0] : "";
  const accountHref = `${assetBase}/account/`;
  const profileHref = `${assetBase}/profile/`;

  if (!toggle || !nav) {
    return;
  }

  const setOpen = (open) => {
    toggle.setAttribute("aria-expanded", String(open));
    toggle.setAttribute("aria-label", open ? "Close navigation" : "Open navigation");
    nav.classList.toggle("is-open", open);
  };

  const showSignedOut = () => {
    if (!accountLink) {
      return;
    }
    accountLink.href = accountHref;
    accountLink.classList.remove("site-nav__profile");
    accountLink.setAttribute("aria-label", "Login or sign up");
    accountLink.removeAttribute("title");
    if (accountLabel) {
      accountLabel.hidden = false;
      accountLabel.textContent = "Login / Sign Up";
    }
    if (accountAvatar) {
      accountAvatar.hidden = true;
    }
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
    if (event.key === "Escape" && toggle.getAttribute("aria-expanded") === "true") {
      setOpen(false);
    }
  });

  window.matchMedia("(min-width: 761px)").addEventListener("change", (event) => {
    if (event.matches) {
      setOpen(false);
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

  const showSignedIn = async (user) => {
    let displayName = user.user_metadata?.display_name || "Profile";
    let avatar = user.user_metadata?.avatar_url || AVATARS[0];

    const { data: profile } = await client
      .from("profiles")
      .select("display_name,avatar_url")
      .eq("id", user.id)
      .single();

    if (profile) {
      displayName = profile.display_name || displayName;
      avatar = profile.avatar_url || avatar;
    }

    if (!AVATARS.includes(avatar)) {
      avatar = AVATARS[0];
    }

    accountLink.href = profileHref;
    accountLink.classList.add("site-nav__profile");
    accountLink.setAttribute("aria-label", `${displayName} profile`);
    accountLink.title = "Profile";
    if (accountLabel) {
      accountLabel.hidden = true;
    }
    if (accountAvatar) {
      accountAvatar.src = `${assetBase}${avatar}`;
      accountAvatar.hidden = false;
    }
  };

  const renderSession = async (session) => {
    if (!session?.user) {
      showSignedOut();
      return;
    }
    await showSignedIn(session.user);
  };

  client.auth.getSession().then(({ data }) => {
    renderSession(data.session);
  });

  client.auth.onAuthStateChange((_event, session) => {
    window.setTimeout(() => renderSession(session), 0);
  });
})();
