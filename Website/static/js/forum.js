(() => {
  const root = document.querySelector("[data-forum]");
  if (!root) {
    return;
  }

  const status = root.querySelector("[data-forum-status]");
  const list = root.querySelector("[data-forum-categories]");
  const url = (root.dataset.supabaseUrl || "").replace(/\/+$/, "");
  const key = root.dataset.supabaseKey || "";

  if (!url || !key || !status || !list) {
    return;
  }

  const endpoint = new URL("/rest/v1/forum_categories", url);
  endpoint.searchParams.set("select", "slug,name,description,position,is_locked");
  endpoint.searchParams.set("order", "position.asc,name.asc");

  fetch(endpoint, {
    headers: {
      apikey: key,
      Accept: "application/json",
    },
  })
    .then(async (response) => {
      if (!response.ok) {
        throw new Error(`forum categories returned ${response.status}`);
      }
      return response.json();
    })
    .then((categories) => {
      list.replaceChildren();

      for (const category of categories) {
        const item = document.createElement("li");
        const name = document.createElement("span");
        const description = document.createElement("span");

        name.className = "feature-list__name";
        name.textContent = category.name;
        description.className = "feature-list__desc";
        description.textContent =
          category.description || (category.is_locked ? "Read-only board." : "Community discussion.");

        item.append(name, description);
        list.append(item);
      }

      status.textContent = categories.length
        ? "Forum backend connected. Public boards are loading from Supabase."
        : "Forum backend connected. No public boards have been created yet.";
      list.hidden = categories.length === 0;
    })
    .catch(() => {
      status.textContent = "Forum data is temporarily unavailable. Discord remains available for discussion.";
    });
})();
