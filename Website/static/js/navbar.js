(() => {
  const mount = document.querySelector("[data-co-navbar]");
  if (!mount) {
    return;
  }

  const toggle = mount.querySelector(".site-nav-toggle");
  const nav = mount.querySelector(".site-nav");
  const accountLink = mount.querySelector("[data-account-nav]");

  if (!toggle || !nav) {
    return;
  }

  const setOpen = (open) => {
    toggle.setAttribute("aria-expanded", String(open));
    toggle.setAttribute("aria-label", open ? "Close navigation" : "Open navigation");
    nav.classList.toggle("is-open", open);
  };

  const setAccountLabel = (signedIn) => {
    if (accountLink) {
      accountLink.textContent = signedIn ? "Account" : "Login / Sign Up";
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
    if (event.key !== "Escape" || toggle.getAttribute("aria-expanded") !== "true") {
      return;
    }
    setOpen(false);
  });

  window.matchMedia("(min-width: 761px)").addEventListener("change", (event) => {
    if (event.matches) {
      setOpen(false);
    }
  });

  const url = (mount.dataset.supabaseUrl || "").replace(/\/+$/, "");
  const key = mount.dataset.supabaseKey || "";
  if (!accountLink || !url || !key || !window.supabase?.createClient) {
    setAccountLabel(false);
    return;
  }

  const client = window.supabase.createClient(url, key);
  client.auth.getSession().then(({ data }) => {
    setAccountLabel(Boolean(data.session));
  });
  client.auth.onAuthStateChange((_event, session) => {
    setAccountLabel(Boolean(session));
  });
})();
