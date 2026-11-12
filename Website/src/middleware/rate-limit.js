"use strict";

/**
 * Small in-memory fixed-window rate limiter.
 *
 * Deliberately dependency free. It is per-process, so once the site runs on
 * more than one instance the counters should move to a shared store (SQLite or
 * Redis). Good enough to protect the form endpoints as they are introduced.
 */

const { isIPv6 } = require("node:net");

const buckets = new Map();
let lastSweep = 0;
const SWEEP_INTERVAL_MS = 60_000;
const MAX_BUCKETS = 20_000;

const dropExpired = (now) => {
  for (const [key, bucket] of buckets) {
    if (bucket.resetAt <= now) {
      buckets.delete(key);
    }
  }
};

const pruneToCapacity = () => {
  while (buckets.size > MAX_BUCKETS) {
    const oldest = buckets.keys().next();
    if (oldest.done) {
      return;
    }
    buckets.delete(oldest.value);
  }
};

const sweep = (now) => {
  if (now - lastSweep < SWEEP_INTERVAL_MS) {
    return;
  }
  lastSweep = now;
  dropExpired(now);
};

// req.ip only honours X-Forwarded-For when TRUST_PROXY sets "trust proxy".
const clientIp = (req) =>
  req.ip ||
  req.socket?.remoteAddress ||
  "unknown";

const expandIpv6 = (address) => {
  const [head, tail] = address.split("::");
  const left = head ? head.split(":") : [];
  const right = tail ? tail.split(":") : [];
  return [...left, ...new Array(8 - left.length - right.length).fill("0"), ...right];
};

const canonicalIpv6 = (ip) => {
  try {
    return new URL(`http://[${ip.split("%")[0]}]`).hostname.slice(1, -1);
  } catch {
    return ip;
  }
};

const rateLimitAddress = (ip) => {
  if (!isIPv6(ip)) {
    return ip;
  }
  const address = canonicalIpv6(ip);
  const mapped = /^::ffff:([0-9a-f]{1,4}):([0-9a-f]{1,4})$/.exec(address);
  if (mapped) {
    const [high, low] = mapped.slice(1).map((group) => parseInt(group, 16));
    return [high >> 8, high & 255, low >> 8, low & 255].join(".");
  }
  const prefix = expandIpv6(address).slice(0, 4).map((group) => parseInt(group, 16).toString(16));
  return `${prefix.join(":")}/64`;
};

/**
 * @param {{windowMs?: number, max?: number, message?: string}} options
 */
const rateLimit = ({ windowMs = 60_000, max = 5, message = "Too many requests. Try again shortly." } = {}) => {
  return (req, res, next) => {
    const now = Date.now();
    sweep(now);

    // Constraint: key on the route pattern; req.path varies per :publicId and would mint a bucket per id.
    const key = `${req.method}:${req.baseUrl}${req.route?.path ?? ""}:${rateLimitAddress(clientIp(req))}`;
    const bucket = buckets.get(key);

    if (!bucket || bucket.resetAt <= now) {
      buckets.delete(key);
      buckets.set(key, { count: 1, resetAt: now + windowMs });
      pruneToCapacity();
      res.set("X-RateLimit-Limit", String(max));
      res.set("X-RateLimit-Remaining", String(max - 1));
      return next();
    }

    bucket.count += 1;
    buckets.delete(key);
    buckets.set(key, bucket);
    const remaining = Math.max(0, max - bucket.count);
    res.set("X-RateLimit-Limit", String(max));
    res.set("X-RateLimit-Remaining", String(remaining));
    res.set("X-RateLimit-Reset", String(Math.ceil(bucket.resetAt / 1000)));

    if (bucket.count > max) {
      res.set("Retry-After", String(Math.ceil((bucket.resetAt - now) / 1000)));
      return res.status(429).json({ error: { code: "rate_limited", message } });
    }

    return next();
  };
};

module.exports = { rateLimit, clientIp };
