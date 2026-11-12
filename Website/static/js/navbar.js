(() => {
  // The header markup is rendered server-side (views/partials/navbar.ejs), so
  // this script only wires up the mobile menu toggle.
  const mount = document.querySelector("[data-co-navbar]");
  if (!mount) {
    return;
  }

  const toggle = mount.querySelector(".site-nav-toggle");
  const nav = mount.querySelector(".site-nav");

  if (!toggle || !nav) {
    return;
  }

  const setOpen = (open) => {
    toggle.setAttribute("aria-expanded", String(open));
    toggle.setAttribute("aria-label", open ? "Close navigation" : "Open navigation");
    nav.classList.toggle("is-open", open);
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
})();
