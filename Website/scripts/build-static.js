"use strict";

const fs = require("node:fs");
const path = require("node:path");
const ejs = require("ejs");

const config = require("../src/config");
const { FOOTER_LINKS, getNavItems } = require("../src/lib/nav");
const { PUBLIC_PAGES, STATIC_SHELL_PAGES } = require("../src/routes/page-config");

const root = path.resolve(__dirname, "..");
const dist = path.join(root, "dist");
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

fs.rmSync(dist, { recursive: true, force: true });
fs.mkdirSync(dist, { recursive: true });

for (const definition of pages) {
  const templatePath = path.join(root, "views", `${definition.template}.ejs`);
  const outputPath = path.join(dist, definition.output);
  fs.mkdirSync(path.dirname(outputPath), { recursive: true });
  const template = fs.readFileSync(templatePath, "utf8");
  fs.writeFileSync(outputPath, ejs.render(template, localsFor(definition.page), { filename: templatePath }));
}

fs.cpSync(path.join(root, "assets"), path.join(dist, "assets"), { recursive: true });
fs.cpSync(path.join(root, "static"), path.join(dist, "static"), { recursive: true });
fs.mkdirSync(path.join(dist, "data"), { recursive: true });
fs.copyFileSync(path.join(root, "servers", "server.json"), path.join(dist, "data", "servers.json"));

console.log(`Static website built at ${dist}`);
