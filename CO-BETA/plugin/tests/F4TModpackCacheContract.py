from pathlib import Path

root = Path(__file__).resolve().parents[3]

cache_header = (root / "co-core/include/F4TModpackProbeCache.h").read_text(encoding="utf-8")
cache_source = (root / "co-core/src/manifest/F4TModpackProbeCache.cpp").read_text(encoding="utf-8")
cache_tests = (root / "co-core/tests/F4TModpackProbeCacheTests.cpp").read_text(encoding="utf-8")
core_xmake = (root / "co-core/xmake.lua").read_text(encoding="utf-8")
core_tests_xmake = (root / "co-core/xmake-tests.lua").read_text(encoding="utf-8") + (root / "co-core/xmake-tests-modpack.lua").read_text(encoding="utf-8")
plugin_xmake = (root / "CO-BETA/plugin/xmake.lua").read_text(encoding="utf-8")
store_header = (root / "CO-BETA/plugin/include/F4TServerModpackCache.h").read_text(encoding="utf-8")
store_source = (root / "CO-BETA/plugin/src/F4TServerModpackCache.cpp").read_text(encoding="utf-8")
store_tests = (root / "CO-BETA/plugin/tests/F4TServerModpackCacheTests.cpp").read_text(encoding="utf-8")
probe_header = (root / "CO-BETA/plugin/include/F4TClientModpackProbe.h").read_text(encoding="utf-8")
probe = (root / "CO-BETA/plugin/src/F4TClientModpackProbe.cpp").read_text(encoding="utf-8")
networking = (root / "CO-BETA/plugin/src/F4TNetworkingGns.cpp").read_text(encoding="utf-8")
shim_state = (root / "CO-BETA/plugin/src/F4TGnsWinsockShimState.h").read_text(encoding="utf-8")
shim_frames = (root / "CO-BETA/plugin/src/F4TGnsWinsockShimFrames.cpp").read_text(encoding="utf-8")
shim = (root / "CO-BETA/plugin/src/F4TGnsWinsockShim.cpp").read_text(encoding="utf-8")

for fragment in (
    "struct FileFact",
    "struct Entry",
    "enum class Match",
    "SerializeEntry",
    "ParseEntry",
    "Validate",
    "kMaximumBlobBytes",
    "kMissingFileSize",
):
    assert fragment in cache_header, f"F4TModpackProbeCache.h must define {fragment}"
for fragment in ("IsValidPath", "IsValidPluginName", "changedPlugins", "changedFile", "missingFile"):
    assert fragment in cache_source, f"F4TModpackProbeCache.cpp must implement {fragment}"
for fragment in ("TestRoundTrip", "TestSizeChange", "TestMtimeChange", "TestMissingFile", "TestPluginAdded",
                 "TestPluginRemoved", "TestPluginOrderChanged", "TestMalformedEntryIsRejected"):
    assert fragment in cache_tests, f"F4TModpackProbeCacheTests.cpp must cover {fragment}"
assert "tests/F4TModpackProbeCacheTests.cpp" in core_tests_xmake, "the cache unit test must be registered"
assert "src/manifest/F4TModpackProbeCache.cpp" in core_xmake, "the cache source must be in CoreLib"
assert "include/F4TModpackProbeCache.h" in core_xmake, "the cache header must be in CoreLib"

for fragment in ("Lookup", "Store", "Clear", "SetCachePathForTesting", "modpack-cache.json"):
    assert fragment in store_header + store_source, f"the per-server cache store must handle {fragment}"
assert "F4T_SERVER_MODPACK_CACHE_TESTING" in plugin_xmake, "the cache store test target must use the test define"
assert "F4TServerModpackCacheTests.cpp" in plugin_xmake
for fragment in ("kBlob", "assert(!Store", "assert(Clear"):
    assert fragment in store_tests, f"the cache store test must cover {fragment}"
assert "kMaximumServers" in store_source and "kMaximumFileBytes" in store_source, (
    "the cache store must bound servers and file size"
)

for fragment in ("CachedFingerprint", "RememberMatch", "Forget", "fileFacts", "loadedPlugins"):
    assert fragment in probe_header, f"F4TClientModpackProbe.h must expose {fragment}"
for fragment in (
    "ServerModpackCache::Lookup",
    "ServerModpackCache::Store",
    "ServerModpackCache::Clear",
    "ModpackProbeCache::Validate",
    "ModpackProbeCache::Match::hit",
    "LastWriteTicks",
    "result.fileFacts.push_back",
):
    assert fragment in probe, f"F4TClientModpackProbe.cpp must use {fragment}"
assert "RememberMatch(std::string{ a_serverKey }, *matching, probed)" in probe, (
    "a matching probe must refresh the per-server cache"
)

cache_lookup = networking.index("F4T::ClientModpackProbe::CachedFingerprint(serverKey)")
first_connect = networking.index("ConnectAndValidateServerOnce(a_host, a_port, a_error, &failure, a_serverPassword, cachedFingerprint, a_joinId)")
assert cache_lookup < first_connect, "a validated cached fingerprint must be sent on the FIRST connect"
assert "staleCacheRejected" in networking and "F4T::ClientModpackProbe::Forget(serverKey)" in networking, (
    "a cached fingerprint the server still rejects must be dropped, then the normal probe retried once"
)
assert networking.count("a_serverPassword, cachedFingerprint, a_joinId)") == 2, (
    "the connect wrapper keeps exactly the first attempt plus one equivalent retry"
)
assert networking.count("a_serverPassword, matching, a_joinId)") == 1, (
    "the modpack retry happens exactly once, with the fingerprint the server reported"
)
assert networking.count("ConnectAndValidateServerOnce") == 4, (
    "the wrapper has the first attempt, one modpack retry and one bounded duplicate-identity retry"
)

assert "std::string serverKey;" in shim_state
assert "state->serverKey = serverKey;" in shim
assert "MakeSessionRejectionFailure(rejection, a_state->serverKey)" in shim_frames, (
    "the rejection handler must pass the server key so a match can be cached"
)

for compile_time_switch in ("#if", "F4T_MODPACK_CACHE"):
    assert compile_time_switch not in probe, "the probe cache must always be on, with no compile-time switch"

print("Modpack probe cache contract tests passed")
