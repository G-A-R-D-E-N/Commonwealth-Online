(() => {
  const root = document.querySelector("[data-account]");
  if (!root) {
    return;
  }

  const SIGNUP_COOLDOWN_MS = 15000;
  const SIGNUP_COOLDOWN_KEY = "co-signup-last-at";
  const url = (root.dataset.supabaseUrl || "").replace(/\/+$/, "");
  const key = root.dataset.supabaseKey || "";
  const captchaProvider = (root.dataset.captchaProvider || "").toLowerCase();
  const captchaSiteKey = root.dataset.captchaSiteKey || "";
  const status = root.querySelector("[data-account-status]");
  const registerForm = root.querySelector("[data-register-form]");
  const signInForm = root.querySelector("[data-signin-form]");
  const discordSignIn = root.querySelector("[data-discord-sign-in]");
  const registerSubmit = registerForm?.querySelector('button[type="submit"]');
  const captchaWidget = registerForm?.querySelector("[data-captcha-widget]");
  const brandLogo = document.querySelector(".site-brand__logo");
  const assetBase = brandLogo ? new URL(brandLogo.src).pathname.split("/assets/")[0] : "";
  const accountUrl = new URL(`${assetBase}/account/`, window.location.origin).href;
  const profileUrl = new URL(`${assetBase}/profile/`, window.location.origin).href;
  const registerUrl = `${url}/functions/v1/register-account`;

  let emailSignupAvailable = true;
  let registerPending = false;
  let captchaToken = "";
  let captchaWidgetId = null;

  const captchaEnabled =
    captchaProvider === "turnstile" &&
    Boolean(captchaSiteKey) &&
    Boolean(captchaWidget);

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

  const syncRegisterState = () => {
    if (registerSubmit) {
      registerSubmit.disabled = registerPending || !emailSignupAvailable;
    }
  };

  const goToProfile = () => {
    window.location.assign(profileUrl);
  };

  const resetCaptcha = () => {
    captchaToken = "";
    if (captchaWidgetId !== null && window.turnstile?.reset) {
      window.turnstile.reset(captchaWidgetId);
    }
  };

  const renderCaptcha = () => {
    if (!captchaEnabled || captchaWidgetId !== null || !window.turnstile?.render) {
      return;
    }

    captchaWidgetId = window.turnstile.render(captchaWidget, {
      sitekey: captchaSiteKey,
      theme: "dark",
      callback(token) {
        captchaToken = token;
      },
      "expired-callback"() {
        captchaToken = "";
      },
      "error-callback"() {
        captchaToken = "";
        setStatus("Anti-bot verification failed. Please try again.", true);
      },
    });
  };

  window.coTurnstileReady = renderCaptcha;

  if (!url || !key || !window.supabase?.createClient) {
    setStatus("Account services are temporarily unavailable.", true);
    disableAccount();
    return;
  }

  const client = window.coSupabase || window.supabase.createClient(url, key);
  window.coSupabase = client;

  registerForm?.addEventListener("submit", async (event) => {
    event.preventDefault();
    if (registerPending || !emailSignupAvailable) {
      return;
    }
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

    if (captchaEnabled && !captchaToken) {
      setStatus("Complete the anti-bot verification.", true);
      return;
    }

    const lastAttempt = Number(window.localStorage?.getItem(SIGNUP_COOLDOWN_KEY) || 0);
    const remaining = SIGNUP_COOLDOWN_MS - (Date.now() - lastAttempt);
    if (remaining > 0) {
      setStatus("Please wait a few seconds before trying registration again.", true);
      return;
    }

    window.localStorage?.setItem(SIGNUP_COOLDOWN_KEY, String(Date.now()));
    registerPending = true;
    syncRegisterState();
    setStatus("Creating your account…");

    let response;
    try {
      response = await fetch(registerUrl, {
        method: "POST",
        headers: {
          apikey: key,
          Authorization: `Bearer ${key}`,
          "Content-Type": "application/json",
        },
        body: JSON.stringify({
          email: String(form.get("email") || "").trim(),
          password: String(form.get("password") || ""),
          username,
          website: String(form.get("website") || ""),
          captchaToken,
          redirectTo: accountUrl,
        }),
      });
    } catch {
      registerPending = false;
      syncRegisterState();
      resetCaptcha();
      setStatus("Registration service is temporarily unavailable.", true);
      return;
    }

    const payload = await response.json().catch(() => ({}));
    if (!response.ok) {
      registerPending = false;
      syncRegisterState();
      resetCaptcha();

      if (response.status === 429) {
        setStatus(
          payload.error?.message || "Too many registration attempts. Please wait and try again.",
          true
        );
        return;
      }

      setStatus(payload.error?.message || "Could not create your account.", true);
      return;
    }

    if (payload.session?.access_token && payload.session?.refresh_token) {
      const { error } = await client.auth.setSession(payload.session);
      if (error) {
        registerPending = false;
        syncRegisterState();
        setStatus("Account created, but sign-in could not be completed.", true);
        return;
      }
      goToProfile();
      return;
    }

    registerPending = false;
    emailSignupAvailable = false;
    syncRegisterState();
    registerForm.reset();
    resetCaptcha();
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

  const initialize = async () => {
    const { data } = await client.auth.getSession();
    if (data.session?.user) {
      goToProfile();
      return;
    }

    renderCaptcha();
  };

  initialize();
})();
