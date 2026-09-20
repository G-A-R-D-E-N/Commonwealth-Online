import { withSupabase } from "npm:@supabase/server@^1";
import { scheduleDiscordNotification } from "./discord.mjs";
import { text, validateApplication } from "./validation.mjs";

const APPLICATION_RATE_LIMIT = 5;
const APPLICATION_RATE_WINDOW_SECONDS = 600;
const corsOrigins = new Set(
  (Deno.env.get("APPLICATION_CORS_ORIGINS") ||
    "https://commonwealth-online.com,https://www.commonwealth-online.com,https://g-a-r-d-e-n.github.io,http://localhost:3000")
    .split(",")
    .map((origin) => origin.trim())
    .filter(Boolean),
);

const corsHeaders = (request: Request) => {
  const origin = request.headers.get("origin");
  const headers = new Headers({
    "Access-Control-Allow-Headers": "apikey, authorization, content-type",
    "Access-Control-Allow-Methods": "POST, OPTIONS",
    "Access-Control-Max-Age": "86400",
    Vary: "Origin",
  });
  if (origin && corsOrigins.has(origin)) {
    headers.set("Access-Control-Allow-Origin", origin);
  }
  return headers;
};

const json = (request: Request, body: unknown, status = 200) =>
  Response.json(body, { status, headers: corsHeaders(request) });

const isDiscordMember = async (username: string) => {
  const token = Deno.env.get("DISCORD_BOT_TOKEN");
  const guildId = Deno.env.get("DISCORD_GUILD_ID");

  if (!token || !guildId) {
    return { ok: true as const, member: true };
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

const applicationAddress = (request: Request) =>
  request.headers.get("cf-connecting-ip") ||
  request.headers.get("x-real-ip") ||
  request.headers.get("x-forwarded-for")?.split(",")[0]?.trim() ||
  "unknown";

const hashAddress = async (address: string) => {
  const salt = Deno.env.get("SUPABASE_SERVICE_ROLE_KEY") || "application-rate-limit";
  const input = new TextEncoder().encode(`${salt}:${address}`);
  const digest = await crypto.subtle.digest("SHA-256", input);
  return Array.from(new Uint8Array(digest), (byte) => byte.toString(16).padStart(2, "0")).join("");
};

const consumeApplicationRateLimit = async (supabaseAdmin: any, request: Request) => {
  const bucket = await hashAddress(applicationAddress(request));
  const { data, error } = await supabaseAdmin.rpc("consume_application_rate_limit", {
    p_bucket: bucket,
    p_limit: APPLICATION_RATE_LIMIT,
    p_window_seconds: APPLICATION_RATE_WINDOW_SECONDS,
  });
  return { ok: !error, allowed: data === true };
};

export default {
  fetch: withSupabase({ auth: "publishable" }, async (req, ctx) => {
    if (req.method === "OPTIONS") {
      return new Response(null, { status: 204, headers: corsHeaders(req) });
    }
    if (req.method !== "POST") {
      return json(req, { error: { code: "method_not_allowed", message: "POST required." } }, 405);
    }

    try {
      const input = await req.json().catch(() => null);
      if (!input || typeof input !== "object" || Array.isArray(input)) {
        return json(req, { error: { code: "invalid_json", message: "A JSON object is required." } }, 400);
      }

      if (["website", "fax", "company"].some((key) => text(input[key], 200))) {
        return json(
          req,
          { application: { publicId: crypto.randomUUID(), status: "pending", createdAt: new Date().toISOString() } },
          201,
        );
      }

      const rateLimit = await consumeApplicationRateLimit(ctx.supabaseAdmin, req);
      if (!rateLimit.ok) {
        return json(req, { error: { code: "rate_limit_unavailable", message: "The application service is temporarily unavailable." } }, 503);
      }
      if (!rateLimit.allowed) {
        return json(req, { error: { code: "rate_limited", message: "Too many applications from this address. Please wait and try again." } }, 429);
      }

      const result = validateApplication(input as Record<string, unknown>);
      if (!result.ok) {
        return json(req, { error: { code: "validation_failed", message: "Some fields need attention.", details: result.errors } }, 422);
      }

      const membership = await isDiscordMember(result.value.discord_handle);
      if (!membership.ok) {
        return json(req, { error: { code: "discord_verification_unavailable", message: "Discord verification is temporarily unavailable." } }, 503);
      }
      if (!membership.member) {
        return json(
          req,
          {
            error: {
              code: "validation_failed",
              message: "Some fields need attention.",
              details: [{ field: "discordHandle", message: "Join the Discord server before applying." }],
            },
          },
          422,
        );
      }

      const { data, error } = await ctx.supabaseAdmin
        .from("applications")
        .insert(result.value)
        .select("public_id,type,discord_handle,display_name,email,timezone,availability,experience,motivation,answers,status,source,created_at,updated_at,discord_thread_id")
        .single();

      if (error || !data) {
        console.error("[applications] insert failed", error);
        return json(req, { error: { code: "storage_failed", message: "Could not store the application. Please try again." } }, 503);
      }

      const edgeRuntime = (globalThis as typeof globalThis & {
        EdgeRuntime?: { waitUntil: (promise: Promise<unknown>) => void };
      }).EdgeRuntime;
      scheduleDiscordNotification(
        data,
        Deno.env.get("DISCORD_APPLICATION_WEBHOOK_URL"),
        edgeRuntime?.waitUntil?.bind(edgeRuntime),
        fetch,
        (error) => console.error("[applications] Discord notification failed", error),
        async (message) => {
          const threadId = String(message?.channel_id || "").trim();
          if (!threadId) {
            return;
          }
          const { error: updateError } = await ctx.supabaseAdmin
            .from("applications")
            .update({ discord_thread_id: threadId })
            .eq("public_id", data.public_id);
          if (updateError) {
            console.error("[applications] Discord thread reference update failed", updateError);
          }
        },
      );

      return json(
        req,
        {
          application: {
            publicId: data.public_id,
            status: data.status,
            createdAt: data.created_at,
          },
        },
        201,
      );
    } catch (error) {
      console.error("[applications] request failed", error);
      return json(req, { error: { code: "server_error", message: "The application service is temporarily unavailable." } }, 500);
    }
  }),
};
