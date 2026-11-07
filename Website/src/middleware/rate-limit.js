"use strict";

/**
 * Small in-memory fixed-window rate limiter.
 *
 * Deliberately dependency free. It is per-process, so once the site runs on
 * more than one instance the counters should move to a shared store (SQLite or
 * Redis). Good enough to protect the form endpoints as they are introduced.
 */

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

/**
 * @param {{windowMs?: number, max?: number, message?: string}} options
 */
const rateLimit = ({ windowMs = 60_000, max = 5, message = "Too many requests. Try again shortly." } = {}) => {
  return (req, res, next) => {
    const now = Date.now();
    sweep(now);

    // Constraint: key on the route pattern; req.path varies per :publicId and would mint a bucket per id.
    const key = `${req.method}:${req.baseUrl}${req.route?.path ?? ""}:${clientIp(req)}`;
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
