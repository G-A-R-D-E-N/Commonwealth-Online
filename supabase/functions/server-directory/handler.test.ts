import assert from "node:assert/strict";
import test from "node:test";
import { createDirectoryHandler, endpointText, observedSource, sourceGroup, toPublicListing } from "./handler.ts";

const BASE = "https://example.supabase.co/functions/v1/server-directory";
const ID = "live-" + "a".repeat(32);

const makeRpc = (overrides: Record<string, (args: Record<string, unknown>) => unknown> = {}) => {
  const calls: { fn: string; args: Record<string, unknown> }[] = [];
  const rpc = async (fn: string, args: Record<string, unknown>) => {
    calls.push({ fn, args });
    if (fn === "consume_server_directory_rate_limit") return { data: overrides.limit ? overrides.limit(args) : true, error: null };
    const handler = overrides[fn];
    if (!handler) return { data: null, error: { message: "unexpected" } };
    return { data: handler(args), error: null };
  };
  return { rpc, calls };
};

const request = (method: string, path: string, init: { body?: unknown; headers?: Record<string, string> } = {}) =>
  new Request(BASE + path, {
    method,
    headers: { "cf-connecting-ip": "8.8.4.4", ...(init.headers || {}) },
    body: init.body === undefined ? undefined : typeof init.body === "string" ? init.body : JSON.stringify(init.body),
  });

test("register forwards the observed source, never a body address", async () => {
  const { rpc, calls } = makeRpc({
    register_live_server: (args) => ({ status: 201, serverId: ID, secret: "s", host: args.p_source, heartbeatIntervalSeconds: 30 }),
  });
  const handle = createDirectoryHandler({ rpc });
  const response = await handle(
    request("POST", "/register", { body: { name: "A", port: 7777, maxPlayers: 8 }, headers: { "x-forwarded-for": "1.2.3.4" } }),
  );
  assert.equal(response.status, 201);
  const body = await response.json();
  assert.equal(body.serverId, ID);
  assert.equal(body.host, "8.8.4.4");
  assert.equal(body.status, undefined);
  const call = calls.find((c) => c.fn === "register_live_server")!;
  assert.equal(call.args.p_source, "8.8.4.4");
  assert.equal(call.args.p_allow_private, false);
});

test("untrusted forwarding headers are not accepted as the source", () => {
  const forwarded = new Request(BASE, { headers: { "x-forwarded-for": "1.2.3.4" } });
  const realIp = new Request(BASE, { headers: { "x-real-ip": "1.2.3.4" } });
  assert.equal(observedSource(forwarded), "");
  assert.equal(observedSource(realIp), "");
});

test("register rejects malformed and oversized bodies before the database", async () => {
  const { rpc, calls } = makeRpc();
  const handle = createDirectoryHandler({ rpc });
  assert.equal((await handle(request("POST", "/register", { body: "{not json" }))).status, 400);
  assert.equal((await handle(request("POST", "/register", { body: [1, 2] }))).status, 400);
  assert.equal((await handle(request("POST", "/register", { body: { name: "x".repeat(20000) } }))).status, 400);
  assert.ok(!calls.some((c) => c.fn === "register_live_server"));
});

test("database validation errors keep their code and status", async () => {
  const { rpc } = makeRpc({
    register_live_server: () => ({ status: 422, error: { code: "address_not_allowed", message: "no" } }),
  });
  const response = await createDirectoryHandler({ rpc })(request("POST", "/register", { body: { host: "1.2.3.4" } }));
  assert.equal(response.status, 422);
  assert.equal((await response.json()).error.code, "address_not_allowed");
});

test("oversized streamed bodies stop at the limit and cancel the reader", async () => {
  const { rpc, calls } = makeRpc();
  let pulls = 0;
  let cancelled = false;
  const body = new ReadableStream<Uint8Array>({
    pull(controller) {
      if (++pulls <= 3) controller.enqueue(new Uint8Array(16385));
      else controller.close();
    },
    cancel() { cancelled = true; },
  }, { highWaterMark: 0 });
  const incoming = new Request(BASE + "/register", {
    method: "POST", headers: { "cf-connecting-ip": "8.8.4.4" }, body, duplex: "half",
  } as RequestInit);
  const response = await createDirectoryHandler({ rpc })(incoming);
  assert.equal(response.status, 400);
  assert.equal(pulls, 1);
  assert.equal(cancelled, true);
  assert.ok(!calls.some((call) => call.fn === "register_live_server"));
});

