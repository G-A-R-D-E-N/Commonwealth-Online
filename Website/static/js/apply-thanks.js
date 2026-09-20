(() => {
  const reference = new URLSearchParams(window.location.search).get("ref")?.trim();
  const target = document.querySelector("[data-application-reference]");
  if (!reference || !target) {
    return;
  }

  target.textContent = reference;
  target.closest("[data-application-reference-wrap]")?.removeAttribute("hidden");
})();
