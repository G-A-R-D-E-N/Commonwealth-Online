const TYPE_IDS = new Set(["team", "beta"]);
const EMAIL = /^[^\s@]+@[^\s@]+\.[^\s@]+$/;

export const text = (value, max = 4000) =>
  String(value ?? "").replace(/\r\n/g, "\n").trim().slice(0, max);

const checked = (value) =>
  value === true || ["true", "on", "1", "yes"].includes(text(value, 8).toLowerCase());

export const validateApplication = (input) => {
  const errors = [];
  const type = text(input.type, 32);

  if (!TYPE_IDS.has(type)) {
    return { ok: false, errors: [{ field: "type", message: "Unknown application type." }] };
  }

  const requiredText = (field, min, max) => {
    const value = text(input[field], max);
    if (value.length < min) {
      errors.push({ field, message: `Please write at least ${min} characters.` });
    }
    return value;
  };

  const displayName = requiredText("displayName", 2, 80);
  const email = text(input.email, 254);
  const discordHandle = requiredText("discordHandle", 2, 64);
  const timezone = text(input.timezone, 64) || null;
  const experience = requiredText("experience", type === "beta" ? 80 : 20, 2000);
  const availability = type === "beta"
    ? requiredText("availability", 10, 500)
    : text(input.availability, 500) || null;
  const motivation = requiredText("motivation", 20, 4000);

  if (!EMAIL.test(email)) {
    errors.push({ field: "email", message: "That email address does not look valid." });
  }
  if (!checked(input.ageConfirmed)) {
    errors.push({ field: "ageConfirmed", message: "Please confirm the age requirement." });
  }

  const answers = { ageConfirmed: true };

  if (type === "team") {
    const role = text(input.role, 32);
    const roles = new Set(["development", "art", "writing", "community", "qa", "other"]);
    if (!roles.has(role)) {
      errors.push({ field: "role", message: "Please choose an option." });
    }
    answers.role = role;

    const roleOther = text(input.roleOther, 200);
    if (role === "other" && !roleOther) {
      errors.push({ field: "roleOther", message: "Please say what you would like to help with." });
    }
    if (roleOther) answers.roleOther = roleOther;

    const portfolioUrl = text(input.portfolioUrl, 500);
    if (portfolioUrl) {
      try {
        const parsed = new URL(portfolioUrl.includes("://") ? portfolioUrl : `https://${portfolioUrl}`);
        if (!["http:", "https:"].includes(parsed.protocol)) throw new Error();
        answers.portfolioUrl = portfolioUrl;
      } catch {
        errors.push({ field: "portfolioUrl", message: "That link does not look valid." });
      }
    }
  } else {
    const edition = text(input.gameEdition, 32);
    if (!new Set(["anniversary", "original", "both"]).has(edition)) {
      errors.push({ field: "gameEdition", message: "Please choose an option." });
    }
    answers.gameEdition = edition;

    for (const field of [
      "manualModInstallConfirmed",
      "modManagerConfirmed",
      "logTroubleshootingConfirmed",
      "confidentialAgreed",
      "feedbackAgreed",
    ]) {
      const value = checked(input[field]);
      answers[field] = value;
      if (!value) {
        errors.push({ field, message: "Please confirm this requirement." });
      }
    }

    const hardware = text(input.hardware, 1000);
    if (hardware) answers.hardware = hardware;
  }

  return errors.length
    ? { ok: false, errors }
    : {
        ok: true,
        value: {
          type,
          discord_handle: discordHandle,
          display_name: displayName,
          email,
          timezone,
          availability,
          experience,
          motivation,
          answers,
          source: "web",
        },
      };
};
