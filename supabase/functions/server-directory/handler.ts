export type Rpc = (fn: string, args: Record<string, unknown>) => Promise<{ data: unknown; error: unknown }>;

export interface DirectoryOptions {
  rpc: Rpc;
  allowPrivateSources?: boolean;
  hashSalt?: string;
  registerLimit?: number;
  heartbeatLimit?: number;
  listLimit?: number;
  now?: () => Date;
}

const MAX_BODY_BYTES = 16 * 1024;
const SERVER_ID = /^live-[0-9a-f]{32}$/;

const corsHeaders = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Headers": "authorization, content-type",
  "Access-Control-Allow-Methods": "GET, POST, DELETE, OPTIONS",
  "Access-Control-Max-Age": "86400",
};

const json = (body: unknown, status = 200, extra: Record<string, string> = {}) =>
  new Response(JSON.stringify(body), {
    status,
    headers: { ...corsHeaders, "Content-Type": "application/json", "Cache-Control": "no-store", ...extra },
  });

const fail = (status: number, code: string, message: string, extra: Record<string, string> = {}) =>
  json({ error: { code, message } }, status, extra);

export const observedSource = (request: Request): string => {
  // Trust only cf-connecting-ip; X-Forwarded-For and X-Real-IP are client-controlled.
  const source = request.headers.get("cf-connecting-ip")?.trim() || "";
  const mapped = /^::ffff:(\d{1,3}(?:\.\d{1,3}){3})$/i.exec(source);
  return mapped ? mapped[1] : source;
};

const expandIpv6 = (address: string): string[] | null => {
  if (!/^[0-9a-f:]+$/i.test(address) || (address.match(/::/g) || []).length > 1) return null;
  const [head, tail] = address.includes("::") ? address.split("::") : [address, null];
  const left = head ? head.split(":") : [];
  const right = tail ? tail.split(":") : [];
  const missing = 8 - left.length - right.length;
  if (tail === null ? missing !== 0 : missing < 1) return null;
  const groups = [...left, ...Array(tail === null ? 0 : missing).fill("0"), ...right];
  if (groups.some((group) => group.length < 1 || group.length > 4)) return null;
  return groups.map((group) => group.toLowerCase().padStart(4, "0"));
};

export const sourceGroup = (source: string): string => {
  if (!source.includes(":")) return source;
  const groups = expandIpv6(source);
  return groups ? `${groups.slice(0, 4).join(":")}::/64` : source;
};

export const endpointText = (host: unknown, port: unknown) =>
  String(host).includes(":") ? `[${host}]:${port}` : `${host}:${port}`;

const readBearer = (request: Request) => {
  const match = /^Bearer\s+(\S{1,256})$/i.exec(request.headers.get("authorization") || "");
  return match ? match[1] : "";
};

const hashBucket = async (scope: string, salt: string, value: string) => {
  const digest = await crypto.subtle.digest("SHA-256", new TextEncoder().encode(`${scope}:${salt}:${value}`));
  return "srvdir:" + Array.from(new Uint8Array(digest), (b) => b.toString(16).padStart(2, "0")).join("");
};

const readBody = async (request: Request): Promise<unknown> => {
  const reader = request.body?.getReader();
  if (!reader) return {};
  const decoder = new TextDecoder();
  let text = "";
  let bytes = 0;
  try {
    for (;;) {
      const { done, value } = await reader.read();
      if (done) break;
      bytes += value.byteLength;
      if (bytes > MAX_BODY_BYTES) {
        await reader.cancel();
        return undefined;
      }
      text += decoder.decode(value, { stream: true });
    }
    text += decoder.decode();
  } finally {
    reader.releaseLock();
  }
  if (!text) return {};
  try {
    return JSON.parse(text);
  } catch {
    return undefined;
  }
};

const rpcResult = (data: unknown) => {
  const result = (data ?? {}) as Record<string, unknown>;
  const status = typeof result.status === "number" ? result.status : 500;
  const { status: _status, ...body } = result;
  return json(body, status);
};

export const toPublicListing = (row: Record<string, unknown>) => {
  const players = Number(row.players);
  const maxPlayers = Number(row.max_players);
  return {
    id: row.id,
    name: row.name,
    host: row.host,
    port: row.port,
    address: endpointText(row.host, row.port),
    description: row.description,
    region: row.region,
    tags: row.tags,
    password: row.passworded === true,
    passworded: row.passworded === true,
    players,
    maxPlayers,
    status: players >= maxPlayers ? "Full" : "Open",
    live: true,
    lastSeen: row.last_seen,
    compatibility: {
      clientVersion: row.client_version,
      falloutRuntime: row.fallout_runtime,
      contentManifestFingerprint: row.content_manifest_fingerprint,
      capabilities: row.capabilities,
    },
  };
};

