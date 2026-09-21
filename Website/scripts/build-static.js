"use strict";

const fs = require("node:fs");
const path = require("node:path");
const ejs = require("ejs");

const config = require("../src/config");
const { FOOTER_LINKS, getNavItems } = require("../src/lib/nav");
const { getType } = require("../src/lib/applications");
const { PUBLIC_PAGES, STATIC_SHELL_PAGES } = require("../src/routes/page-config");

const root = path.resolve(__dirname, "..");
const changelogDir = path.resolve(root, "..", "changelogs");
const dist = path.join(root, "dist");
const staticBasePath = (process.env.STATIC_BASE_PATH || "").replace(/\/+$/, "");
const site = {
  name: "Commonwealth Online",
  url: config.siteUrl,
  links: config.links,
  supabase: config.supabase,
  captcha: config.captcha,
};

const localsFor = (page) => ({
  page,
  site,
  navItems: getNavItems(),
  defaultFooterLinks: FOOTER_LINKS,
  features: config.features,
  year: new Date().getFullYear(),
});
const pages = [...PUBLIC_PAGES, ...STATIC_SHELL_PAGES];
const withStaticBasePath = (html) =>
  staticBasePath
    ? html.replace(/(\b(?:href|src|action|data-thanks-url)=")\/(?!\/)/g, `$1${staticBasePath}/`)
    : html;

const readChangelogs = () => {
  if (!fs.existsSync(changelogDir)) {
    return [];
  }

  return fs
    .readdirSync(changelogDir)
    .filter((fileName) => fileName.endsWith(".md"))
    .map((fileName) => {
      const source = fs.readFileSync(path.join(changelogDir, fileName), "utf8");
      const match = source.match(/^---\r?\n([\s\S]*?)\r?\n---\r?\n([\s\S]*)$/);
      if (!match) {
        throw new Error(`Changelog is missing front matter: ${fileName}`);
      }

      const metadata = {};
      for (const line of match[1].split(/\r?\n/).filter(Boolean)) {
        const separator = line.indexOf(":");
        if (separator < 1) {
          throw new Error(`Invalid changelog metadata: ${fileName}`);
        }
        const key = line.slice(0, separator).trim();
        const value = line.slice(separator + 1).trim().replace(/^("|')|("|')$/g, "");
        metadata[key] = value;
      }

      if (!metadata.version || !metadata.date) {
        throw new Error(`Changelog needs version and date: ${fileName}`);
      }

      return {
        version: metadata.version,
        date: metadata.date,
        title: metadata.title || `Commonwealth Online ${metadata.version}`,
        github: metadata.github || "",
        nexus: metadata.nexus || "",
        markdown: match[2].trim(),
      };
    })
    .sort((left, right) => right.date.localeCompare(left.date));
};

fs.rmSync(dist, { recursive: true, force: true });
fs.mkdirSync(dist, { recursive: true });

const writePage = (templateName, output, locals) => {
  const templatePath = path.join(root, "views", `${templateName}.ejs`);
  const outputPath = path.join(dist, output);
  fs.mkdirSync(path.dirname(outputPath), { recursive: true });
  const template = fs.readFileSync(templatePath, "utf8");
  const html = ejs.render(template, locals, { filename: templatePath });
  fs.writeFileSync(outputPath, withStaticBasePath(html));
};

const writeRedirect = (output, target) => {
  const outputPath = path.join(dist, output);
  const href = `${staticBasePath}${target}`;
  fs.mkdirSync(path.dirname(outputPath), { recursive: true });
  fs.writeFileSync(
    outputPath,
    `<!doctype html><meta charset="utf-8"><meta http-equiv="refresh" content="0;url=${href}"><link rel="canonical" href="${href}"><title>Redirecting</title><a href="${href}">Continue</a>`
  );
};

for (const definition of pages) {
  writePage(definition.template, definition.output, localsFor(definition.page));
}

writeRedirect("factions/review/index.html", "/profile/?staff=factions");
writeRedirect("faction/manage/index.html", "/profile/");

const applicationScripts = [
  "/static/js/links.js",
  "/static/js/navbar.js",
  "/static/js/script.js",
  "/static/js/applications.js",
];

for (const type of ["team", "beta"]) {
  const form = getType(type);
  writePage("pages/apply-form", `apply/${type}/index.html`, {
    ...localsFor({
      title: `${form.title} - Commonwealth Online`,
      description: form.description,
      bodyClass: "co-apply-page",
      activeKey: "applications",
      scripts: applicationScripts,
    }),
    form,
    values: {},
    errors: [],
    errorMap: {},
    notice: null,
    submitAction: "#",
    thanksUrl: "/apply/thanks/",
  });
}

writePage("pages/apply-thanks", "apply/thanks/index.html", {
  ...localsFor({
    title: "Application received - Commonwealth Online",
    description: "Your Commonwealth Online application has been received.",
    bodyClass: "co-apply-page",
    activeKey: "applications",
    scripts: ["/static/js/links.js", "/static/js/navbar.js", "/static/js/script.js", "/static/js/apply-thanks.js"],
  }),
  reference: null,
});

fs.cpSync(path.join(root, "assets"), path.join(dist, "assets"), { recursive: true });
fs.cpSync(path.join(root, "static"), path.join(dist, "static"), { recursive: true });
fs.mkdirSync(path.join(dist, "data"), { recursive: true });
fs.copyFileSync(path.join(root, "servers", "server.json"), path.join(dist, "data", "servers.json"));
fs.writeFileSync(path.join(dist, "data", "changelogs.json"), `${JSON.stringify(readChangelogs(), null, 2)}\n`);

if (staticBasePath) {
  const linksPath = path.join(dist, "static", "js", "links.js");
  const links = fs.readFileSync(linksPath, "utf8").replace(
    /(:\s*")\/(?!\/)/g,
    `$1${staticBasePath}/`
  );
  fs.writeFileSync(linksPath, links);

  const serversPath = path.join(dist, "static", "js", "servers.js");
  const servers = fs
    .readFileSync(serversPath, "utf8")
    .replace(
      'const DATA_URL = "/data/servers.json";',
      `const DATA_URL = "${staticBasePath}/data/servers.json";`
    );
  fs.writeFileSync(serversPath, servers);

  const updatesPath = path.join(dist, "static", "js", "updates.js");
  const updates = fs
    .readFileSync(updatesPath, "utf8")
    .replace(
      'const DATA_URL = "/data/changelogs.json";',
      `const DATA_URL = "${staticBasePath}/data/changelogs.json";`
    );
  fs.writeFileSync(updatesPath, updates);
}

console.log(`Static website built at ${dist}`);
