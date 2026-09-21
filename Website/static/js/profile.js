(() => {
  const root = document.querySelector("[data-profile]");
  if (!root) {
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

  const url = (root.dataset.supabaseUrl || "").replace(/\/+$/, "");
  const key = root.dataset.supabaseKey || "";
  const status = root.querySelector("[data-profile-status]");
  const profileForm = root.querySelector("[data-profile-form]");
  const passwordForm = root.querySelector("[data-password-form]");
  const profileAvatar = root.querySelector("[data-profile-avatar]");
  const profileName = root.querySelector("[data-profile-name]");
  const profileEmail = root.querySelector("[data-profile-email]");
  const discordState = root.querySelector("[data-discord-state]");
  const discordLink = root.querySelector("[data-discord-link]");
  const signOut = root.querySelector("[data-sign-out]");
  const brandLogo = document.querySelector(".site-brand__logo");
  const assetBase = brandLogo ? new URL(brandLogo.src).pathname.split("/assets/")[0] : "";
  const accountUrl = new URL(`${assetBase}/account/`, window.location.origin).href;

  let currentUser = null;
  let discordAvailable = false;

  const setStatus = (message, error = false) => {
    status.hidden = !message;
    status.textContent = message || "";
    status.classList.toggle("is-error", error);
  };

  const avatarSrc = (path) => `${assetBase}${AVATARS.includes(path) ? path : AVATARS[0]}`;
  const chosenAvatar = () =>
    profileForm?.querySelector('input[name="avatar_url"]:checked')?.value || AVATARS[0];

  const goToAccount = () => {
    window.location.assign(accountUrl);
  };

  if (!url || !key || !window.supabase?.createClient) {
    setStatus("Profile services are temporarily unavailable.", true);
    root.querySelectorAll("input, button").forEach((control) => {
      control.disabled = true;
    });
    return;
  }

  const client = window.coSupabase || window.supabase.createClient(url, key);
  window.coSupabase = client;

  const loadAuthSettings = async () => {
    try {
      const response = await fetch(`${url}/auth/v1/settings`, {
        headers: { apikey: key },
      });
      if (!response.ok) {
        return;
      }
      const settings = await response.json();
      discordAvailable = Boolean(settings.external?.discord);
    } catch {
      discordAvailable = false;
    }
  };

  const renderProfile = (user, profile) => {
    const avatar = AVATARS.includes(profile.avatar_url) ? profile.avatar_url : AVATARS[0];
    const username = profile.display_name || user.user_metadata?.display_name || "Member";

    profileName.textContent = username;
    profileEmail.textContent = user.email || "";
    profileAvatar.src = avatarSrc(avatar);
    profileForm.elements.username.value = username;
    profileForm.elements.email.value = user.email || "";
    profileForm.querySelectorAll('input[name="avatar_url"]').forEach((radio) => {
      radio.checked = radio.value === avatar;
    });
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

  const loadDiscord = async () => {
    if (!discordAvailable) {
      discordState.textContent = "Unavailable";
      discordLink.hidden = true;
      discordLink.disabled = true;
      return;
    }

    const { data, error } = await client.auth.getUserIdentities();
    if (error) {
      discordState.textContent = "Could not check connection";
      discordLink.hidden = true;
      discordLink.disabled = true;
      return;
    }

    const linked = Boolean(data?.identities?.some((identity) => identity.provider === "discord"));
    discordState.textContent = linked ? "Connected" : "Not connected";
    discordLink.hidden = linked;
    discordLink.disabled = linked;
  };

  profileForm?.addEventListener("submit", async (event) => {
    event.preventDefault();
    if (!profileForm.checkValidity() || !currentUser) {
      profileForm.reportValidity();
      return;
    }

    const username = profileForm.elements.username.value.trim();
    const email = profileForm.elements.email.value.trim();
    const avatar = chosenAvatar();
    if (!username) {
      setStatus("Enter a username.", true);
      return;
    }
    if (!AVATARS.includes(avatar)) {
      setStatus("Choose one of the available profile pictures.", true);
      return;
    }

    setStatus("Saving profile…");
    const emailChanged = email.toLowerCase() !== String(currentUser.email || "").toLowerCase();

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
    profileAvatar.src = avatarSrc(avatar);

    const authUpdates = {
      data: {
        display_name: username,
        avatar_url: avatar,
      },
    };
    if (emailChanged) {
      authUpdates.email = email;
    }

    const { data: authData, error: authError } = await client.auth.updateUser(
      authUpdates,
      emailChanged ? { emailRedirectTo: accountUrl } : undefined
    );

    if (authError) {
      setStatus(
        emailChanged
          ? "Profile saved, but the email change could not be started."
          : "Profile saved, but account metadata could not be updated.",
        true
      );
      return;
    }

    currentUser = authData.user || currentUser;
    profileEmail.textContent = currentUser.email || email;

    setStatus(
      emailChanged
        ? "Profile saved. Check your new email address to confirm the change."
        : "Profile saved."
    );
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

  discordLink?.addEventListener("click", async () => {
    if (!discordAvailable) {
      return;
    }

    const { error } = await client.auth.linkIdentity({
      provider: "discord",
      options: { redirectTo: accountUrl },
    });
    if (error) {
      setStatus(error.message || "Could not link Discord.", true);
    }
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

    const loaded = await loadProfile(currentUser);
    if (!loaded) {
      return;
    }

    await loadAuthSettings();
    await loadDiscord();
  };

  initialize();
})();
