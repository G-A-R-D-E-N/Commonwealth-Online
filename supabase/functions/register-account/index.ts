import { createClient } from "npm:@supabase/supabase-js@2.105.0";

const IP_LIMIT = 5;
const IP_WINDOW_SECONDS = 600;
const EMAIL_LIMIT = 3;
const EMAIL_WINDOW_SECONDS = 3600;

const allowedOrigins = new Set(
  (
    Deno.env.get("ACCOUNT_CORS_ORIGINS") ||
    "https://commonwealth-online.com,https://www.commonwealth-online.com,https://g-a-r-d-e-n.github.io,http://localhost:3000"
  )
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

  if (origin && allowedOrigins.has(origin)) {
    headers.set("Access-Control-Allow-Origin", origin);
  }

  return headers;
};

const json = (request: Request, body: unknown, status = 200) =>
  Response.json(body, { status, headers: corsHeaders(request) });

const text = (value: unknown, max = 1000) =>
  typeof value === "string" ? value.trim().slice(0, max) : "";

const requestAddress = (request: Request) =>
  request.headers.get("cf-connecting-ip") ||
  request.headers.get("x-real-ip") ||
  request.headers.get("x-forwarded-for")?.split(",")[0]?.trim() ||
  "unknown";

const hashBucket = async (scope: string, value: string) => {
  const salt = Deno.env.get("SUPABASE_SERVICE_ROLE_KEY") || "signup-rate-limit";
  const input = new TextEncoder().encode(`${scope}:${salt}:${value.toLowerCase()}`);
  const digest = await crypto.subtle.digest("SHA-256", input);
  return Array.from(new Uint8Array(digest), (byte) => byte.toString(16).padStart(2, "0")).join("");
};

const consumeLimit = async (
  admin: ReturnType<typeof createClient>,
  bucket: string,
  limit: number,
  windowSeconds: number,
) => {
  const { data, error } = await admin.rpc("consume_signup_rate_limit", {
    p_bucket: bucket,
    p_limit: limit,
    p_window_seconds: windowSeconds,
  });

  return { ok: !error, allowed: data === true };
};

const allowedRedirects = new Set([
  "http://localhost:3000/account/",
  "http://127.0.0.1:3000/account/",
  "https://g-a-r-d-e-n.github.io/Commonwealth-Online/account/",
  "https://commonwealth-online.com/account/",
  "https://www.commonwealth-online.com/account/",
]);

Deno.serve(async (request) => {
  if (request.method === "OPTIONS") {
    return new Response(null, { status: 204, headers: corsHeaders(request) });
  }

  if (request.method !== "POST") {
    return json(request, { error: { code: "method_not_allowed", message: "POST required." } }, 405);
  }

  const origin = request.headers.get("origin");
  if (origin && !allowedOrigins.has(origin)) {
    return json(request, { error: { code: "origin_not_allowed", message: "Request origin is not allowed." } }, 403);
  }

  const supabaseUrl = Deno.env.get("SUPABASE_URL");
  const anonKey = Deno.env.get("SUPABASE_ANON_KEY");
  const serviceRoleKey = Deno.env.get("SUPABASE_SERVICE_ROLE_KEY");
  if (!supabaseUrl || !anonKey || !serviceRoleKey) {
    return json(request, { error: { code: "service_unavailable", message: "Registration is temporarily unavailable." } }, 503);
  }

  try {
    const input = await request.json().catch(() => null);
    if (!input || typeof input !== "object" || Array.isArray(input)) {
      return json(request, { error: { code: "invalid_json", message: "A JSON object is required." } }, 400);
    }

    const record = input as Record<string, unknown>;
    const email = text(record.email, 254).toLowerCase();
    const password = typeof record.password === "string" ? record.password : "";
    const username = text(record.username, 80);
    const redirectTo = text(record.redirectTo, 500);
    const captchaToken = text(record.captchaToken, 4096);
    const honeypot = text(record.website, 200);

    if (honeypot) {
      return json(
        request,
        { account: { requiresConfirmation: true }, session: null },
        201,
      );
    }

    if (!username || username.length > 80) {
      return json(request, { error: { code: "invalid_username", message: "Enter a valid username." } }, 422);
    }

    if (!email || !email.includes("@") || email.length > 254) {
      return json(request, { error: { code: "invalid_email", message: "Enter a valid email address." } }, 422);
    }

    if (password.length < 8 || password.length > 1024) {
      return json(request, { error: { code: "invalid_password", message: "Password must be at least 8 characters." } }, 422);
    }

    if (!allowedRedirects.has(redirectTo)) {
      return json(request, { error: { code: "invalid_redirect", message: "Registration redirect is not allowed." } }, 400);
    }

    const admin = createClient(supabaseUrl, serviceRoleKey, {
      auth: { persistSession: false, autoRefreshToken: false },
    });

    const ipBucket = await hashBucket("ip", requestAddress(request));
    const emailBucket = await hashBucket("email", email);
    const [ipLimit, emailLimit] = await Promise.all([
      consumeLimit(admin, ipBucket, IP_LIMIT, IP_WINDOW_SECONDS),
      consumeLimit(admin, emailBucket, EMAIL_LIMIT, EMAIL_WINDOW_SECONDS),
    ]);

    if (!ipLimit.ok || !emailLimit.ok) {
      return json(request, { error: { code: "rate_limit_unavailable", message: "Registration is temporarily unavailable." } }, 503);
    }

    if (!ipLimit.allowed || !emailLimit.allowed) {
      return json(
        request,
        { error: { code: "rate_limited", message: "Too many registration attempts. Please wait and try again." } },
        429,
      );
    }

    const authClient = createClient(supabaseUrl, anonKey, {
      auth: {
        persistSession: false,
        autoRefreshToken: false,
        detectSessionInUrl: false,
      },
    });

    const { data, error } = await authClient.auth.signUp({
      email,
      password,
      options: {
        emailRedirectTo: redirectTo,
        data: { display_name: username },
        captchaToken: captchaToken || undefined,
      },
    });

    if (error) {
      const rateLimited =
        error.status === 429 ||
        error.code === "over_email_send_rate_limit" ||
        /rate limit/i.test(error.message || "");

      return json(
        request,
        {
          error: {
            code: rateLimited ? "email_rate_limited" : error.code || "signup_failed",
            message: rateLimited
              ? "Confirmation email service is temporarily rate-limited. Please try again later."
              : error.message || "Could not create your account.",
          },
        },
        rateLimited ? 429 : Math.max(400, Math.min(error.status || 400, 499)),
      );
    }

    return json(
      request,
      {
        account: {
          requiresConfirmation: !data.session,
        },
        session: data.session
          ? {
              access_token: data.session.access_token,
              refresh_token: data.session.refresh_token,
            }
          : null,
      },
      201,
    );
  } catch (error) {
    console.error("[register-account] request failed", error);
    return json(request, { error: { code: "server_error", message: "Registration is temporarily unavailable." } }, 500);
  }
});
