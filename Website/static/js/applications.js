(() => {
  const form = document.querySelector("[data-apply-form]");
  if (!form) {
    return;
  }

  const statusEl = document.getElementById("apply-status");
  const submitBtn = document.getElementById("apply-submit");
  const discordInput = form.elements.namedItem("discordHandle");
  const discordStatus = document.getElementById("discord-verify-status");
  const discordMessage = document.getElementById("discord-verify-message");
  const discordJoin = discordStatus?.querySelector(".apply-discord-status__join");
  const sendCodeBtn = document.getElementById("discord-send-code");
  const verifyBox = document.getElementById("discord-verify");
  const verificationInput = document.getElementById("discord-verification-id");
  const codeInput = document.getElementById("field-discordCode");
  const confirmBtn = document.getElementById("discord-confirm-code");
  const resendBtn = document.getElementById("discord-resend");
  const type = form.getAttribute("data-apply-type") || "team";
  let discordVerified = false;
  let verificationRequest = 0;

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

  const postJson = async (url, payload) => {
    const response = await fetch(url, {
      method: "POST",
      headers: { Accept: "application/json", "Content-Type": "application/json" },
      body: JSON.stringify(payload),
    });
    return { response, body: await response.json().catch(() => null) };
  };

  const firstDetail = (body) => body?.error?.details?.[0]?.message || body?.error?.message;

  const setVerified = (verified) => {
    discordVerified = verified;
    if (submitBtn) {
      submitBtn.disabled = !verified;
    }
  };

  const resetVerification = () => {
    verificationRequest += 1;
    setVerified(false);
    if (verificationInput) {
      verificationInput.value = "";
    }
    if (codeInput) {
      codeInput.value = "";
      codeInput.disabled = false;
    }
    if (verifyBox) {
      verifyBox.hidden = true;
    }
    setDiscordStatus("", "");
  };

  const startVerification = async () => {
    const username = String(discordInput?.value || "").trim();
    if (username.length < 2) {
      setDiscordStatus("Enter your Discord username first.", "error");
      return;
    }
    const request = ++verificationRequest;
    setVerified(false);
    setDiscordStatus("Sending a code to your Discord DMs…", "checking");
    try {
      const { response, body } = await postJson("/api/v1/applications/discord-verification", { username });
      if (request !== verificationRequest) {
        return;
      }
      if (!response.ok || !body?.verificationId) {
        const notMember = response.status === 422 && /not in the/i.test(firstDetail(body) || "");
        setDiscordStatus(
          firstDetail(body) || "Discord verification is temporarily unavailable. Please try again.",
          notMember ? "not-member" : "error"
        );
        return;
      }
      if (verificationInput) {
        verificationInput.value = body.verificationId;
      }
      if (verifyBox) {
        verifyBox.hidden = false;
      }
      if (codeInput) {
        codeInput.value = "";
        codeInput.disabled = false;
        codeInput.focus();
      }
      setDiscordStatus(`We sent a 6-digit code to ${body.username}'s Discord DMs. Enter it below.`, "checking");
    } catch {
      if (request === verificationRequest) {
        setDiscordStatus("Could not reach the server. Check your connection and try again.", "error");
      }
    }
  };

  const confirmVerification = async () => {
    const verificationId = verificationInput?.value || "";
    const code = String(codeInput?.value || "").trim();
    if (!verificationId) {
      setDiscordStatus("Send a code to your Discord DMs first.", "error");
      return;
    }
    if (!/^\d{6}$/.test(code)) {
      setDiscordStatus("Enter the 6-digit code from your Discord DMs.", "error");
      return;
    }
    const request = verificationRequest;
    try {
      const { response, body } = await postJson("/api/v1/applications/discord-verification/confirm", { verificationId, code });
      if (request !== verificationRequest) {
        return;
      }
      if (!response.ok || !body?.verified) {
        setDiscordStatus(firstDetail(body) || "That code didn't work. Try again or send a new code.", "error");
        return;
      }
      if (codeInput) {
        codeInput.disabled = true;
      }
      setVerified(true);
      setDiscordStatus(`Discord account ${body.username} verified — you can submit this application.`, "success");
    } catch {
      if (request === verificationRequest) {
        setDiscordStatus("Could not reach the server. Check your connection and try again.", "error");
      }
    }
  };

  if (discordInput) {
    setVerified(false);
    if (sendCodeBtn) {
      sendCodeBtn.hidden = false;
      sendCodeBtn.addEventListener("click", () => void startVerification());
    }
    if (confirmBtn) {
      confirmBtn.hidden = false;
      confirmBtn.addEventListener("click", () => void confirmVerification());
    }
    if (resendBtn) {
      resendBtn.type = "button";
      resendBtn.addEventListener("click", () => void startVerification());
    }
    codeInput?.addEventListener("keydown", (event) => {
      if (event.key === "Enter") {
        event.preventDefault();
        void confirmVerification();
      }
    });
    discordInput.addEventListener("input", resetVerification);
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
      setStatus("Verify your Discord account with the code we DM you before applying.", true);
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
        let verificationLost = false;
        body.error.details.forEach((detail) => {
          if (detail.field === "discordHandle" || detail.field === "discordCode") {
            verificationLost = true;
          }
          if (detail.field && detail.message) {
            showFieldError(detail.field, detail.message);
          }
        });
        if (verificationLost) {
          resetVerification();
          setDiscordStatus(firstDetail(body), "error");
        }
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
