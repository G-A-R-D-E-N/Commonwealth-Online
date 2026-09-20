import { withSupabase } from "npm:@supabase/server@^1";

const TYPE_IDS = new Set(["team", "beta"]);
const EMAIL = /^[^\s@]+@[^\s@]+\.[^\s@]+$/;

const text = (value: unknown, max = 4000) =>
  String(value ?? "").replace(/\r\n/g, "\n").trim().slice(0, max);

const checked = (value: unknown) =>
  value === true || ["true", "on", "1", "yes"].includes(text(value, 8).toLowerCase());

const validate = (input: Record<string, unknown>) => {
  const errors: Array<{ field: string; message: string }> = [];
  const type = text(input.type, 32);

  if (!TYPE_IDS.has(type)) {
    return { ok: false as const, errors: [{ field: "type", message: "Unknown application type." }] };
  }

  const requiredText = (field: string, min: number, max: number) => {
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
  const availability = text(input.availability, 500) || null;
  const motivation = requiredText("motivation", 20, 4000);

  if (!EMAIL.test(email)) {
    errors.push({ field: "email", message: "That email address does not look valid." });
  }
  if (!checked(input.ageConfirmed)) {
    errors.push({ field: "ageConfirmed", message: "Please confirm the age requirement." });
  }

  const answers: Record<string, unknown> = { ageConfirmed: true };

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
    ? { ok: false as const, errors }
    : {
        ok: true as const,
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

const isDiscordMember = async (username: string) => {
  const token = Deno.env.get("DISCORD_BOT_TOKEN");
  const guildId = Deno.env.get("DISCORD_GUILD_ID");

  if (!token || !guildId) {
    return { ok: false as const };
  }

  const response = await fetch(
    `https://discord.com/api/v10/guilds/${encodeURIComponent(guildId)}/members/search?query=${encodeURIComponent(username)}&limit=100`,
    { headers: { Authorization: `Bot ${token}` } },
  );

  if (!response.ok) {
    return { ok: false as const };
  }

  const members = await response.json();
  const needle = username.toLowerCase();
  const member = Array.isArray(members) && members.some((entry) =>
    String(entry?.user?.username || "").toLowerCase() === needle
  );

  return { ok: true as const, member };
};

export default {
  fetch: withSupabase({ auth: "publishable" }, async (req, ctx) => {
    if (req.method !== "POST") {
      return Response.json({ error: { code: "method_not_allowed", message: "POST required." } }, { status: 405 });
    }

    const input = await req.json().catch(() => null);
    if (!input || typeof input !== "object" || Array.isArray(input)) {
      return Response.json({ error: { code: "invalid_json", message: "A JSON object is required." } }, { status: 400 });
    }

    if (["website", "fax", "company"].some((key) => text(input[key], 200))) {
      return Response.json(
        { application: { publicId: crypto.randomUUID(), status: "pending", createdAt: new Date().toISOString() } },
        { status: 201 },
      );
    }

    const result = validate(input as Record<string, unknown>);
    if (!result.ok) {
      return Response.json(
        { error: { code: "validation_failed", message: "Some fields need attention.", details: result.errors } },
        { status: 422 },
      );
    }

    const membership = await isDiscordMember(result.value.discord_handle);
    if (!membership.ok) {
      return Response.json(
        { error: { code: "discord_verification_unavailable", message: "Discord verification is temporarily unavailable." } },
        { status: 503 },
      );
    }
    if (!membership.member) {
      return Response.json(
        {
          error: {
            code: "validation_failed",
            message: "Some fields need attention.",
            details: [{ field: "discordHandle", message: "Join the Discord server before applying." }],
          },
        },
        { status: 422 },
      );
    }

    const { data, error } = await ctx.supabaseAdmin
      .from("applications")
      .insert(result.value)
      .select("public_id,status,created_at")
      .single();

    if (error || !data) {
      console.error("[applications] insert failed", error);
      return Response.json(
        { error: { code: "storage_failed", message: "Could not store the application. Please try again." } },
        { status: 503 },
      );
    }

    return Response.json(
      {
        application: {
          publicId: data.public_id,
          status: data.status,
          createdAt: data.created_at,
        },
      },
      { status: 201 },
    );
  }),
};
