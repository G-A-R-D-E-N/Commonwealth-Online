(function (root, factory) {
  const api = factory();
  if (typeof module === "object" && module.exports) {
    module.exports = api;
  } else {
    root.CoUrlPolicy = api;
  }
})(typeof globalThis !== "undefined" ? globalThis : this, function () {
  const parseUrl = (value, base) => {
    try {
      return new URL(String(value || ""), base || "https://example.invalid/");
    } catch {
      return null;
    }
  };

  const protocolOf = (value, base) =>
    parseUrl(value, base)?.protocol.toLowerCase() || "";

  const isSafeLink = (value, base) =>
    ["http:", "https:", "mailto:"].includes(protocolOf(value, base));

  const isExternalHttp = (value, base) => {
    const url = parseUrl(value, base);
    const origin = parseUrl(base);
    return Boolean(
      url &&
        origin &&
        ["http:", "https:"].includes(url.protocol.toLowerCase()) &&
        url.origin !== origin.origin
    );
  };

  const isSafeImage = (value, base) => {
    const raw = String(value || "").trim();
    if (/^data:image\/(?:png|gif|jpe?g|webp);base64,/i.test(raw)) {
      return true;
    }
    return ["http:", "https:"].includes(protocolOf(raw, base));
  };

  return { isExternalHttp, isSafeImage, isSafeLink, protocolOf };
});