export const createDirectoryHandler = (options: DirectoryOptions) => {
  const {
    rpc,
    allowPrivateSources = false,
    hashSalt = "server-directory",
    registerLimit = 6,
    heartbeatLimit = 30,
    listLimit = 120,
    now = () => new Date(),
  } = options;

  const limited = async (scope: string, source: string, limit: number) => {
    const { data, error } = await rpc("consume_server_directory_rate_limit", {
      p_bucket: await hashBucket(scope, hashSalt, sourceGroup(source)),
      p_limit: limit,
      p_window_seconds: 60,
    });
    if (error) return fail(503, "rate_limit_unavailable", "Directory is temporarily unavailable.");
    if (data !== true) return fail(429, "rate_limited", "Too many directory requests. Slow down.", { "Retry-After": "60" });
    return null;
  };

  return async (request: Request): Promise<Response> => {
    if (request.method === "OPTIONS") return new Response(null, { status: 204, headers: corsHeaders });

    const path = new URL(request.url).pathname.replace(/\/+$/, "");
    const route = path.replace(/^.*?\/server-directory/, "");
    const segments = route.split("/").filter(Boolean);

    try {
      const source = observedSource(request);
      if (!source) return fail(400, "unknown_source", "The request source address could not be determined.");

      if (request.method === "GET" && segments.length === 0) {
        const blocked = await limited("list", source, listLimit);
        if (blocked) return blocked;
        const { data, error } = await rpc("list_live_servers", {});
        if (error) return fail(503, "directory_unavailable", "Directory is temporarily unavailable.");
        const servers = ((data as Record<string, unknown>[]) || []).map(toPublicListing);
        return json(
          { version: 2, updatedAt: now().toISOString(), heartbeatIntervalSeconds: 30, ttlSeconds: 90, servers },
          200,
          { "Cache-Control": "public, max-age=10" },
        );
      }

      if (request.method === "POST" && segments.length === 1 && segments[0] === "register") {
        const blocked = await limited("register", source, registerLimit);
        if (blocked) return blocked;
        const body = await readBody(request);
        if (body === undefined || body === null || typeof body !== "object" || Array.isArray(body)) {
          return fail(400, "invalid_json", "A JSON object of at most 16 KiB is required.");
        }
        const { data, error } = await rpc("register_live_server", {
          p_source: source,
          p_body: body,
          p_allow_private: allowPrivateSources,
        });
        if (error) return fail(503, "directory_unavailable", "Directory is temporarily unavailable.");
        return rpcResult(data);
      }

      const isHeartbeat = request.method === "POST" && segments.length === 2 && segments[1] === "heartbeat";
      const isDelete = request.method === "DELETE" && segments.length === 1;
      if (isHeartbeat || isDelete) {
        const blocked = await limited("heartbeat", source, heartbeatLimit);
        if (blocked) return blocked;
        const id = segments[0];
        const secret = readBearer(request);
        if (!SERVER_ID.test(id) || !secret) {
          return fail(404, "unknown_server", "Listing expired or credentials are invalid; register again.");
        }
        if (isDelete) {
          const { data, error } = await rpc("unregister_live_server", { p_id: id, p_secret: secret, p_source: source });
          if (error) return fail(503, "directory_unavailable", "Directory is temporarily unavailable.");
          return rpcResult(data);
        }
        const body = await readBody(request);
        if (body === undefined || body === null || typeof body !== "object" || Array.isArray(body)) {
          return fail(400, "invalid_json", "A JSON object of at most 16 KiB is required.");
        }
        const { data, error } = await rpc("heartbeat_live_server", {
          p_id: id,
          p_secret: secret,
          p_source: source,
          p_body: body,
        });
        if (error) return fail(503, "directory_unavailable", "Directory is temporarily unavailable.");
        return rpcResult(data);
      }

      return fail(404, "not_found", "Unknown directory route.");
    } catch (error) {
      console.error("[server-directory] request failed", error);
      return fail(500, "server_error", "Directory is temporarily unavailable.");
    }
  };
};
