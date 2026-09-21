(() => {
  const root = document.querySelector("[data-faction-apply]");
  if (!root) return;

  const url = (root.dataset.supabaseUrl || "").replace(/\/+$/, "");
  const key = root.dataset.supabaseKey || "";
  const form = root.querySelector("[data-faction-apply-form]");
  const status = root.querySelector("[data-faction-apply-status]");
  const brandLogo = document.querySelector(".site-brand__logo");
  const assetBase = brandLogo ? new URL(brandLogo.src).pathname.split("/assets/")[0] : "";
  if (!url || !key || !form || !window.supabase?.createClient) return;

  const client = window.coSupabase || window.supabase.createClient(url, key);
  window.coSupabase = client;
  let user = null;
  let activeApplication = null;

  const setStatus = (message, error = false) => {
    status.hidden = !message;
    status.textContent = message || "";
    status.classList.toggle("is-error", error);
  };

  const initialize = async () => {
    const { data } = await client.auth.getSession();
    user = data.session?.user || null;

    if (!user) {
      form.hidden = true;
      setStatus("Sign in before submitting a faction application.", true);

      const link = document.createElement("a");
      link.className = "co-btn co-btn--primary";
      link.href = assetBase + "/account/";
      link.textContent = "Sign in";
      status.after(link);
      return;
    }

    const { data: existing } = await client
      .from("faction_applications")
      .select("id,status,review_note,proposed_name,proposed_tag,summary,lore,goals,focus,recruitment")
      .eq("applicant_id", user.id)
      .in("status", ["draft", "submitted", "reviewing", "changes_requested"])
      .maybeSingle();

    if (!existing) return;

    activeApplication = existing;

    if (existing.status === "changes_requested" || existing.status === "draft") {
      for (const name of ["proposed_name", "proposed_tag", "summary", "lore", "goals", "focus", "recruitment"]) {
        form.elements[name].value = existing[name] || "";
      }
      setStatus(
        existing.review_note
          ? "Staff requested changes: " + existing.review_note
          : "Continue editing your faction application."
      );
      return;
    }

    form.hidden = true;
    setStatus("You already have an active faction application (" + existing.status.replaceAll("_", " ") + ").");
  };

  form.addEventListener("submit", async (event) => {
    event.preventDefault();
    if (!user || !form.checkValidity()) return;

    const submit = form.querySelector('button[type="submit"]');
    submit.disabled = true;

    const payload = {
      applicant_id: user.id,
      proposed_name: form.elements.proposed_name.value.trim(),
      proposed_tag: form.elements.proposed_tag.value.trim().toUpperCase(),
      summary: form.elements.summary.value.trim(),
      lore: form.elements.lore.value.trim(),
      goals: form.elements.goals.value.trim(),
      focus: form.elements.focus.value,
      recruitment: form.elements.recruitment.value,
      status: "submitted",
    };

    const query = activeApplication
      ? client
          .from("faction_applications")
          .update(payload)
          .eq("id", activeApplication.id)
      : client
          .from("faction_applications")
          .insert(payload);

    const { error } = await query;

    if (error) {
      submit.disabled = false;
      const duplicate = error.code === "23505";
      setStatus(
        duplicate
          ? "You already have an active faction application."
          : "Could not submit the faction application.",
        true
      );
      return;
    }

    activeApplication = null;
    form.reset();
    form.hidden = true;
    setStatus("Faction application submitted for review.");
  });

  initialize();
})();