test("bounded streamed bodies preserve UTF-8 characters across chunks", async () => {
  const expected = { name: "𐍈é", port: 7777 };
  const bytes = new TextEncoder().encode(JSON.stringify(expected));
  const split = bytes.indexOf(0xf0) + 1;
  const body = new ReadableStream<Uint8Array>({
    start(controller) {
      controller.enqueue(bytes.slice(0, split));
      controller.enqueue(bytes.slice(split));
      controller.close();
    },
  });
  const { rpc, calls } = makeRpc({ register_live_server: () => ({ status: 201 }) });
  const incoming = new Request(BASE + "/register", {
    method: "POST", headers: { "cf-connecting-ip": "8.8.4.4" }, body, duplex: "half",
  } as RequestInit);
  assert.equal((await createDirectoryHandler({ rpc })(incoming)).status, 201);
  assert.deepEqual(calls.at(-1)!.args.p_body, expected);
});

test("heartbeat passes bearer secret and counts", async () => {
  const { rpc, calls } = makeRpc({
    heartbeat_live_server: () => ({ status: 200, serverId: ID, players: 3, heartbeatIntervalSeconds: 30 }),
  });
  const response = await createDirectoryHandler({ rpc })(
    request("POST", `/${ID}/heartbeat`, { body: { players: 3, maxPlayers: 8 }, headers: { authorization: "Bearer sekrit" } }),
  );
  assert.equal(response.status, 200);
  const call = calls.find((c) => c.fn === "heartbeat_live_server")!;
  assert.deepEqual(call.args, { p_id: ID, p_secret: "sekrit", p_source: "8.8.4.4", p_body: { players: 3, maxPlayers: 8 } });
});

test("heartbeat without secret or with bad id is 404 so the registrar re-registers", async () => {
  const { rpc, calls } = makeRpc();
  const handle = createDirectoryHandler({ rpc });
  assert.equal((await handle(request("POST", `/${ID}/heartbeat`, { body: {} }))).status, 404);
  assert.equal((await handle(request("POST", `/../heartbeat`, { body: {}, headers: { authorization: "Bearer x" } }))).status, 404);
  assert.ok(!calls.some((c) => c.fn === "heartbeat_live_server"));
});

test("wrong secret maps to 404 unknown_server", async () => {
  const { rpc } = makeRpc({
    heartbeat_live_server: () => ({ status: 404, error: { code: "unknown_server", message: "x" } }),
  });
  const response = await createDirectoryHandler({ rpc })(
    request("POST", `/${ID}/heartbeat`, { body: {}, headers: { authorization: "Bearer wrong" } }),
  );
  assert.equal(response.status, 404);
  assert.equal((await response.json()).error.code, "unknown_server");
});

test("unregister calls the delete rpc", async () => {
  const { rpc, calls } = makeRpc({ unregister_live_server: () => ({ status: 200, removed: true }) });
  const response = await createDirectoryHandler({ rpc })(request("DELETE", `/${ID}`, { headers: { authorization: "Bearer s" } }));
  assert.equal(response.status, 200);
  assert.equal(calls.at(-1)!.fn, "unregister_live_server");
});

test("list returns only public-safe fields in the client shape", async () => {
  const { rpc } = makeRpc({
    list_live_servers: () => [
      {
        id: ID, name: "A", description: "", region: "", tags: [], host: "8.8.4.4", port: 7777, players: 8, max_players: 8,
        client_version: "0.0.0.2", fallout_runtime: "1.10.163", content_manifest_fingerprint: "", capabilities: ["protocol-v2"],
        last_seen: "2026-09-23T00:00:00Z", secret_hash: "leak",
      },
    ],
  });
  const response = await createDirectoryHandler({ rpc })(request("GET", ""));
  assert.equal(response.status, 200);
  assert.equal(response.headers.get("access-control-allow-origin"), "*");
  const body = await response.json();
  assert.equal(body.version, 2);
  const [server] = body.servers;
  assert.equal(server.address, "8.8.4.4:7777");
  assert.equal(server.maxPlayers, 8);
  assert.equal(server.status, "Full");
  assert.equal(server.live, true);
  assert.equal(server.compatibility.falloutRuntime, "1.10.163");
  assert.equal(server.passworded, false, "a row without the flag is open");
  assert.equal(server.password, false);
  assert.ok(!JSON.stringify(body).includes("leak"));
});

