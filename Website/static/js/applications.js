(() => {
  const form = document.querySelector("[data-apply-form]");
  if (!form) {
    return;
  }

  const statusEl = document.getElementById("apply-status");
  const submitBtn = document.getElementById("apply-submit");
  const discordInput = form.elements.namedItem("discordHandle");
  const discordStatus = document.getElementById("discord-membership-status");
  const discordMessage = document.getElementById("discord-membership-message");
  const discordJoin = discordStatus?.querySelector(".apply-discord-status__join");
  const type = form.getAttribute("data-apply-type") || "team";
  let discordVerified = false;
  let membershipRequest = 0;
  let membershipTimer;

  const setStatus = (message, isError = false) => {
    if (!statusEl) {
      return;
    }
    statusEl.hidden = !message;
    statusEl.textContent = message || "";
    statusEl.classList.toggle("is-error", Boolean(isError));
  };

  const clearFieldErrors = () => {
    form.querySelectorAll(".apply-field.is-invalid").forEach((field) => {
      field.classList.remove("is-invalid");
    });
    form.querySelectorAll(".apply-field__error[data-client-error]").forEach((node) => node.remove());
    form.querySelectorAll("[aria-invalid='true']").forEach((input) => {
      input.setAttribute("aria-invalid", "false");
    });
  };

  const showFieldError = (name, message) => {
    const input = form.elements.namedItem(name);
    if (!input || typeof input === "radio") {
      return;
    }
    const field = input.closest(".apply-field");
    if (!field) {
      return;
    }
    field.classList.add("is-invalid");
    input.setAttribute("aria-invalid", "true");
    let error = field.querySelector(".apply-field__error");
    if (!error) {
      error = document.createElement("p");
      error.className = "apply-field__error";
      error.dataset.clientError = "true";
      field.append(error);
    }
    error.textContent = message;
    error.id = error.id || `field-${name}-error`;
    input.setAttribute("aria-describedby", [input.getAttribute("aria-describedby"), error.id].filter(Boolean).join(" "));
  };

  const collectPayload = () => {
    const data = { type, source: "web" };
    const formData = new FormData(form);

    for (const [key, value] of formData.entries()) {
      if (key === "website") {
        data[key] = value;
        continue;
      }
      data[key] = typeof value === "string" ? value.trim() : value;
    }

    form.querySelectorAll('input[type="checkbox"]').forEach((checkbox) => {
      data[checkbox.name] = checkbox.checked;
    });

    return data;
  };

  const setDiscordStatus = (message, state = "") => {
    if (!discordStatus || !discordMessage) {
      return;
    }
    discordStatus.hidden = !message;
    discordMessage.textContent = message || "";
    discordStatus.dataset.state = state;
    if (discordJoin) {
      discordJoin.hidden = state !== "not-member";
    }
  };

  const checkDiscordMembership = async () => {
    const request = ++membershipRequest;
    const username = String(discordInput?.value || "").trim();
    discordVerified = false;
    if (submitBtn) {
      submitBtn.disabled = true;
    }
    if (username.length < 2) {
      setDiscordStatus(username ? "Keep typing your Discord username to check it." : "", "checking");
      return;
    }
    setDiscordStatus("Checking this Discord username against the server…", "checking");
    try {
      const response = await fetch(`/api/v1/applications/discord-membership?username=${encodeURIComponent(username)}`);
      const body = await response.json().catch(() => null);
      if (request !== membershipRequest) {
        return;
      }
      discordVerified = Boolean(response.ok && body?.member);
      setDiscordStatus(
        discordVerified
          ? "Discord membership confirmed — you can submit this application."
          : response.ok
            ? "That Discord username is not in the server."
            : body?.error?.message || "Discord verification is temporarily unavailable. Please try again.",
        discordVerified ? "success" : response.ok ? "not-member" : "error"
      );
      if (submitBtn) {
        submitBtn.disabled = !discordVerified;
      }
    } catch {
      if (request === membershipRequest) {
        setDiscordStatus("Discord verification is temporarily unavailable. Please try again.", "error");
      }
    }
  };

  if (discordInput) {
    if (submitBtn) {
      submitBtn.disabled = true;
    }
    discordInput.addEventListener("input", () => {
      clearTimeout(membershipTimer);
      membershipTimer = setTimeout(checkDiscordMembership, 300);
    });
    if (discordInput.value.trim()) {
      void checkDiscordMembership();
    }
  }

  form.querySelectorAll("textarea[data-max]").forEach((textarea) => {
    const counter = document.querySelector(`[data-count-for="${textarea.id}"]`);
    if (!counter) {
      return;
    }
    const max = Number(textarea.getAttribute("data-max")) || 0;
    const update = () => {
      const used = textarea.value.length;
      counter.textContent = max ? `${used} / ${max}` : "";
    };
    textarea.addEventListener("input", update);
    update();
  });

  form.addEventListener("submit", async (event) => {
    event.preventDefault();
    if (!discordVerified) {
      setStatus("Confirm your Discord membership before applying.", true);
      return;
    }
    clearFieldErrors();
    setStatus("Sending application…", false);

    if (submitBtn) {
      submitBtn.disabled = true;
    }

    try {
      const response = await fetch("/api/v1/applications", {
        method: "POST",
        headers: {
          Accept: "application/json",
          "Content-Type": "application/json",
        },
        body: JSON.stringify(collectPayload()),
      });

      const body = await response.json().catch(() => null);

      if (response.status === 422 && body?.error?.details) {
        body.error.details.forEach((detail) => {
          if (detail.field && detail.message) {
            showFieldError(detail.field, detail.message);
          }
        });
        setStatus(body.error.message || "Some fields need attention.", true);
        return;
      }

      if (response.status === 429) {
        setStatus(body?.error?.message || "Too many submissions. Please wait and try again.", true);
        return;
      }

      if (!response.ok || !body?.application?.publicId) {
        setStatus(body?.error?.message || "Could not send the application. Please try again.", true);
        return;
      }

      window.location.assign(`/apply/thanks?ref=${encodeURIComponent(body.application.publicId)}`);
    } catch {
      setStatus("Could not reach the server. Check your connection and try again.", true);
    } finally {
      if (submitBtn && !window.location.pathname.startsWith("/apply/thanks")) {
        submitBtn.disabled = !discordVerified;
      }
    }
  });
})();
