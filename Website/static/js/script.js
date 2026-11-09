(() => {
  const yearEl = document.getElementById("footer-year");
  if (yearEl) {
    yearEl.textContent = String(new Date().getFullYear());
  }

  const lightbox = document.getElementById("media-lightbox");
  const lightboxImage = document.getElementById("media-lightbox-image");
  const lightboxName = document.getElementById("media-lightbox-name");
  const lightboxDescription = document.getElementById("media-lightbox-description");
  const lightboxClose = lightbox?.querySelector(".media-lightbox__close");
  const LIGHTBOX_CLOSE_MS = 280;

  const preventBackgroundScroll = (event) => {
    if (lightbox?.contains(event.target)) {
      return;
    }

    event.preventDefault();
  };

  const lockPageScroll = () => {
    document.body.classList.add("has-lightbox-open");
    document.addEventListener("wheel", preventBackgroundScroll, { passive: false });
    document.addEventListener("touchmove", preventBackgroundScroll, { passive: false });
  };

  const unlockPageScroll = () => {
    document.body.classList.remove("has-lightbox-open");
    document.removeEventListener("wheel", preventBackgroundScroll);
    document.removeEventListener("touchmove", preventBackgroundScroll);
  };

  const clearLightbox = () => {
    if (!lightboxImage || !lightboxName || !lightboxDescription) {
      return;
    }

    lightboxImage.removeAttribute("src");
    lightboxImage.alt = "";
    lightboxName.textContent = "";
    lightboxDescription.textContent = "";
  };

  const closeLightbox = () => {
    if (!lightbox?.open || lightbox.classList.contains("is-closing")) {
      return;
    }

    lightbox.classList.add("is-closing");

    const finishClose = () => {
      lightbox.classList.remove("is-closing");
      lightbox.close();
      unlockPageScroll();
      clearLightbox();
    };

    let closed = false;
    const onPanelAnimationEnd = (event) => {
      if (closed || event.animationName !== "co-lightbox-panel-out") {
        return;
      }

      closed = true;
      lightbox.removeEventListener("animationend", onPanelAnimationEnd);
      finishClose();
    };

    lightbox.addEventListener("animationend", onPanelAnimationEnd);
    window.setTimeout(() => {
      if (!closed) {
        closed = true;
        lightbox.removeEventListener("animationend", onPanelAnimationEnd);
        finishClose();
      }
    }, LIGHTBOX_CLOSE_MS);
  };

  if (lightbox && lightboxImage && lightboxName && lightboxDescription && typeof lightbox.showModal === "function") {
    document.querySelectorAll(".preview-window__trigger").forEach((trigger) => {
      trigger.addEventListener("click", () => {
        const figure = trigger.closest(".preview-window");
        const img = trigger.querySelector("img");
        const name = figure?.querySelector(".preview-window__name");
        const description = figure?.querySelector(".preview-window__description");

        if (!img || !name) {
          return;
        }

        lightbox.classList.remove("is-closing");
        lightboxImage.src = img.currentSrc || img.src;
        lightboxImage.alt = img.alt;
        lightboxName.textContent = name.textContent.trim();
        lightboxDescription.textContent = description?.textContent.trim() || "";
        lockPageScroll();
        lightbox.showModal();
      });
    });

    lightboxClose?.addEventListener("click", closeLightbox);

    lightbox.addEventListener("cancel", (event) => {
      event.preventDefault();
      closeLightbox();
    });

    lightbox.addEventListener("click", (event) => {
      if (event.target === lightbox) {
        closeLightbox();
      }
    });
  }
})();
