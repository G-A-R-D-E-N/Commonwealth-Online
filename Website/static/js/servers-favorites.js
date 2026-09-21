(() => {
  const navbar = document.querySelector("[data-co-navbar]");
  const url = (navbar?.dataset.supabaseUrl || "").replace(/\/+$/, "");
  const key = navbar?.dataset.supabaseKey || "";
  if (!url || !key || !window.supabase?.createClient) return;

  const client = window.coSupabase || window.supabase.createClient(url, key);
  window.coSupabase = client;
  let user = null;
  let favorites = new Set();

  const syncButtons = () => {
    document.querySelectorAll("[data-favorite-server]").forEach((button) => {
      if (!user) {
        button.hidden = true;
        return;
      }

      const serverId = button.dataset.serverId || "";
      const isFavorite = favorites.has(serverId);
      button.hidden = false;
      button.textContent = isFavorite ? "Favorited" : "Favorite";
      button.setAttribute("aria-pressed", String(isFavorite));

      if (!button.dataset.favoriteBound) {
        button.dataset.favoriteBound = "true";
        button.addEventListener("click", async () => {
          const id = button.dataset.serverId || "";
          const name = button.dataset.serverName || "";
          if (!user || !id || !name) return;

          button.disabled = true;
          const isCurrentlyFavorite = favorites.has(id);
          const result = isCurrentlyFavorite
            ? await client
                .from("user_server_favorites")
                .delete()
                .eq("user_id", user.id)
                .eq("server_id", id)
            : await client
                .from("user_server_favorites")
                .insert({
                  user_id: user.id,
                  server_id: id,
                  server_name: name,
                });

          if (!result.error) {
            if (isCurrentlyFavorite) {
              favorites.delete(id);
            } else {
              favorites.add(id);
            }
          }

          button.disabled = false;
          syncButtons();
        });
      }
    });
  };

  const loadFavorites = async () => {
    if (!user) {
      favorites = new Set();
      syncButtons();
      return;
    }

    const { data, error } = await client
      .from("user_server_favorites")
      .select("server_id")
      .eq("user_id", user.id);

    favorites = error ? new Set() : new Set((data || []).map((row) => row.server_id));
    syncButtons();
  };

  document.addEventListener("co:servers-rendered", syncButtons);

  const initialize = async () => {
    const { data } = await client.auth.getSession();
    user = data.session?.user || null;
    await loadFavorites();

    client.auth.onAuthStateChange((_event, session) => {
      user = session?.user || null;
      loadFavorites();
    });
  };

  initialize();
})();
