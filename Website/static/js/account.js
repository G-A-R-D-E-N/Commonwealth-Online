(() => {
  const root = document.querySelector("[data-account]");
  if (!root) {
    return;
  }

  const url = (root.dataset.supabaseUrl || "").replace(/\/+$/, "");
  const key = root.dataset.supabaseKey || "";
  const status = root.querySelector("[data-account-status]");
  const registerForm = root.querySelector("[data-register-form]");
  const signInForm = root.querySelector("[data-signin-form]");
  const discordSignIn = root.querySelector("[data-discord-sign-in]");
  const registerSubmit = registerForm?.querySelector('button[type="submit"]');
  const brandLogo = document.querySelector(".site-brand__logo");
  const assetBase = brandLogo ? new URL(brandLogo.src).pathname.split("/assets/")[0] : "";
  const accountUrl = new URL(`${assetBase}/account/`, window.location.origin).href;
  const profileUrl = new URL(`${assetBase}/profile/`, window.location.origin).href;

  const setStatus = (message, error = false) => {
    if (!status) {
      return;
    }
    status.hidden = !message;
    status.textContent = message || "";
    status.classList.toggle("is-error", error);
  };

  const disableAccount = () => {
    root.querySelectorAll("input, button").forEach((control) => {
      control.disabled = true;
    });
  };

  if (!url || !key || !window.supabase?.createClient) {
    setStatus("Account services are temporarily unavailable.", true);
    disableAccount();
    return;
  }

  const client = window.coSupabase || window.supabase.createClient(url, key);
  window.coSupabase = client;
  let discordAvailable = false;

  const goToProfile = () => {
    window.location.assign(profileUrl);
  };

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
        discordSignIn.hidden = !discordAvailable;
      }
      if (registerSubmit) {
        const emailSignupAvailable = !settings.disable_signup && settings.external?.email !== false;
        registerSubmit.disabled = !emailSignupAvailable;
        if (!emailSignupAvailable) {
          setStatus("Account registration is currently unavailable.", true);
        }
      }
    } catch {
      if (discordSignIn) {
        discordSignIn.disabled = true;
        discordSignIn.hidden = true;
      }
    }
  };

  registerForm?.addEventListener("submit", async (event) => {
    event.preventDefault();
    if (!registerForm.checkValidity()) {
      registerForm.reportValidity();
      return;
    }

    const form = new FormData(registerForm);
    const username = String(form.get("username") || "").trim();
    if (!username) {
      setStatus("Enter a username.", true);
      return;
    }

    setStatus("Creating your account…");
    const { data, error } = await client.auth.signUp({
      email: String(form.get("email") || "").trim(),
      password: String(form.get("password") || ""),
      options: {
        emailRedirectTo: accountUrl,
        data: {
          display_name: username,
        },
      },
    });

    if (error) {
      setStatus(error.message || "Could not create your account.", true);
      return;
    }

    if (data.session) {
      goToProfile();
      return;
    }

    registerForm.reset();
    setStatus("Account created. Check your email to confirm your address, then sign in.");
  });

  signInForm?.addEventListener("submit", async (event) => {
    event.preventDefault();
    if (!signInForm.checkValidity()) {
      signInForm.reportValidity();
      return;
    }

    setStatus("Signing in…");
    const form = new FormData(signInForm);
    const { data, error } = await client.auth.signInWithPassword({
      email: String(form.get("email") || "").trim(),
      password: String(form.get("password") || ""),
    });

    if (error) {
      setStatus(error.message || "Could not sign in.", true);
      return;
    }

    if (!data.session?.user) {
      setStatus("Sign-in succeeded but no session was created. Try again.", true);
      return;
    }

    goToProfile();
  });

  discordSignIn?.addEventListener("click", async () => {
    if (!discordAvailable) {
      setStatus("Discord sign-in is currently unavailable.", true);
      return;
    }

    setStatus("Opening Discord…");
    const { data, error } = await client.auth.signInWithOAuth({
      provider: "discord",
      options: {
        redirectTo: accountUrl,
        skipBrowserRedirect: true,
      },
    });

    if (error) {
      setStatus(error.message || "Could not start Discord sign-in.", true);
      return;
    }

    if (!data?.url) {
      setStatus("Discord sign-in did not return an authorization URL.", true);
      return;
    }

    window.location.assign(data.url);
  });

  const initialize = async () => {
    const { data } = await client.auth.getSession();
    if (data.session?.user) {
      goToProfile();
      return;
    }
    await loadAuthSettings();
  };

  initialize();
})();
