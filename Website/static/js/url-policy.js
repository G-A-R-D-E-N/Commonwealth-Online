(function (root, factory) {
  const api = factory();
  if (typeof module === "object" && module.exports) {
    module.exports = api;
  } else {
    root.CoUrlPolicy = api;
  }
})(typeof globalThis !== "undefined" ? globalThis : this, function () {
  const protocolOf = (value, base) => {
    try {
      return new URL(String(value || ""), base || "https://example.invalid/").protocol.toLowerCase();
    } catch {
      return "";
    }
  };

  const isSafeLink = (value, base) =>
    ["http:", "https:", "mailto:"].includes(protocolOf(value, base));

  const isSafeImage = (value, base) => {
    const raw = String(value || "").trim();
    if (/^data:image\/(?:png|gif|jpe?g|webp);base64,/i.test(raw)) {
      return true;
    }
    return ["http:", "https:"].includes(protocolOf(raw, base));
  };

  return { isSafeImage, isSafeLink, protocolOf };
});
