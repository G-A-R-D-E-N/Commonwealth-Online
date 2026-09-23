"use strict";

/**
 * Navigation model shared by the header partial and the page routes.
 *
 * Items marked with `feature` only render once the matching feature flag is
 * enabled in config, so unshipped surfaces stay out of the nav. Top-level
 * items with `children` render as disclosure menus; the active child marks
 * its parent group active.
 */

const config = require("../config");

const NAV_ITEMS = [
  { key: "home", label: "Home", href: "/" },
  {
    key: "media",
    label: "Media",
    children: [
      { key: "media", label: "Gallery", href: "/media" },
      { key: "updates", label: "Updates", href: "/updates" },
      { key: "roadmap", label: "Roadmap", href: "/roadmap" },
    ],
  },
  { key: "servers", label: "Servers", href: "/servers" },
  { key: "hosting", label: "Hosting", href: "/hosting" },
  {
    key: "community",
    label: "Community",
    children: [
      { key: "forum", label: "Forum", href: "/forum", feature: "forum" },
      { key: "factions", label: "Factions", href: "/factions" },
      { key: "members", label: "Members", href: "/members" },
    ],
  },
  {
    key: "project",
    label: "Project",
    children: [
      { key: "repository", label: "Repository", href: config.links.repository, external: true },
      { key: "applications", label: "Apply to Join", href: "/apply", feature: "applications" },
    ],
  },
  { key: "account", label: "Account", href: "/account" },
];

const prune = (items) =>
  items
    .filter((item) => !item.feature || config.features[item.feature])
    .map(({ feature, children, ...item }) => ({
      ...item,
      ...(children ? { children: prune(children) } : {}),
    }))
    .filter((item) => !item.children || item.children.length > 0);

const getNavItems = () => prune(NAV_ITEMS);

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
  { label: "Hosting", href: "/hosting" },
  { label: "Roadmap", href: "/roadmap" },
  { label: "Updates", key: "updates" },
];

module.exports = { NAV_ITEMS, FOOTER_LINKS, getNavItems };
