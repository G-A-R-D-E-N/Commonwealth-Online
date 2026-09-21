(() => {
  const root = document.querySelector("[data-faction-review]");
  if (!root) return;

  const url = (root.dataset.supabaseUrl || "").replace(/\/+$/, "");
  const key = root.dataset.supabaseKey || "";
  const status = root.querySelector("[data-faction-review-status]");
  const toolbar = root.querySelector("[data-faction-review-toolbar]");
  const filter = root.querySelector("[data-faction-review-filter]");
  const list = root.querySelector("[data-faction-review-list]");
  const brandLogo = document.querySelector(".site-brand__logo");
  const assetBase = brandLogo ? new URL(brandLogo.src).pathname.split("/assets/")[0] : "";
  if (!url || !key || !status || !list || !window.supabase?.createClient) return;

  const client = window.coSupabase || window.supabase.createClient(url, key);
  window.coSupabase = client;

  const setStatus = (message, error = false) => {
    status.hidden = !message;
    status.textContent = message || "";
    status.classList.toggle("is-error", error);
  };

  const label = (value) =>
    String(value || "")
      .replaceAll("_", " ")
      .replace(/\b\w/g, (character) => character.toUpperCase());

  const profileHref = (username) =>
    assetBase + "/member/?username=" + encodeURIComponent(username);

  const review = async (application, decision, note) => {
    const { data, error } = await client.rpc("review_faction_application", {
      p_application_id: application.id,
      p_decision: decision,
      p_review_note: note || null,
    });

    if (error) {
      setStatus(error.message || "Could not update the faction application.", true);
      return false;
    }

    if (decision === "approved" && data) {
      const { data: createdFaction } = await client
        .from("factions")
        .select("slug")
        .eq("id", data)
        .maybeSingle();
      window.location.assign(
        createdFaction?.slug
          ? assetBase + "/faction/?slug=" + encodeURIComponent(createdFaction.slug)
          : assetBase + "/factions/"
      );
      return true;
    }

    setStatus("Faction application updated.");
    await loadApplications();
    return true;
  };

  const createButton = (labelText, className, handler) => {
    const button = document.createElement("button");
    button.className = className;
    button.type = "button";
    button.textContent = labelText;
    button.addEventListener("click", async () => {
      const controls = button.closest(".faction-review-actions")?.querySelectorAll("button") || [button];
      controls.forEach((control) => {
        control.disabled = true;
      });

      try {
        const ok = await handler();
        if (!ok) {
          controls.forEach((control) => {
            control.disabled = false;
          });
        }
      } catch {
        controls.forEach((control) => {
          control.disabled = false;
        });
        setStatus("Could not update the faction application.", true);
      }
    });
    return button;
  };

  const appendReviewNote = (facts, application) => {
    if (!application.review_note) return;

    const row = document.createElement("div");
    const term = document.createElement("dt");
    const detail = document.createElement("dd");
    term.textContent = "Review note";
    detail.textContent = application.review_note;
    row.append(term, detail);
    facts.append(row);
  };

  const renderApplication = (application) => {
    const card = document.createElement("article");
    card.className = "profile-card faction-review-card";

    const head = document.createElement("div");
    head.className = "faction-review-card__head";

    const identity = document.createElement("div");
    const kicker = document.createElement("p");
    kicker.className = "section-kicker";
    kicker.textContent = label(application.status);

    const title = document.createElement("h2");
    title.textContent = application.proposed_name + " [" + application.proposed_tag + "]";

    const applicantName = application.applicant?.display_name || "Applicant";
    const applicant = document.createElement(application.applicant?.display_name ? "a" : "span");
    applicant.className = "faction-review-applicant";
    applicant.textContent = applicantName;
    if (application.applicant?.display_name) {
      applicant.href = profileHref(application.applicant.display_name);
    }

    identity.append(kicker, title, applicant);

    const submitted = document.createElement("span");
    submitted.className = "faction-status";
    submitted.textContent = new Date(application.created_at).toLocaleDateString();
    head.append(identity, submitted);

    const facts = document.createElement("dl");
    facts.className = "faction-review-facts";
    for (const [name, value] of [
      ["Focus", label(application.focus)],
      ["Recruitment", label(application.recruitment)],
      ["Summary", application.summary],
      ["Lore", application.lore],
      ["Goals", application.goals],
    ]) {
      const row = document.createElement("div");
      const term = document.createElement("dt");
      const detail = document.createElement("dd");
      term.textContent = name;
      detail.textContent = value;
      row.append(term, detail);
      facts.append(row);
    }

    const actionable =
      application.status === "submitted" || application.status === "reviewing";

    if (!actionable) {
      appendReviewNote(facts, application);
      card.append(head, facts);

      if (application.status === "changes_requested") {
        const waiting = document.createElement("p");
        waiting.className = "account-help";
        waiting.textContent = "Waiting for the applicant to resubmit changes.";
        card.append(waiting);
      } else if (application.status === "draft") {
        const waiting = document.createElement("p");
        waiting.className = "account-help";
        waiting.textContent = "The applicant has not submitted this application.";
        card.append(waiting);
      }

      return card;
    }

    const noteField = document.createElement("label");
    noteField.className = "profile-field";
    const noteLabel = document.createElement("span");
    noteLabel.textContent = "Review note";
    const note = document.createElement("textarea");
    note.rows = 3;
    note.maxLength = 2000;
    note.placeholder = "Required when requesting changes or rejecting.";
    note.value = application.review_note || "";
    noteField.append(noteLabel, note);

    const actions = document.createElement("div");
    actions.className = "profile-actions faction-review-actions";

    if (application.status === "submitted") {
      actions.append(
        createButton("Start review", "co-btn co-btn--ghost", () =>
          review(application, "reviewing", note.value.trim())
        )
      );
    }

    actions.append(
      createButton("Approve", "co-btn co-btn--primary", () => {
        if (!window.confirm("Approve this faction and create it?")) return false;
        return review(application, "approved", note.value.trim());
      }),
      createButton("Request changes", "co-btn co-btn--ghost", async () => {
        const message = note.value.trim();
        if (!message) {
          setStatus("Enter a review note before requesting changes.", true);
          return false;
        }
        return review(application, "changes_requested", message);
      }),
      createButton("Reject", "co-btn co-btn--ghost", async () => {
        const message = note.value.trim();
        if (!message) {
          setStatus("Enter a review note before rejecting an application.", true);
          return false;
        }
        if (!window.confirm("Reject this faction application?")) return false;
        return review(application, "rejected", message);
      })
    );

    card.append(head, facts, noteField, actions);
    return card;
  };

  const loadApplications = async () => {
    let query = client
      .from("faction_applications")
      .select(
        "id,applicant_id,proposed_name,proposed_tag,summary,lore,goals,focus,recruitment,status,review_note,created_at,applicant:profiles!faction_applications_applicant_id_fkey(id,display_name,avatar_url)"
      )
      .order("created_at", { ascending: true });

    const selected = filter?.value || "open";
    if (selected === "open") {
      query = query.in("status", ["submitted", "reviewing", "changes_requested"]);
    } else if (selected === "closed") {
      query = query.in("status", ["approved", "rejected"]);
    } else if (selected !== "all") {
      query = query.eq("status", selected);
    }

    const { data, error } = await query;
    list.replaceChildren();

    if (error) {
      setStatus("Faction applications are temporarily unavailable.", true);
      list.hidden = true;
      return;
    }

    list.hidden = false;

    if (!data?.length) {
      list.textContent =
        selected === "closed"
          ? "No closed faction applications."
          : selected === "all"
            ? "No faction applications."
            : "No faction applications in this view.";
      return;
    }

    for (const application of data) list.append(renderApplication(application));
  };

  const initialize = async () => {
    const { data: sessionData } = await client.auth.getSession();
    const user = sessionData.session?.user || null;
    if (!user) {
      setStatus("Sign in with an administrator account to review faction applications.", true);
      return;
    }

    const { data: profile, error } = await client
      .from("profiles")
      .select("role")
      .eq("id", user.id)
      .maybeSingle();

    if (error || profile?.role !== "admin") {
      setStatus("Administrator access is required.", true);
      return;
    }

    setStatus("");
    if (toolbar) toolbar.hidden = false;
    await loadApplications();
  };

  filter?.addEventListener("change", loadApplications);
  initialize();
})();
