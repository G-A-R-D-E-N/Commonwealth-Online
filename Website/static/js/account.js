(() => {
  const root = document.querySelector("[data-account]");
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
  const status = root.querySelector("[data-account-status]");
  const signedOut = root.querySelector("[data-account-signed-out]");
  const signedIn = root.querySelector("[data-account-signed-in]");
  const registerForm = root.querySelector("[data-register-form]");
  const signInForm = root.querySelector("[data-signin-form]");
  const profileForm = root.querySelector("[data-profile-form]");
  const profileAvatar = root.querySelector("[data-profile-avatar]");
  const profileName = root.querySelector("[data-profile-name]");
  const profileEmail = root.querySelector("[data-profile-email]");
  const profileRole = root.querySelector("[data-profile-role]");
  const discordState = root.querySelector("[data-discord-state]");
  const discordLink = root.querySelector("[data-discord-link]");
  const discordSignIn = root.querySelector("[data-discord-sign-in]");
  const registerSubmit = registerForm?.querySelector('button[type="submit"]');
  const brandLogo = document.querySelector(".site-brand__logo");
  const assetBase = brandLogo ? new URL(brandLogo.src).pathname.split("/assets/")[0] : "";
  const redirectTo = new URL(`${assetBase}/account/`, window.location.origin).href;

  const setStatus = (message, error = false) => {
    if (!status) {
      return;
    }
    status.hidden = !message;
    status.textContent = message || "";
    status.classList.toggle("is-error", error);
  };

  const avatarSrc = (path) => `${assetBase}${AVATARS.includes(path) ? path : AVATARS[0]}`;
  const chosenAvatar = (form) => form?.querySelector('input[name="avatar_url"]:checked')?.value || AVATARS[0];

  if (!url || !key || !window.supabase?.createClient) {
    setStatus("Account services are temporarily unavailable.", true);
    root.querySelectorAll("input, button").forEach((control) => {
      control.disabled = true;
    });
    return;
  }

  const client = window.coSupabase || window.supabase.createClient(url, key);
  window.coSupabase = client;
  let discordAvailable = false;

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
      if (discordSignIn) {
        discordSignIn.disabled = !discordAvailable;
        discordSignIn.textContent = discordAvailable ? "Continue with Discord" : "Discord sign-in unavailable";
      }
      if (discordLink && !signedIn.hidden) {
        discordLink.disabled = !discordAvailable;
      }
      if (registerSubmit) {
        const emailSignupAvailable = !settings.disable_signup && settings.external?.email !== false;
        registerSubmit.disabled = !emailSignupAvailable;
        if (!emailSignupAvailable) {
          setStatus("Email registration is currently disabled.", true);
        }
      }
    } catch {
      if (discordSignIn) {
        discordSignIn.disabled = true;
      }
    }
  };

  const loadProfile = async (user) => {
    const { data: profile, error } = await client
      .from("profiles")
      .select("display_name,avatar_url,role")
      .eq("id", user.id)
      .single();

    if (error) {
      setStatus("Could not load your profile.", true);
      return;
    }

    const avatar = AVATARS.includes(profile.avatar_url) ? profile.avatar_url : AVATARS[0];
    signedOut.hidden = true;
    signedIn.hidden = false;
    profileName.textContent = profile.display_name;
    profileEmail.textContent = user.email || "";
    profileRole.textContent = profile.role || "member";
    profileAvatar.src = avatarSrc(avatar);
    profileForm.elements.display_name.value = profile.display_name;
    profileForm.querySelectorAll('input[name="avatar_url"]').forEach((radio) => {
      radio.checked = radio.value === avatar;
    });

    const { data: identityData } = await client.auth.getUserIdentities();
    const linked = Boolean(identityData?.identities?.some((identity) => identity.provider === "discord"));
    discordState.textContent = linked ? "Linked" : discordAvailable ? "Not linked" : "Available after Discord is configured";
    discordLink.hidden = linked;
    discordLink.disabled = !discordAvailable;
  };

  const refresh = async () => {
    const { data } = await client.auth.getSession();
    const user = data.session?.user;
    if (!user) {
      signedOut.hidden = false;
      signedIn.hidden = true;
      return;
    }
    await loadProfile(user);
  };

  registerForm?.addEventListener("submit", async (event) => {
    event.preventDefault();
    if (!registerForm.checkValidity()) {
      registerForm.reportValidity();
      return;
    }

    setStatus("Creating account…");
    const form = new FormData(registerForm);
    const avatar = chosenAvatar(registerForm);
    const { data, error } = await client.auth.signUp({
      email: String(form.get("email") || "").trim(),
      password: String(form.get("password") || ""),
      options: {
        emailRedirectTo: redirectTo,
        data: {
          display_name: String(form.get("display_name") || "").trim(),
          avatar_url: avatar,
        },
      },
    });

    if (error) {
      setStatus(error.message || "Could not create account.", true);
      return;
    }

    setStatus(data.session ? "Account created." : "Account created. Check your email to confirm it.");
    await refresh();
  });

  signInForm?.addEventListener("submit", async (event) => {
    event.preventDefault();
    if (!signInForm.checkValidity()) {
      signInForm.reportValidity();
      return;
    }

    setStatus("Signing in…");
    const form = new FormData(signInForm);
    const { error } = await client.auth.signInWithPassword({
      email: String(form.get("email") || "").trim(),
      password: String(form.get("password") || ""),
    });

    if (error) {
      setStatus(error.message || "Could not sign in.", true);
      return;
    }

    setStatus("");
    await refresh();
  });

  discordSignIn?.addEventListener("click", async () => {
    if (!discordAvailable) {
      return;
    }
    const { error } = await client.auth.signInWithOAuth({
      provider: "discord",
      options: { redirectTo },
    });
    if (error) {
      setStatus(error.message || "Could not start Discord sign-in.", true);
    }
  });

  discordLink?.addEventListener("click", async () => {
    if (!discordAvailable) {
      return;
    }
    const { error } = await client.auth.linkIdentity({
      provider: "discord",
      options: { redirectTo },
    });
    if (error) {
      setStatus(error.message || "Could not link Discord.", true);
    }
  });

  profileForm?.addEventListener("submit", async (event) => {
    event.preventDefault();
    if (!profileForm.checkValidity()) {
      profileForm.reportValidity();
      return;
    }

    const { data } = await client.auth.getUser();
    if (!data.user) {
      setStatus("Your session expired. Sign in again.", true);
      await refresh();
      return;
    }

    const displayName = profileForm.elements.display_name.value.trim();
    const avatar = chosenAvatar(profileForm);
    if (!AVATARS.includes(avatar)) {
      setStatus("Choose one of the built-in profile icons.", true);
      return;
    }

    const { error } = await client
      .from("profiles")
      .update({ display_name: displayName, avatar_url: avatar })
      .eq("id", data.user.id);

    if (error) {
      setStatus(error.message || "Could not save profile.", true);
      return;
    }

    setStatus("Profile saved.");
    await loadProfile(data.user);
  });

  root.querySelector("[data-sign-out]")?.addEventListener("click", async () => {
    await client.auth.signOut();
    setStatus("");
    await refresh();
  });

  client.auth.onAuthStateChange(() => {
    window.setTimeout(refresh, 0);
  });

  const initialize = async () => {
    await loadAuthSettings();
    await refresh();
  };

  initialize();
})();
