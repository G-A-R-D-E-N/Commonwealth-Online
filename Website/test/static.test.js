"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const http = require("node:http");
const path = require("node:path");

const root = path.resolve(__dirname, "..");
const dist = path.join(root, "dist");
const expectedPages = [
  "index.html",
  "media/index.html",
  "roadmap/index.html",
  "servers/index.html",
  "updates/index.html",
  "account/index.html",
  "profile/index.html",
  "members/index.html",
  "member/index.html",
  "factions/index.html",
  "faction/index.html",
  "faction/manage/index.html",
  "factions/apply/index.html",
  "apply/index.html",
  "apply/team/index.html",
  "apply/beta/index.html",
  "apply/thanks/index.html",
  "forum/index.html",
];

const startServer = (handler) =>
  new Promise((resolve) => {
    const server = http.createServer(handler);
    server.listen(0, "127.0.0.1", () => resolve(server));
  });

const closeServer = (server) =>
  new Promise((resolve, reject) => {
    server.close((error) => (error ? reject(error) : resolve()));
    server.closeAllConnections?.();
  });

const request = async (base, route) => {
  const response = await fetch(`${base}${route}`);
  return { response, text: await response.text() };
};

const serveStatic = (req, res) => {
  const requestPath = decodeURIComponent(new URL(req.url, "http://127.0.0.1").pathname);
  const relativePath = requestPath.endsWith("/") ? `${requestPath}index.html` : requestPath;
  const filePath = path.resolve(dist, `.${relativePath}`);
  if (!filePath.startsWith(`${dist}${path.sep}`)) {
    res.writeHead(404).end();
    return;
  }
  fs.createReadStream(filePath).on("error", () => res.writeHead(404).end()).pipe(res);
};

