import { withSupabase } from "npm:@supabase/server@^1";
import { postDiscordMessage } from "../submit-application/discord.mjs";
import { text } from "../submit-application/validation.mjs";

const STATUSES = new Set(["pending", "reviewing", "more_info_requested", "accepted", "rejected"]);
const REVIEW_COLUMNS = "public_id,type,discord_handle,display_name,email,timezone,availability,experience,motivation,answers,status,source,created_at,updated_at,reviewed_by,reviewed_at,review_note,discord_thread_id";
const corsOrigins = new Set(
  (Deno.env.get("APPLICATION_CORS_ORIGINS") || "http://localhost:3000")
    .split(",")
    .map((origin) => origin.trim())
    .filter(Boolean),
);

const corsHeaders = (request: Request) => {
  const origin = request.headers.get("origin");
  const headers = new Headers({
    "Access-Control-Allow-Headers": "apikey, authorization, content-type, x-application-review-token",
    "Access-Control-Allow-Methods": "GET, PATCH, OPTIONS",
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

const publicIdFromRequest = (request: Request) => {
  const segments = new URL(request.url).pathname.split("/").filter(Boolean);
  const functionIndex = segments.lastIndexOf("review-application");
  return functionIndex >= 0 ? decodeURIComponent(segments[functionIndex + 1] || "") : "";
};

const isReviewer = (request: Request) => {
  const expected = Deno.env.get("APPLICATION_REVIEW_TOKEN");
  return Boolean(expected && request.headers.get("x-application-review-token") === expected);
};

const postReviewUpdate = async (application: Record<string, unknown>) => {
  const webhook = Deno.env.get("DISCORD_APPLICATION_WEBHOOK_URL");
  const threadId = text(application.discord_thread_id, 128);
  if (!webhook || !threadId) {
    return;
  }

  await postDiscordMessage(webhook, {
    content: `Application ${application.public_id} is now ${application.status}.`,
    allowed_mentions: { parse: [] },
  }, threadId);
};

export default {
  fetch: withSupabase({ auth: "publishable" }, async (req, ctx) => {
    if (req.method === "OPTIONS") {
      return new Response(null, { status: 204, headers: corsHeaders(req) });
    }
    if (!isReviewer(req)) {
      return json(req, { error: { code: "unauthorized", message: "Reviewer authorization required." } }, 401);
    }
    if (!["GET", "PATCH"].includes(req.method)) {
      return json(req, { error: { code: "method_not_allowed", message: "GET or PATCH required." } }, 405);
    }

    const publicId = publicIdFromRequest(req);
    if (req.method === "GET") {
      if (publicId) {
        const { data, error } = await ctx.supabaseAdmin
          .from("applications")
          .select(REVIEW_COLUMNS)
          .eq("public_id", publicId)
          .maybeSingle();
        if (error) {
          return json(req, { error: { code: "storage_failed", message: "Could not load the application." } }, 503);
        }
        if (!data) {
          return json(req, { error: { code: "not_found", message: "No application with that reference." } }, 404);
        }
        return json(req, { application: data });
      }

      const url = new URL(req.url);
      const status = text(url.searchParams.get("status"), 32);
      const type = text(url.searchParams.get("type"), 32);
      const limit = Math.min(Math.max(Number.parseInt(url.searchParams.get("limit") || "50", 10) || 50, 1), 200);
      let query = ctx.supabaseAdmin.from("applications").select(REVIEW_COLUMNS, { count: "exact" }).order("created_at", { ascending: false }).limit(limit);
      if (status) query = query.eq("status", status);
      if (type) query = query.eq("type", type);
      const { data, count, error } = await query;
      if (error) {
        return json(req, { error: { code: "storage_failed", message: "Could not load applications." } }, 503);
      }
      return json(req, { applications: data || [], total: count || 0, limit });
    }

    const body = (await req.json().catch(() => null)) as Record<string, unknown> | null;
    const status = text(body?.status, 32);
    const reviewNote = text(body?.reviewNote, 4000) || null;
    if (!publicId || !STATUSES.has(status)) {
      return json(req, { error: { code: "validation_failed", message: "A valid application reference and status are required." } }, 422);
    }
    if (status === "more_info_requested" && !reviewNote) {
      return json(req, { error: { code: "validation_failed", message: "A review note is required when requesting more information." } }, 422);
    }

    const { data: existing, error: lookupError } = await ctx.supabaseAdmin
      .from("applications")
      .select("public_id")
      .eq("public_id", publicId)
      .maybeSingle();
    if (lookupError) {
      return json(req, { error: { code: "storage_failed", message: "Could not load the application." } }, 503);
    }
    if (!existing) {
      return json(req, { error: { code: "not_found", message: "No application with that reference." } }, 404);
    }

    const { data, error } = await ctx.supabaseAdmin
      .from("applications")
      .update({
        status,
        reviewed_by: text(body?.reviewedBy, 120) || null,
        reviewed_at: new Date().toISOString(),
        review_note: reviewNote,
        updated_at: new Date().toISOString(),
      })
      .eq("public_id", publicId)
      .select(REVIEW_COLUMNS)
      .single();
    if (error || !data) {
      return json(req, { error: { code: "storage_failed", message: "Could not update the application." } }, 503);
    }

    try {
      await postReviewUpdate(data);
    } catch (error) {
      console.error("[applications] Discord review update failed", error);
    }

    return json(req, { application: data });
  }),
};
