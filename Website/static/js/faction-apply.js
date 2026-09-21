(() => {
  const root = document.querySelector("[data-faction-apply]");
  if (!root) return;

  const url = (root.dataset.supabaseUrl || "").replace(/\/+$/, "");
  const key = root.dataset.supabaseKey || "";
  const form = root.querySelector("[data-faction-apply-form]");
  const status = root.querySelector("[data-faction-apply-status]");
  const state = root.querySelector("[data-faction-application-state]");
  const badge = root.querySelector("[data-faction-application-badge]");
  const stateTitle = root.querySelector("[data-faction-application-title]");
  const stateCopy = root.querySelector("[data-faction-application-copy]");
  const stateDate = root.querySelector("[data-faction-application-date]");
  const reviewNoteWrap = root.querySelector("[data-faction-application-review-note-wrap]");
  const reviewNote = root.querySelector("[data-faction-application-review-note]");
  const stateActions = root.querySelector("[data-faction-application-actions]");
  const history = root.querySelector("[data-faction-application-history]");
  const historyList = root.querySelector("[data-faction-application-history-list]");
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

  const stateLabels = {
    draft: ["Draft", "Application draft", "Finish the application below and submit it for review."],
    submitted: ["Submitted", "Application submitted", "Your faction application is waiting for administrator review."],
    reviewing: ["In review", "Application under review", "An administrator is currently reviewing your faction application."],
    changes_requested: ["Changes needed", "Changes requested", "Update the application below, then resubmit it for review."],
    approved: ["Approved", "Faction approved", "Your faction application was approved and the faction has been created."],
    rejected: ["Denied", "Application denied", "This faction application was not approved."],
  };

  const clearStateActions = () => {
    stateActions?.replaceChildren();
  };

  const addStateLink = (label, href, primary = false) => {
    if (!stateActions) return;
    const link = document.createElement("a");
    link.className = primary ? "co-btn co-btn--primary" : "co-btn co-btn--ghost";
    link.href = href;
    link.textContent = label;
    stateActions.append(link);
  };

  const addNewApplicationButton = () => {
    if (!stateActions) return;
    const button = document.createElement("button");
    button.className = "co-btn co-btn--primary";
    button.type = "button";
    button.textContent = "Start a new application";
    button.addEventListener("click", () => {
      activeApplication = null;
      form.reset();
      form.hidden = false;
      state.hidden = true;
      setStatus("");
      form.querySelector("input, textarea, select")?.focus();
    });
    stateActions.append(button);
  };

  const renderState = (application) => {
    if (!state || !application) {
      if (state) state.hidden = true;
      return;
    }

    const [label, title, copy] = stateLabels[application.status] || [
      "Application",
      "Application status",
      "Your faction application status has been updated.",
    ];

    badge.textContent = label;
    stateTitle.textContent = title;
    stateCopy.textContent = copy;
    stateDate.textContent = application.reviewed_at || application.updated_at || application.created_at
      ? "Updated " + new Date(application.reviewed_at || application.updated_at || application.created_at).toLocaleString()
      : "";

    const note = String(application.review_note || "").trim();
    reviewNoteWrap.hidden = !note;
    reviewNote.textContent = note;

    clearStateActions();

    if (application.status === "approved") {
      addStateLink("Browse factions", assetBase + "/factions/", true);
      addStateLink("View profile", assetBase + "/profile/");
    } else if (application.status === "rejected") {
      addNewApplicationButton();
    }

    state.hidden = false;
  };

  const fillForm = (application) => {
    for (const name of ["proposed_name", "proposed_tag", "summary", "lore", "goals", "focus", "recruitment"]) {
      form.elements[name].value = application[name] || "";
    }
  };

  const renderHistory = (applications) => {
    if (!history || !historyList) return;

    historyList.replaceChildren();
    for (const application of applications) {
      const item = document.createElement("article");
      item.className = "faction-history-item";

      const head = document.createElement("div");
      head.className = "faction-history-item__head";
      const title = document.createElement("strong");
      const badge = document.createElement("span");
      title.textContent = application.proposed_name + " [" + application.proposed_tag + "]";
      badge.className = "faction-status";
      badge.textContent = String(application.status || "").replaceAll("_", " ");
      head.append(title, badge);

      const date = document.createElement("p");
      date.className = "faction-history-item__date";
      date.textContent = new Date(application.created_at).toLocaleString();

      item.append(head, date);

      const note = String(application.review_note || "").trim();
      if (note) {
        const review = document.createElement("p");
        review.className = "faction-history-item__note";
        review.textContent = "Review note: " + note;
        item.append(review);
      }

      historyList.append(item);
    }

    history.hidden = applications.length === 0;
  };

  const loadApplication = async () => {
    const { data: applications, error } = await client
      .from("faction_applications")
      .select("id,status,review_note,reviewed_at,created_at,updated_at,proposed_name,proposed_tag,summary,lore,goals,focus,recruitment")
      .eq("applicant_id", user.id)
      .order("created_at", { ascending: false });

    if (error) {
      setStatus("Could not load your faction application.", true);
      return;
    }

    const rows = applications || [];
    const existing = rows[0] || null;
    const openStatuses = new Set(["draft", "submitted", "reviewing", "changes_requested"]);
    renderHistory(existing && openStatuses.has(existing.status) ? rows.slice(1) : rows);

    if (!existing) {
      activeApplication = null;
      state.hidden = true;
      form.hidden = false;
      return;
    }

    renderState(existing);

    if (existing.status === "draft" || existing.status === "changes_requested") {
      activeApplication = existing;
      fillForm(existing);
      form.hidden = false;
      return;
    }

    activeApplication = null;
    form.hidden = true;
  };

  const initialize = async () => {
    const { data } = await client.auth.getSession();
    user = data.session?.user || null;

    if (!user) {
      form.hidden = true;
      if (state) state.hidden = true;
      setStatus("Sign in before submitting a faction application.", true);

      const link = document.createElement("a");
      link.className = "co-btn co-btn--primary";
      link.href = assetBase + "/account/";
      link.textContent = "Sign in";
      status.after(link);
      return;
    }

    await loadApplication();
  };

  form.addEventListener("submit", async (event) => {
    event.preventDefault();
    if (!user || !form.checkValidity()) return;

    const submit = form.querySelector('button[type="submit"]');
    submit.disabled = true;

    const payload = {
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
          .insert({ applicant_id: user.id, ...payload });

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

    form.reset();
    setStatus("");
    await loadApplication();
    submit.disabled = false;
  });

  initialize();
})();
