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
  { key: "forum", label: "Forum", href: "/forum", feature: "forum" },
  { key: "applications", label: "Apply", href: "/apply", feature: "applications" },
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
  { label: "Media", key: "media" },
  { label: "Servers", key: "servers" },
  { label: "Updates", key: "updates" },
];

module.exports = { NAV_ITEMS, FOOTER_LINKS, getNavItems };
