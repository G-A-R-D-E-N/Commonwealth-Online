"use strict";

const fs = require("node:fs");
const path = require("node:path");
const ejs = require("ejs");

const config = require("../src/config");
const { FOOTER_LINKS, getNavItems } = require("../src/lib/nav");
const { PUBLIC_PAGES, STATIC_SHELL_PAGES } = require("../src/routes/page-config");

const root = path.resolve(__dirname, "..");
const dist = path.join(root, "dist");
const staticBasePath = (process.env.STATIC_BASE_PATH || "").replace(/\/+$/, "");
const site = {
  name: "Commonwealth Online",
  url: config.siteUrl,
  links: config.links,
  gitea: config.gitea,
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
    ? html.replace(/(\b(?:href|src|action)=")\/(?!\/)/g, `$1${staticBasePath}/`)
    : html;

fs.rmSync(dist, { recursive: true, force: true });
fs.mkdirSync(dist, { recursive: true });

for (const definition of pages) {
  const templatePath = path.join(root, "views", `${definition.template}.ejs`);
  const outputPath = path.join(dist, definition.output);
  fs.mkdirSync(path.dirname(outputPath), { recursive: true });
  const template = fs.readFileSync(templatePath, "utf8");
  const html = ejs.render(template, localsFor(definition.page), { filename: templatePath });
  fs.writeFileSync(outputPath, withStaticBasePath(html));
}

fs.cpSync(path.join(root, "assets"), path.join(dist, "assets"), { recursive: true });
fs.cpSync(path.join(root, "static"), path.join(dist, "static"), { recursive: true });
fs.mkdirSync(path.join(dist, "data"), { recursive: true });
fs.copyFileSync(path.join(root, "servers", "server.json"), path.join(dist, "data", "servers.json"));

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
}

console.log(`Static website built at ${dist}`);
