(() => {
  const root = document.querySelector("[data-factions]");
  if (!root) return;

  const url = (root.dataset.supabaseUrl || "").replace(/\/+$/, "");
  const key = root.dataset.supabaseKey || "";
  const list = root.querySelector("[data-factions-list]");
  const status = root.querySelector("[data-factions-status]");
  const reviewLink = root.querySelector("[data-faction-review-link]");
  const brandLogo = document.querySelector(".site-brand__logo");
  const assetBase = brandLogo ? new URL(brandLogo.src).pathname.split("/assets/")[0] : "";
  if (!url || !key || !list || !window.supabase?.createClient) return;

  const client = window.coSupabase || window.supabase.createClient(url, key);
  window.coSupabase = client;

  const setStatus = (message, error = false) => {
    if (!status) return;
    status.hidden = !message;
    status.textContent = message || "";
    status.classList.toggle("is-error", error);
  };

  const label = (value) =>
    String(value || "")
      .replaceAll("_", " ")
      .replace(/\b\w/g, (character) => character.toUpperCase());

  const renderFaction = (faction) => {
    const card = document.createElement("article");
    card.className = "faction-card";

    const heading = document.createElement("div");
    heading.className = "faction-card__head";

    const identity = document.createElement("div");
    const tag = document.createElement("span");
    tag.className = "faction-tag";
    tag.textContent = faction.tag;

    const name = document.createElement("h2");
    const link = document.createElement("a");
    link.href = assetBase + "/faction/?id=" + encodeURIComponent(faction.id);
    link.textContent = faction.name;
    name.append(link);

    identity.append(tag, name);

    const recruitment = document.createElement("span");
    recruitment.className = "faction-status";
    recruitment.textContent = label(faction.recruitment);

    heading.append(identity, recruitment);

    const summary = document.createElement("p");
    summary.textContent = faction.summary;

    const meta = document.createElement("div");
    meta.className = "faction-card__meta";
    meta.textContent = label(faction.focus);

    card.append(heading, summary, meta);
    return card;
  };

  const loadAdminActions = async () => {
    if (!reviewLink) return;

    const { data: sessionData } = await client.auth.getSession();
    const user = sessionData.session?.user || null;
    if (!user) return;

    const { data: profile, error } = await client
      .from("profiles")
      .select("role")
      .eq("id", user.id)
      .maybeSingle();

    reviewLink.hidden = Boolean(error) || profile?.role !== "admin";
  };

  const load = async () => {
    const { data, error } = await client
      .from("factions")
      .select("id,slug,name,tag,summary,focus,recruitment,created_at")
      .eq("status", "active")
      .order("created_at", { ascending: true });

    list.replaceChildren();

    if (error) {
      setStatus("Factions are temporarily unavailable.", true);
      return;
    }

    if (!data?.length) {
      list.textContent = "No approved factions yet.";
      return;
    }

    for (const faction of data) list.append(renderFaction(faction));
  };

  Promise.all([load(), loadAdminActions()]);
})();