const run = async () => {
  assert.equal(fs.existsSync(dist), true, "run npm run build:static first");
  for (const page of expectedPages) {
    assert.equal(fs.existsSync(path.join(dist, page)), true, `missing ${page}`);
  }

  const htmlFiles = [];
  const collectHtml = (directory) => {
    for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
      const filePath = path.join(directory, entry.name);
      if (entry.isDirectory()) {
        collectHtml(filePath);
      } else if (entry.name.endsWith(".html")) {
        htmlFiles.push(filePath);
      }
    }
  };
  collectHtml(dist);
  for (const filePath of htmlFiles) {
    const html = fs.readFileSync(filePath, "utf8");
    assert.equal(/<%|<%=|<%-/.test(html), false, `unresolved EJS in ${filePath}`);
    assert.match(html, /href="\/static\/css\/discord-link\.css\?v=20260921-1"/);
    assert.match(html, /src="\/static\/js\/discord-link\.js\?v=20260921-1"/);
    assert.doesNotMatch(html, /@widgetbot\/crate/);
    assert.doesNotMatch(html, /\/static\/(?:js|css)\/widgetbot\./);
    assert.match(html, /script-src 'self' https:\/\/cdn\.jsdelivr\.net https:\/\/challenges\.cloudflare\.com/);
    assert.match(html, /frame-src 'self' https:\/\/challenges\.cloudflare\.com/);
    assert.doesNotMatch(html, /frame-src[^;]*e\.widgetbot\.io/);
    assert.doesNotMatch(html, /script-src[^;]*unsafe-inline/);
    assert.doesNotMatch(html, /script-src[^;]*unsafe-eval/);
  }

  const staticServer = await startServer(serveStatic);
  const staticBase = `http://127.0.0.1:${staticServer.address().port}`;
  try {
    for (const route of ["/", "/media/", "/roadmap/", "/servers/", "/updates/", "/account/", "/profile/", "/members/", "/member/", "/factions/", "/faction/?id=test", "/faction/manage/?id=test", "/factions/apply/", "/apply/", "/forum/"]) {
      const page = await request(staticBase, route);
      assert.equal(page.response.status, 200, route);
    }

    const staticData = JSON.parse(fs.readFileSync(path.join(dist, "data/servers.json"), "utf8"));
    const sourceData = JSON.parse(fs.readFileSync(path.join(root, "servers/server.json"), "utf8"));
    assert.deepEqual(staticData, sourceData, "server data copied unchanged");
    for (const image of ["server-browser-direct-connect.webp", "server-browser-recent.webp"]) {
      assert.equal(fs.existsSync(path.join(dist, "assets/images", image)), true, `missing ${image}`);
    }

    const changelogs = JSON.parse(fs.readFileSync(path.join(dist, "data/changelogs.json"), "utf8"));
    assert.equal(changelogs.length > 0, true, "changelog data is populated");
    assert.equal(changelogs[0].version, "1.0.6");
    assert.equal(changelogs[0].date, "2026-09-20");
    assert.match(changelogs[0].markdown, /server browser/i);

    const staticApply = await request(staticBase, "/apply/");
    const staticAccount = await request(staticBase, "/account/");
    const staticProfile = await request(staticBase, "/profile/");
    const staticMembers = await request(staticBase, "/members/");
    const staticMember = await request(staticBase, "/member/?id=test");
    const staticFactions = await request(staticBase, "/factions/");
    const staticFaction = await request(staticBase, "/faction/?id=test");
    const staticFactionApply = await request(staticBase, "/factions/apply/");
    const staticTeamForm = await request(staticBase, "/apply/team/");
    const staticBetaForm = await request(staticBase, "/apply/beta/");
    const staticThanks = await request(staticBase, "/apply/thanks/?ref=static-proof");
    const staticUpdates = await request(staticBase, "/updates/");
    const staticMedia = await request(staticBase, "/media/");
    assert.match(staticTeamForm.text, /data-supabase-url/);
    assert.match(staticBetaForm.text, /data-supabase-key/);
    assert.match(staticAccount.text, /data-account/);
    assert.match(staticAccount.text, /data-captcha-provider=/);
    assert.match(staticAccount.text, /data-captcha-site-key=/);
    assert.match(staticAccount.text, /name="website"/);
    assert.match(staticAccount.text, /data-account-nav/);
    assert.match(staticAccount.text, /data-account-nav-avatar/);
    const donateIndex = staticAccount.text.indexOf(">Donate</a>");
    const accountNavIndex = staticAccount.text.indexOf("data-account-nav");
    assert.ok(donateIndex >= 0 && accountNavIndex > donateIndex, "account/profile control must render after Donate");
    assert.match(staticAccount.text, />Your Commonwealth starts here\.</);
    assert.doesNotMatch(staticAccount.text, />\s*[^<]*Supabase[^<]*</i);
    assert.doesNotMatch(staticAccount.text, /data-profile-form/);
    assert.doesNotMatch(staticAccount.text, /data-discord-link/);
    assert.doesNotMatch(staticAccount.text, /name="avatar_url"/);
    assert.doesNotMatch(staticAccount.text, /type="file"/);
    assert.match(staticProfile.text, /data-profile/);
    assert.match(staticProfile.text, /class="profile-column profile-column--main"/);
    assert.match(staticProfile.text, /class="profile-column profile-column--side"/);
    assert.match(staticProfile.text, /data-profile-form/);
    assert.match(staticProfile.text, /data-password-form/);
    assert.match(staticProfile.text, /data-community-profile-form/);
    assert.match(staticProfile.text, /data-friends-list/);
    assert.match(staticProfile.text, /data-notifications-list/);
    assert.match(staticMembers.text, /data-members/);
    assert.match(staticMembers.text, /data-members-search/);
    assert.match(staticFactions.text, /class="factions-hero"/);
    assert.match(staticFactions.text, /class="factions-directory"/);
    assert.match(staticFactions.text, />Faction directory</);
    assert.doesNotMatch(staticFactions.text, /section-panel faction-shell/);
    assert.match(staticMember.text, /data-member/);
    assert.match(staticMember.text, /data-friend-action/);
    assert.match(staticMember.text, /data-block-action/);
    assert.match(staticMember.text, /data-member-friends-list/);
    assert.match(staticMember.text, /data-member-username-history-list/);
    assert.match(staticMember.text, /data-member-badges-list/);
    assert.match(staticMember.text, /data-member-recent-servers-list/);
    assert.match(staticMember.text, /data-member-characters-list/);
    assert.doesNotMatch(staticProfile.text, /data-discord-link/);
    assert.doesNotMatch(staticProfile.text, />Link Discord</);
    assert.doesNotMatch(staticAccount.text, />Continue with Discord</);
    assert.match(staticProfile.text, /name="username"/);
    assert.match(staticProfile.text, /name="email"/);
    assert.match(staticProfile.text, /name="current_password"/);
    assert.match(staticProfile.text, /name="new_password"/);
    assert.match(staticProfile.text, /name="confirm_password"/);
    assert.match(staticProfile.text, /name="show_username_history"/);
    assert.match(staticProfile.text, /data-username-history-list/);
    assert.match(staticProfile.text, /data-badges-list/);
    assert.match(staticProfile.text, /name="show_recent_servers"/);
    assert.match(staticProfile.text, /data-server-favorites-list/);
    assert.match(staticProfile.text, /data-server-history-list/);
    assert.match(staticProfile.text, /name="show_characters"/);
    assert.match(staticProfile.text, /data-characters-list/);
    assert.doesNotMatch(staticProfile.text, />\s*[^<]*Supabase[^<]*</i);
    const supabaseConfigured =
      /data-supabase-url="[^"]+"/.test(staticAccount.text) &&
      /data-supabase-key="[^"]+"/.test(staticAccount.text);
    const supabaseClientCount = (staticAccount.text.match(/@supabase\/supabase-js@2\.105\.0/g) || []).length;
    assert.equal(supabaseClientCount, supabaseConfigured ? 1 : 0);
    assert.doesNotMatch(staticAccount.text, /supabase\.min\.js/);
    const profileIcons = [
      "armorer.png",
      "hacker.png",
      "rifleman.png",
      "medic.png",
      "scrapper.png",
      "cap_collector.png",
    ];
    for (const icon of profileIcons) {
      assert.ok(staticProfile.text.includes(`/assets/profile-icons/${icon}`), `missing real perk icon ${icon}`);
      const iconPath = path.join(dist, "assets/profile-icons", icon);
      assert.equal(fs.existsSync(iconPath), true, `missing fetched perk icon ${icon}`);
      assert.deepEqual([...fs.readFileSync(iconPath).subarray(0, 8)], [137, 80, 78, 71, 13, 10, 26, 10]);
    }
    assert.doesNotMatch(staticProfile.text, /profile-icons\/[^"]+\.svg/);
    const factionIcons = ["Brotherhood", "Institute", "Minutemen", "Railroad"];
    for (const name of factionIcons) {
      const icon = `Icon__${name}.png`;
      assert.ok(staticProfile.text.includes(`/assets/profile-images/${icon}`), `missing faction icon ${icon}`);
      const iconPath = path.join(dist, "assets/profile-images", icon);
      assert.equal(fs.existsSync(iconPath), true, `missing deployed faction icon ${icon}`);
      assert.deepEqual([...fs.readFileSync(iconPath).subarray(0, 8)], [137, 80, 78, 71, 13, 10, 26, 10]);
    }
    const iconFetcher = fs.readFileSync(path.join(root, "scripts/fetch-profile-icons.js"), "utf8");
    assert.match(iconFetcher, /918547cc872c3288122f9d15ed0416cf33aa8bbf/);
    const accountJs = fs.readFileSync(path.join(dist, "static/js/account.js"), "utf8");
    const profileJs = fs.readFileSync(path.join(dist, "static/js/profile.js"), "utf8");
    const profileSocialJs = fs.readFileSync(path.join(dist, "static/js/profile-social.js"), "utf8");
    const profileNotificationsJs = fs.readFileSync(path.join(dist, "static/js/profile-notifications.js"), "utf8");
    const profileBadgesJs = fs.readFileSync(path.join(dist, "static/js/profile-badges.js"), "utf8");
    const profileServersJs = fs.readFileSync(path.join(dist, "static/js/profile-servers.js"), "utf8");
    const profileCharactersJs = fs.readFileSync(path.join(dist, "static/js/profile-characters.js"), "utf8");
    const memberCharactersJs = fs.readFileSync(path.join(dist, "static/js/member-characters.js"), "utf8");
    const memberServersJs = fs.readFileSync(path.join(dist, "static/js/member-servers.js"), "utf8");
    const serversFavoritesJs = fs.readFileSync(path.join(dist, "static/js/servers-favorites.js"), "utf8");
    const membersJs = fs.readFileSync(path.join(dist, "static/js/members.js"), "utf8");
    const memberJs = fs.readFileSync(path.join(dist, "static/js/member.js"), "utf8");
    const serversJs = fs.readFileSync(path.join(dist, "static/js/servers.js"), "utf8");
    const factionsJs = fs.readFileSync(path.join(dist, "static/js/factions.js"), "utf8");
    const factionJs = fs.readFileSync(path.join(dist, "static/js/faction.js"), "utf8");
    const factionApplyJs = fs.readFileSync(path.join(dist, "static/js/faction-apply.js"), "utf8");
    const navbarJs = fs.readFileSync(path.join(dist, "static/js/navbar.js"), "utf8");
    const siteShellCss = fs.readFileSync(path.join(dist, "static/css/site-shell.css"), "utf8");
    const discordLinkJs = fs.readFileSync(path.join(dist, "static/js/discord-link.js"), "utf8");
    assert.ok(discordLinkJs.includes("https://discord.gg/GyfxYG2gzH"));
    assert.match(discordLinkJs, /noopener noreferrer/);
    assert.equal(fs.existsSync(path.join(dist, "static/js/widgetbot.js")), false);
    assert.equal(fs.existsSync(path.join(dist, "static/css/widgetbot.css")), false);
    assert.doesNotMatch(discordLinkJs, /e\.widgetbot\.io/);
    assert.doesNotMatch(discordLinkJs, /createElement\("iframe"\)/);
    assert.doesNotMatch(discordLinkJs, /\bCrate\b/);
    assert.doesNotMatch(discordLinkJs, /\beval\s*\(/);
    assert.doesNotMatch(discordLinkJs, /new Function\s*\(/);
    assert.match(navbarJs, /getSession\(\)/);
    assert.match(navbarJs, /onAuthStateChange/);
    assert.match(navbarJs, /site-nav__profile/);
    assert.match(navbarJs, /data-account-nav-avatar/);
    assert.match(navbarJs, /\/profile\//);
    assert.match(navbarJs, /window\.coSupabase/);
    assert.doesNotMatch(navbarJs, /\.from\("profiles"\)/);
    assert.match(accountJs, /window\.coSupabase/);
    assert.doesNotMatch(accountJs, /linkIdentity/);
    assert.doesNotMatch(accountJs, /\.from\("profiles"\)/);
    assert.doesNotMatch(profileJs, /linkIdentity/);
    assert.doesNotMatch(profileJs, /\/auth\/v1\/settings/);
    assert.match(profileJs, /updateUser/);
    assert.match(profileJs, /currentPassword/);
    assert.match(profileJs, /\.from\("profiles"\)/);
    assert.match(profileSocialJs, /user_profile_details/);
    assert.match(profileSocialJs, /user_friendships/);
    assert.doesNotMatch(profileSocialJs, /user_notifications/);
    assert.match(profileNotificationsJs, /user_notifications/);
    assert.match(profileSocialJs, /user_username_history/);
    assert.match(profileSocialJs, /show_username_history/);
    assert.match(profileBadgesJs, /user_badge_assignments/);
    assert.match(profileBadgesJs, /displayedCount >= 3/);
    assert.match(profileServersJs, /user_server_favorites/);
    assert.match(profileServersJs, /user_server_history/);
    assert.match(memberServersJs, /get_public_recent_servers/);
    assert.match(profileCharactersJs, /user_characters/);
    assert.match(memberCharactersJs, /get_public_user_characters/);
    assert.match(serversFavoritesJs, /user_server_favorites/);
    assert.match(serversFavoritesJs, /onAuthStateChange/);
    assert.match(serversJs, /data-favorite-server/);
    assert.match(serversJs, /co:servers-rendered/);
    assert.match(profileSocialJs, /Decline/);
    assert.match(profileSocialJs, /status: "declined"/);
    assert.match(profileSocialJs, /Cancel request/);
    assert.match(profileNotificationsJs, /row\.actor\?\.avatar_url/);
    assert.match(profileNotificationsJs, /co:notifications-cleared/);
    assert.match(profileNotificationsJs, /window\.location\.assign\(item\.href\)/);
    assert.match(profileSocialJs, /co:notifications-refresh/);
    assert.match(profileSocialJs, /Promise\.all/);
    assert.match(membersJs, /user_profile_details!inner/);
    assert.match(membersJs, /user_presence/);
    assert.doesNotMatch(membersJs, /created_at/);
    assert.match(memberJs, /get_public_member_profile/);
    assert.match(memberJs, /get_public_member_friends/);
    assert.match(memberJs, /get_public_username_history/);
    assert.match(memberJs, /user_badge_assignments/);
    assert.match(memberJs, /is_displayed/);
    assert.match(memberJs, /user_friendships/);
    assert.match(memberJs, /user_blocks/);
    assert.match(memberJs, /status === "declined"/);
    assert.doesNotMatch(membersJs, /setInterval|setTimeout/);
    assert.doesNotMatch(memberJs, /setInterval/);
    assert.match(factionsJs, /\.from\("factions"\)/);
    assert.match(factionJs, /\.from\("faction_members"\)/);
    assert.match(factionApplyJs, /\.from\("faction_applications"\)/);
    assert.match(factionApplyJs, /\.insert\(\{ applicant_id: user\.id, \.\.\.payload \}\)/);
    assert.doesNotMatch(factionApplyJs, /const payload = \{[^}]*applicant_id:/);
    assert.match(factionApplyJs, /changes_requested/);
    assert.doesNotMatch(factionsJs, /setInterval|setTimeout/);
    assert.doesNotMatch(factionJs, /setInterval|setTimeout/);
    assert.doesNotMatch(factionApplyJs, /setInterval|setTimeout/);
    assert.match(siteShellCss, /body\.co-site\s*\{\s*background: #0d0e0f;/);
    assert.match(siteShellCss, /body\.co-site::before\s*\{\s*content: none;/);
    assert.doesNotMatch(siteShellCss, /radial-gradient|fractalNoise/);
    assert.doesNotMatch(accountJs, /\/auth\/v1\/settings/);
    assert.match(accountJs, /\/functions\/v1\/register-account/);
    assert.match(accountJs, /SIGNUP_COOLDOWN_MS/);
    assert.match(accountJs, /captchaToken/);
    assert.doesNotMatch(accountJs, /auth\.signUp\(/);
    assert.match(accountJs, /Account services are temporarily unavailable\./);
    assert.ok(profileJs.includes("/assets/profile-icons/armorer.png"));
    for (const name of factionIcons) {
      const icon = `/assets/profile-images/Icon__${name}.png`;
      assert.ok(profileJs.includes(icon), `profile.js missing faction icon ${icon}`);
      assert.ok(navbarJs.includes(icon), `navbar.js missing faction icon ${icon}`);
    }
    assert.doesNotMatch(accountJs, /Fallout_Perk_Planner/);
    assert.doesNotMatch(profileJs, /Fallout_Perk_Planner/);
    assert.ok(accountJs.includes('new URL(`${assetBase}/account/`, window.location.origin).href'));
    assert.doesNotMatch(accountJs, /new URL\("\\.", window\.location\.href\)/);
    assert.doesNotMatch(accountJs, /storage\.from/);
    const updatesJs = fs.readFileSync(path.join(dist, "static/js/updates.js"), "utf8");
    assert.doesNotMatch(updatesJs, /\/repo/, "updates.js must not reference the removed /repo page");
    assert.equal(fs.existsSync(path.join(dist, "repo/index.html")), false, "removed /repo page must not ship");
    assert.equal(fs.existsSync(path.join(dist, "static/js/repo.js")), false, "removed repo browser must not ship");
    assert.match(
      staticUpdates.text,
      /href="https:\/\/github\.com\/G-A-R-D-E-N\/Commonwealth-Online"[^>]*>Repository</,
      "Repository footer link must target the public GitHub repository"
    );
    const linksJs = fs.readFileSync(path.join(dist, "static/js/links.js"), "utf8");
    assert.match(
      linksJs,
      /repository: "https:\/\/github\.com\/G-A-R-D-E-N\/Commonwealth-Online"/,
      "Repository link registry must target the public GitHub repository"
    );
    assert.match(
      updatesJs,
      /https:\/\/github\.com\/G-A-R-D-E-N\/Commonwealth-Online/,
      "updates.js recovery link must point at GitHub"
    );
    assert.doesNotMatch(staticTeamForm.text, /discord-membership/);
    assert.doesNotMatch(staticTeamForm.text, /\/api\/v1/);
  } finally {
    await closeServer(staticServer);
  }

  console.log(`static checks passed: ${htmlFiles.length} HTML pages`);
};

run().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
