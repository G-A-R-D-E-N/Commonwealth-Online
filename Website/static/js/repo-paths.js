(() => {
  const EXTERNAL_TARGET = /^(?:[a-z][a-z0-9+.-]*:|\/\/)/i;

  const resolveRepoPath = (value, baseDir = "") => {
    if (typeof value !== "string") {
      return null;
    }
    const target = value.trim();
    if (!target || EXTERNAL_TARGET.test(target)) {
      return null;
    }

    const hashAt = target.indexOf("#");
    const fragment = hashAt >= 0 ? target.slice(hashAt) : "";
    const beforeHash = hashAt >= 0 ? target.slice(0, hashAt) : target;
    const queryAt = beforeHash.indexOf("?");
    const pathPart = queryAt >= 0 ? beforeHash.slice(0, queryAt) : beforeHash;
    const query = queryAt >= 0 ? beforeHash.slice(queryAt) : "";
    if (!pathPart.split("/").some((segment) => segment && segment !== ".")) {
      return null;
    }

    const segments = pathPart.startsWith("/") ? [] : baseDir.split("/").filter(Boolean);
    for (const segment of pathPart.split("/")) {
      if (segment === "" || segment === ".") {
        continue;
      }
      if (segment === "..") {
        if (segments.length === 0) {
          return null;
        }
        segments.pop();
      } else {
        segments.push(segment);
      }
    }
    if (segments.length === 0) {
      return null;
    }
    return { path: segments.join("/"), query, fragment };
  };

  window.CO_REPO_PATHS = { resolveRepoPath };
})();
