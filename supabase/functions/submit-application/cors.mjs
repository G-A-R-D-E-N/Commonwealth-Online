const configuredOrigins = (fallback) =>
  (globalThis.Deno?.env.get("APPLICATION_CORS_ORIGINS") || fallback)
    .split(",")
    .map((origin) => origin.trim())
    .filter(Boolean);

export const createCors = (fallbackOrigins, methods, allowedHeaders) => {
  const origins = new Set(configuredOrigins(fallbackOrigins));

  const headers = (request) => {
    const origin = request.headers.get("origin");
    const result = new Headers({
      "Access-Control-Allow-Headers": allowedHeaders,
      "Access-Control-Allow-Methods": methods,
      "Access-Control-Max-Age": "86400",
      Vary: "Origin",
    });
    if (origin && origins.has(origin)) {
      result.set("Access-Control-Allow-Origin", origin);
    }
    return result;
  };

  const wrap = (handler) => async (request, context) => {
    if (request.method === "OPTIONS") {
      return new Response(null, { status: 204, headers: headers(request) });
    }
    const response = await handler(request, context);
    const responseHeaders = new Headers(response.headers);
    for (const [name, value] of headers(request)) {
      responseHeaders.set(name, value);
    }
    return new Response(response.body, {
      status: response.status,
      statusText: response.statusText,
      headers: responseHeaders,
    });
  };

  return { headers, wrap };
};
