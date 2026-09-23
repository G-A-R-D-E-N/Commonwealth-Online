(() => {
  const root = document.querySelector("[data-profile]");
  if (!root) {
    return;
  }

  // Canonical profile picture catalog. Keep this in sync with the avatar grid
  // rendered in views/pages/profile.ejs and with the avatar allow-list in
  // supabase/migrations/20260922000000_emote_profile_icons.sql.
  const PROFILE_PICTURES = [
    { name: "Armorer", src: "/assets/profile-icons/armorer.png", category: "perks" },
    { name: "Hacker", src: "/assets/profile-icons/hacker.png", category: "perks" },
    { name: "Rifleman", src: "/assets/profile-icons/rifleman.png", category: "perks" },
    { name: "Medic", src: "/assets/profile-icons/medic.png", category: "perks" },
    { name: "Scrapper", src: "/assets/profile-icons/scrapper.png", category: "perks" },
    { name: "Cap Collector", src: "/assets/profile-icons/cap_collector.png", category: "perks" },
    { name: "Brotherhood", src: "/assets/profile-images/Icon__Brotherhood.png", category: "factions" },
    { name: "Institute", src: "/assets/profile-images/Icon__Institute.png", category: "factions" },
    { name: "Minutemen", src: "/assets/profile-images/Icon__Minutemen.png", category: "factions" },
    { name: "Railroad", src: "/assets/profile-images/Icon__Railroad.png", category: "factions" },
    { name: "Commonwealth", src: "/assets/profile-images/CO.png", category: "commonwealth" },
    { name: "Cool", src: "/assets/profile-images/Cool.png", category: "commonwealth" },
    { name: "Love", src: "/assets/profile-images/Love.png", category: "commonwealth" },
    { name: "Rage", src: "/assets/profile-images/Rage.png", category: "commonwealth" },
    { name: "Wink", src: "/assets/profile-images/Wink.png", category: "commonwealth" },
  ];
  const AVATARS = PROFILE_PICTURES.map((picture) => picture.src);

  const url = (root.dataset.supabaseUrl || "").replace(/\/+$/, "");
  const key = root.dataset.supabaseKey || "";
  const status = root.querySelector("[data-profile-status]");
  const profileForm = root.querySelector("[data-profile-form]");
  const emailForm = root.querySelector("[data-email-form]");
  const passwordForm = root.querySelector("[data-password-form]");
  const profileAvatar = root.querySelector("[data-profile-avatar]");
  const avatarCurrent = root.querySelector("[data-profile-avatar-current]");
  const avatarCurrentName = root.querySelector("[data-profile-avatar-current-name]");
  const profileName = root.querySelector("[data-profile-name]");
  const profileEmail = root.querySelector("[data-profile-email]");
  const signOut = root.querySelector("[data-sign-out]");
  const viewPublicProfile = root.querySelector("[data-view-public-profile]");
  const brandLogo = document.querySelector(".site-brand__logo");
  const assetBase = brandLogo ? new URL(brandLogo.src).pathname.split("/assets/")[0] : "";
  const accountUrl = new URL(`${assetBase}/account/`, window.location.origin).href;

  let currentUser = null;

  const setStatus = (message, error = false) => {
    status.hidden = !message;
    status.textContent = message || "";
    status.classList.toggle("is-error", error);
  };

  const avatarSrc = (path) => `${assetBase}${AVATARS.includes(path) ? path : AVATARS[0]}`;
  const avatarName = (path) =>
    (PROFILE_PICTURES.find((picture) => picture.src === path) || PROFILE_PICTURES[0]).name;

  const goToAccount = () => {
    window.location.assign(accountUrl);
  };

  /* ── Tab navigation ───────────────────────────────────────── */

  const tabs = [...root.querySelectorAll("[data-profile-tab]")];
  const panels = [...root.querySelectorAll("[data-profile-panel]")];
  const validTabs = new Set(tabs.map((tab) => tab.dataset.profileTab));

  const activateTab = (name) => {
    if (!validTabs.has(name)) {
      name = tabs[0]?.dataset.profileTab || "profile";
    }
    tabs.forEach((tab) => {
      const active = tab.dataset.profileTab === name;
      tab.classList.toggle("is-active", active);
      tab.setAttribute("aria-selected", String(active));
      tab.tabIndex = active ? 0 : -1;
    });
    panels.forEach((panel) => {
      panel.hidden = panel.dataset.profilePanel !== name;
    });
  };

  tabs.forEach((tab) => {
    tab.addEventListener("click", () => {
      activateTab(tab.dataset.profileTab);
      history.replaceState(null, "", `#${tab.dataset.profileTab}`);
    });
    tab.addEventListener("keydown", (event) => {
      const index = tabs.indexOf(tab);
      let target = null;
      if (event.key === "ArrowRight" || event.key === "ArrowDown") {
        target = tabs[(index + 1) % tabs.length];
      } else if (event.key === "ArrowLeft" || event.key === "ArrowUp") {
        target = tabs[(index - 1 + tabs.length) % tabs.length];
      } else if (event.key === "Home") {
        target = tabs[0];
      } else if (event.key === "End") {
        target = tabs[tabs.length - 1];
      }
      if (target) {
        event.preventDefault();
        target.focus();
        activateTab(target.dataset.profileTab);
      }
    });
  });

  const initialTab = window.location.hash.replace("#", "");
  activateTab(validTabs.has(initialTab) ? initialTab : "profile");

  /* ── Profile picture picker ───────────────────────────────── */

  const avatarModal = root.querySelector("[data-avatar-modal]");
  const avatarOptions = [...root.querySelectorAll("[data-avatar-value]")];
  const avatarFilters = [...root.querySelectorAll("[data-avatar-filter]")];
  let selectedAvatar = AVATARS[0];

  const renderAvatarSelection = () => {
    avatarOptions.forEach((option) => {
      const selected = option.dataset.avatarValue === selectedAvatar;
      option.classList.toggle("is-selected", selected);
      option.setAttribute("aria-pressed", String(selected));
    });
  };

  const setAvatarFilter = (category) => {
    avatarFilters.forEach((filter) => {
      filter.classList.toggle("is-active", filter.dataset.avatarFilter === category);
    });
    avatarOptions.forEach((option) => {
      option.hidden = category !== "all" && option.dataset.avatarCategory !== category;
    });
  };

  avatarOptions.forEach((option) => {
    option.addEventListener("click", () => {
      selectedAvatar = option.dataset.avatarValue;
      renderAvatarSelection();
    });
  });

  avatarFilters.forEach((filter) => {
    filter.addEventListener("click", () => setAvatarFilter(filter.dataset.avatarFilter));
  });

  const applyAvatar = (src) => {
    selectedAvatar = AVATARS.includes(src) ? src : AVATARS[0];
    if (profileForm?.elements.avatar_url) {
      profileForm.elements.avatar_url.value = selectedAvatar;
    }
    const resolved = avatarSrc(selectedAvatar);
    if (profileAvatar) profileAvatar.src = resolved;
    if (avatarCurrent) avatarCurrent.src = resolved;
    if (avatarCurrentName) avatarCurrentName.textContent = avatarName(selectedAvatar);
    renderAvatarSelection();
  };

  const openAvatarModal = () => {
    selectedAvatar = AVATARS.includes(profileForm?.elements.avatar_url?.value)
      ? profileForm.elements.avatar_url.value
      : AVATARS[0];
    setAvatarFilter("all");
    renderAvatarSelection();
    if (typeof avatarModal.showModal === "function") {
      avatarModal.showModal();
    } else {
      avatarModal.setAttribute("open", "");
    }
  };

  const closeAvatarModal = (commit) => {
    if (commit) {
      applyAvatar(selectedAvatar);
    }
    if (typeof avatarModal.close === "function") {
      avatarModal.close();
    } else {
      avatarModal.removeAttribute("open");
    }
  };

  root.querySelector("[data-avatar-picker-open]")?.addEventListener("click", openAvatarModal);
  root.querySelector("[data-avatar-modal-close]")?.addEventListener("click", () => closeAvatarModal(false));
  root.querySelector("[data-avatar-modal-cancel]")?.addEventListener("click", () => closeAvatarModal(false));
  root.querySelector("[data-avatar-modal-confirm]")?.addEventListener("click", () => closeAvatarModal(true));
  avatarModal?.addEventListener("cancel", (event) => {
    event.preventDefault();
    closeAvatarModal(false);
  });

  if (!url || !key || !window.supabase?.createClient) {
    setStatus("Profile services are temporarily unavailable.", true);
    root.querySelectorAll("input, textarea, select, button:not(.profile-tab)").forEach((control) => {
      control.disabled = true;
    });
    return;
  }

  const client = window.coSupabase || window.supabase.createClient(url, key);
  window.coSupabase = client;

  const renderProfile = (user, profile) => {
    const avatar = AVATARS.includes(profile.avatar_url) ? profile.avatar_url : AVATARS[0];
    const username = profile.display_name || user.user_metadata?.display_name || "Member";

    profileName.textContent = username;
    profileEmail.textContent = user.email || "";
    applyAvatar(avatar);
    profileForm.elements.username.value = username;
    emailForm.elements.email.value = user.email || "";
    if (viewPublicProfile) {
      viewPublicProfile.href = `${assetBase}/member/?username=${encodeURIComponent(username)}`;
    }
  };

  const loadProfile = async (user) => {
    const { data: profile, error } = await client
      .from("profiles")
      .select("display_name,avatar_url")
      .eq("id", user.id)
      .single();

    if (error) {
      setStatus("Could not load your profile.", true);
      return false;
    }

    renderProfile(user, profile);
    return true;
  };

  profileForm?.addEventListener("submit", async (event) => {
    event.preventDefault();
    if (!profileForm.checkValidity() || !currentUser) {
      profileForm.reportValidity();
      return;
    }

    const username = profileForm.elements.username.value.trim();
    const avatar = profileForm.elements.avatar_url.value;
    if (!username) {
      setStatus("Enter a username.", true);
      return;
    }
    if (!AVATARS.includes(avatar)) {
      setStatus("Choose one of the available profile pictures.", true);
      return;
    }

    setStatus("Saving profile…");

    const { error: profileError } = await client
      .from("profiles")
      .update({
        display_name: username,
        avatar_url: avatar,
      })
      .eq("id", currentUser.id);

    if (profileError) {
      setStatus("Could not save your profile.", true);
      return;
    }

    profileName.textContent = username;

    const { data: authData, error: authError } = await client.auth.updateUser({
      data: {
        display_name: username,
        avatar_url: avatar,
      },
    });

    if (authError) {
      setStatus("Profile saved, but account metadata could not be updated.", true);
      return;
    }

    currentUser = authData.user || currentUser;
    applyAvatar(avatar);
    if (viewPublicProfile) {
      viewPublicProfile.href = `${assetBase}/member/?username=${encodeURIComponent(username)}`;
    }
    setStatus("Profile saved.");
  });

  emailForm?.addEventListener("submit", async (event) => {
    event.preventDefault();
    if (!emailForm.checkValidity() || !currentUser) {
      emailForm.reportValidity();
      return;
    }

    const email = emailForm.elements.email.value.trim();
    if (!email) {
      setStatus("Enter an email address.", true);
      return;
    }
    if (email.toLowerCase() === String(currentUser.email || "").toLowerCase()) {
      setStatus("That is already your email address.");
      return;
    }

    setStatus("Saving email…");
    const { data: authData, error } = await client.auth.updateUser(
      { email },
      { emailRedirectTo: accountUrl }
    );

    if (error) {
      setStatus(error.message || "Could not update your email.", true);
      return;
    }

    currentUser = authData.user || currentUser;
    profileEmail.textContent = currentUser.email || email;
    setStatus("Check your new email address to confirm the change.");
  });

  passwordForm?.addEventListener("submit", async (event) => {
    event.preventDefault();
    if (!passwordForm.checkValidity()) {
      passwordForm.reportValidity();
      return;
    }

    const currentPassword = passwordForm.elements.current_password.value;
    const newPassword = passwordForm.elements.new_password.value;
    const confirmPassword = passwordForm.elements.confirm_password.value;

    if (newPassword !== confirmPassword) {
      setStatus("The new passwords do not match.", true);
      return;
    }

    setStatus("Updating password…");
    const passwordUpdate = { password: newPassword };
    if (currentPassword) {
      passwordUpdate.currentPassword = currentPassword;
    }
    const { error } = await client.auth.updateUser(passwordUpdate);

    if (error) {
      setStatus(error.message || "Could not update your password.", true);
      return;
    }

    passwordForm.reset();
    setStatus("Password updated.");
  });

  signOut?.addEventListener("click", async () => {
    await client.auth.signOut();
    goToAccount();
  });

  client.auth.onAuthStateChange((event, session) => {
    if (event === "SIGNED_OUT" || !session) {
      goToAccount();
      return;
    }
    currentUser = session.user;
  });

  const initialize = async () => {
    const { data } = await client.auth.getSession();
    currentUser = data.session?.user || null;
    if (!currentUser) {
      goToAccount();
      return;
    }

    await loadProfile(currentUser);
  };

  initialize();
})();
