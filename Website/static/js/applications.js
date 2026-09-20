(() => {
  const form = document.querySelector("[data-apply-form]");
  if (!form) {
    return;
  }

  const statusEl = document.getElementById("apply-status");
  const submitBtn = document.getElementById("apply-submit");
  const type = form.getAttribute("data-apply-type") || "team";

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
    input.setAttribute(
      "aria-describedby",
      [input.getAttribute("aria-describedby"), error.id].filter(Boolean).join(" ")
    );
  };

  const collectPayload = () => {
    const data = { type, source: "web" };
    const formData = new FormData(form);

    for (const [key, value] of formData.entries()) {
      data[key] = typeof value === "string" ? value.trim() : value;
    }

    form.querySelectorAll('input[type="checkbox"]').forEach((checkbox) => {
      data[checkbox.name] = checkbox.checked;
    });

    return data;
  };

  form.querySelectorAll("textarea[data-max]").forEach((textarea) => {
    const counter = document.querySelector(`[data-count-for="${textarea.id}"]`);
    if (!counter) {
      return;
    }
    const max = Number(textarea.getAttribute("data-max")) || 0;
    const update = () => {
      counter.textContent = max ? `${textarea.value.length} / ${max}` : "";
    };
    textarea.addEventListener("input", update);
    update();
  });

  form.addEventListener("submit", async (event) => {
    event.preventDefault();
    clearFieldErrors();
    setStatus("Sending application…");

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
        submitBtn.disabled = false;
      }
    }
  });
})();
