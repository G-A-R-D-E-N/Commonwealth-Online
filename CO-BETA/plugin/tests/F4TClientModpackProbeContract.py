from pathlib import Path

root = Path(__file__).resolve().parents[3]

manifest_header = (root / "co-core/include/F4TModpackManifest.h").read_text(encoding="utf-8")
manifest_source = (root / "co-core/src/manifest/F4TModpackManifest.cpp").read_text(encoding="utf-8")
probe_header = (root / "co-core/include/F4TModpackProbe.h").read_text(encoding="utf-8")
probe_source = (root / "co-core/src/manifest/F4TModpackProbe.cpp").read_text(encoding="utf-8")
plugin_probe = (root / "CO-BETA/plugin/src/F4TClientModpackProbe.cpp").read_text(encoding="utf-8")
load_order = (root / "CO-BETA/plugin/src/F4TLoadOrderSync.cpp").read_text(encoding="utf-8")
main_source = (root / "CO-BETA/plugin/src/main.cpp").read_text(encoding="utf-8")
detail_js = (root / "CommonwealthOnlineUI/ui/views/CommonwealthOnline/browser/components/modpack-detail.js").read_text(encoding="utf-8")

assert "archive" in manifest_header, "the manifest entry model must define an archive kind"
assert "wrongArchive" in manifest_header, "the diff must classify archive content"
for fragment in ('"archive"', "EntryKind::archive", "Wrong archive:", "wrongArchive"):
    assert fragment in manifest_source, f"F4TModpackManifest.cpp must implement {fragment}"

for fragment in ("RankRequiredPlugins", "ContiguousOrdersFromRanks", "FindExtraPlugins", "FindExtraNatives", "kMissingPluginOrder", "PluginTypeFromName"):
    assert fragment in probe_header, f"F4TModpackProbe.h must declare {fragment}"
for fragment in ("NormalizeManifest", "unordered_map", "unordered_set", "LowerAscii", "ContiguousOrdersFromRanks", "stable_sort"):
    assert fragment in probe_source, f"F4TModpackProbe.cpp must use {fragment}"

for fragment in (
    "F4T::LoadOrderSync::LoadedPluginNames()",
    "F4T::ModpackProbe::RankRequiredPlugins",
    "F4T::ModpackProbe::FindExtraPlugins",
    "F4T::ModpackProbe::FindExtraNatives",
    "std::filesystem::status(a_path, error)",
    "GameDataDirectory()",
    "GetModuleFileNameW(nullptr",
    "kExtraNativesHeader",
    "kLoadOrderUnavailable",
    "rank = ranks[nextRank++];",
    "acceptedRanks.push_back(rank);",
    "F4T::ModpackProbe::ContiguousOrdersFromRanks(acceptedRanks)",
    "probedEntry.order = orders[pluginIndex++];",
):
    assert fragment in plugin_probe, f"F4TClientModpackProbe.cpp must use {fragment}"
assert "item.entry->order = nextOrder++" not in plugin_probe, (
    "required plugins must take their order from the client's loaded rank, not the server's manifest position"
)
assert "symlink_status" not in plugin_probe, (
    "the probe must follow symlinks and junctions so Vortex deploys count, while still rejecting broken links "
    "and links to non-regular files"
)
for compile_time_switch in ("#if", "F4T_MODPACK"):
    assert compile_time_switch not in plugin_probe, "the modpack gate must always be on, with no compile-time switch"

assert "void CaptureLoadedPlugins()" in load_order, "the plugin list must be captured on the game thread"
assert "std::scoped_lock" in load_order, "the captured plugin list must be shared under a lock"
assert "F4T::LoadOrderSync::CaptureLoadedPlugins();" in main_source, (
    "kGameDataReady must capture the loaded plugin list so the probe never reads the engine off the game thread"
)

for fragment in (
    "Wrong load order",
    "Unexpected",
    "co-modpack-detail-entry--order",
    "Extra F4SE plugins (allowed, not required):",
):
    assert fragment in detail_js, f"modpack-detail.js must show {fragment}"

print("Client modpack probe contract tests passed")
