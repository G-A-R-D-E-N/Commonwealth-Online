"use strict";

/**
 * Navigation model shared by the header partial and the page routes.
 *
 * Items marked with `feature` only render once the matching feature flag is
 * enabled in config, so unshipped surfaces stay out of the nav.
 */

const config = require("../config");

const NAV_ITEMS = [
  { key: "home", label: "Home", href: "/" },
  { key: "media", label: "Media", href: "/media" },
  { key: "servers", label: "Servers", href: "/servers" },
  { key: "roadmap", label: "Roadmap", href: "/roadmap" },
  { key: "forum", label: "Forum", href: "/forum", feature: "forum" },
  { key: "factions", label: "Factions", href: "/factions" },
  { key: "applications", label: "Apply", href: "/apply", feature: "applications" },
  { key: "members", label: "Members", href: "/members" },
  { key: "account", label: "Login / Sign Up", href: "/account" },
  { key: "repository", label: "Repository", href: config.links.repository, external: true },
  { key: "updates", label: "Updates", href: "/updates" },
];

const getNavItems = () =>
  NAV_ITEMS.filter((item) => !item.feature || config.features[item.feature]).map(
    ({ feature, ...item }) => item
  );

/** Default footer links; pages can override with `page.footerLinks`. */
const FOOTER_LINKS = [
  { label: "Repository", key: "repository" },
  { label: "Discord", key: "discord" },
  { label: "Apply", key: "applications" },
  { label: "Account", href: "/account" },
  { label: "Factions", href: "/factions" },
  { label: "Members", href: "/members" },
  { label: "Media", key: "media" },
  { label: "Servers", key: "servers" },
  { label: "Roadmap", href: "/roadmap" },
  { label: "Updates", key: "updates" },
];

module.exports = { NAV_ITEMS, FOOTER_LINKS, getNavItems };