test("passworded listings are marked for the client password prompt", () => {
  const listing = toPublicListing({ id: ID, name: "Locked", host: "8.8.4.4", port: 7777, players: 1, max_players: 8, passworded: true });
  assert.equal(listing.passworded, true);
  assert.equal(listing.password, true, "older clients read the password field");
  assert.equal(toPublicListing({ id: ID, name: "Odd", host: "8.8.4.4", port: 7777, players: 1, max_players: 8, passworded: "true" }).passworded, false);
});

test("rate limited requests get 429 with Retry-After", async () => {
  const { rpc } = makeRpc({ limit: () => false });
  const response = await createDirectoryHandler({ rpc })(request("POST", "/register", { body: {} }));
  assert.equal(response.status, 429);
  assert.equal(response.headers.get("retry-after"), "60");
});

test("database outage is 503 so the registrar backs off", async () => {
  const rpc = async () => ({ data: null, error: { message: "down" } });
  const response = await createDirectoryHandler({ rpc })(request("POST", "/register", { body: {} }));
  assert.equal(response.status, 503);
});

test("missing source address is rejected for writes", async () => {
  const { rpc } = makeRpc();
  const response = await createDirectoryHandler({ rpc })(new Request(BASE + "/register", { method: "POST", body: "{}" }));
  assert.equal(response.status, 400);
});

test("missing source address is rejected for the list without consuming a shared bucket", async () => {
  const { rpc, calls } = makeRpc({ list_live_servers: () => [] });
  const response = await createDirectoryHandler({ rpc })(new Request(BASE, { method: "GET" }));
  assert.equal(response.status, 400);
  assert.equal((await response.json()).error.code, "unknown_source");
  assert.equal(calls.length, 0);
});

test("ipv6 sources rate-limit per /64 and list with brackets", async () => {
  assert.equal(sourceGroup("8.8.4.4"), "8.8.4.4");
  assert.equal(sourceGroup("2606:4700:10::1"), "2606:4700:0010:0000::/64");
  assert.equal(sourceGroup("2606:4700:10:0:ffff::9"), "2606:4700:0010:0000::/64", "one /64 is one bucket");
  assert.equal(sourceGroup("2606:4700:11::1"), "2606:4700:0011:0000::/64");
  assert.equal(sourceGroup("::1"), "0000:0000:0000:0000::/64");
  assert.equal(sourceGroup("1::2::3"), "1::2::3", "malformed addresses keep their own bucket");
  assert.equal(observedSource(new Request(BASE, { headers: { "cf-connecting-ip": "::ffff:8.8.4.4" } })), "8.8.4.4");
  assert.equal(endpointText("2606:4700:10::1", 7777), "[2606:4700:10::1]:7777");
  assert.equal(endpointText("8.8.4.4", 7777), "8.8.4.4:7777");
  assert.equal(toPublicListing({ id: ID, name: "Six", host: "2606:4700:10::1", port: 7777, players: 0, max_players: 8 }).address, "[2606:4700:10::1]:7777");

  const buckets: unknown[] = [];
  const { rpc } = makeRpc({ limit: (args) => { buckets.push(args.p_bucket); return true; }, list_live_servers: () => [] });
  const handle = createDirectoryHandler({ rpc });
  await handle(new Request(BASE, { headers: { "cf-connecting-ip": "2606:4700:10::1" } }));
  await handle(new Request(BASE, { headers: { "cf-connecting-ip": "2606:4700:10::abcd" } }));
  await handle(new Request(BASE, { headers: { "cf-connecting-ip": "2606:4700:11::1" } }));
  assert.equal(buckets[0], buckets[1], "addresses in one /64 share a rate-limit bucket");
  assert.notEqual(buckets[0], buckets[2]);
});
