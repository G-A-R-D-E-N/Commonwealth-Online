includes(path.join(os.scriptdir(), "lib/commonlibf4"))

-- The Commonwealth Online client version is defined in exactly one
-- authoritative place for the wire/handshake: F4T::ClientCompatibility::
-- kClientVersion in co-core (issue #50). xmake's configure sandbox cannot read
-- that header portably across the CI-pinned xmake versions, so this project's
-- F4SE build/shipping metadata mirrors the same value below; keep the two in
-- step (the server checks the network clientVersion, which always comes from
-- co-core, so a mismatch here can only age F4SE metadata, never the handshake).
set_project("CommonwealthOnline")
set_version("0.1.0")
set_license("GPL-3.0")
set_languages("c++23")
set_policy("build.intermediate_directory", false)
set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)")
if is_plat("windows") and not is_host("windows") then
    add_cxxflags("/std:c++23preview", {force = true})
end
if is_plat("linux") then
    set_toolchains("clang")
end
set_warnings("allextra")

add_rules("mode.debug", "mode.releasedbg")
add_rules("plugin.vsxmake.autoupdate")
add_requires("nlohmann_json v3.12.0")
if is_plat("windows") then
    add_requires("libopus 1.5.2", {configs = {runtimes = "MD"}})
    add_requires("safetyhook v0.7.0")
else
    add_requires("libopus 1.5.2")
end

includes("../../co-core/xmake.lua")
includes("xmake-voice.lua")
includes("xmake-quest-tests.lua")

local co_core_dir = path.join(os.scriptdir(), "../../co-core")
local co_core_include = path.join(co_core_dir, "include")
local commonlib_include = path.join(os.scriptdir(), "lib/commonlibf4/include")
local gns_include = path.join(os.scriptdir(), "../ThirdParty/GameNetworkingSockets-src/include")
local networking_legacy_defines = {
    "ConnectAndValidateServer=ConnectAndValidateServerLegacy"
}

local function add_steam_tunnel_includes()
    if os.isdir(gns_include) then
        add_includedirs(gns_include)
    end
end

local function add_client_sources()
    add_files("src/**.cpp|F4TNetworking.cpp|F4TNetworkingOutbound.cpp|F4TNetworkingSocketLifecycle.cpp|F4TProbe*.cpp")
    add_files("src/F4TNetworking.cpp", {
        defines = networking_legacy_defines,
        forceincludes = "F4TNetworkingGnsCompile.h"
    })
    add_files("src/F4TNetworkingSocketLifecycle.cpp", {
        defines = networking_legacy_defines,
        forceincludes = "F4TNetworkingGnsCompile.h"
    })
    add_files("src/F4TNetworkingOutbound.cpp", {
        defines = networking_legacy_defines,
        forceincludes = "F4TNetworkingGnsCompile.h"
    })
end

target("CommonwealthOnline")
    add_rules("commonlibf4.plugin", {
        name = "CommonwealthOnline",
        author = "Commonwealth Online",
        description = "Commonwealth Online F4SE plugin using CommonLibF4",
        plugin_template = "res/commonwealthonline-plugin.cpp.in"
    })

    add_deps("CommonwealthOnlineCore")
    add_defines("F4T_TARGET_AE=1")
    add_client_sources()
    remove_files("src/F4TOGPluginExports.cpp")
    add_headerfiles("src/**.h")
    add_headerfiles("include/**.h")
    add_includedirs("src")
    add_includedirs("include", co_core_include)
    add_steam_tunnel_includes()
    add_packages("nlohmann_json", "safetyhook", "libopus")
    add_syslinks("ws2_32", "iphlpapi", "shell32", "crypt32", "ole32", "avrt", "winmm")
    add_syslinks("advapi32")
    set_pcxxheader("src/pch.h")

    after_build(function (target) os.cp(path.join(os.scriptdir(), "res/flyover"), path.join(target:targetdir(), "flyover")) end)

target("CommonwealthOnlineNG")
    add_rules("commonlibf4.plugin", {
        name = "CommonwealthOnlineNG",
        author = "Commonwealth Online",
        description = "Commonwealth Online F4SE plugin using CommonLibF4",
        plugin_template = "res/commonwealthonline-plugin.cpp.in"
    })

    set_basename("CommonwealthOnline-NG")
    add_deps("CommonwealthOnlineCore")
    add_defines("F4T_TARGET_NG=1")
    add_client_sources()
    add_headerfiles("src/**.h")
    add_headerfiles("include/**.h")
    add_includedirs("src")
    add_includedirs("include", co_core_include)
    add_steam_tunnel_includes()
    add_packages("nlohmann_json", "safetyhook", "libopus")
    add_syslinks("ws2_32", "iphlpapi", "shell32", "crypt32", "ole32", "avrt", "winmm")
    add_syslinks("advapi32")
    set_pcxxheader("src/pch.h")

    after_build(function (target) os.cp(path.join(os.scriptdir(), "res/flyover"), path.join(target:targetdir(), "flyover")) end)

target("CommonwealthOnlineOG")
    add_rules("commonlibf4.plugin", {
        name = "CommonwealthOnlineOG",
        author = "Commonwealth Online",
        description = "Commonwealth Online F4SE plugin using CommonLibF4",
        plugin_template = "res/commonwealthonline-plugin.cpp.in"
    })

    set_basename("CommonwealthOnline-OG")
    add_deps("CommonwealthOnlineCore")
    add_defines("F4T_TARGET_OG=1")
    add_client_sources()
    remove_files("src/F4TOGPluginExports.cpp")
    add_headerfiles("src/**.h")
    add_headerfiles("include/**.h")
    add_includedirs("src")
    add_includedirs("include", co_core_include)
    add_steam_tunnel_includes()
    add_packages("nlohmann_json", "safetyhook", "libopus")
    add_syslinks("ws2_32", "iphlpapi", "shell32", "crypt32", "ole32", "avrt", "winmm")
    add_syslinks("advapi32")
    set_pcxxheader("src/pch.h")

    after_build(function (target) os.cp(path.join(os.scriptdir(), "res/flyover"), path.join(target:targetdir(), "flyover")) end)

target("CommonwealthOnlineProbe")
    add_rules("commonlibf4.plugin", {
        name = "CommonwealthOnlineProbe",
        author = "Commonwealth Online",
        description = "Non-shipping RE observation probe for Commonwealth Online",
        plugin_template = "res/commonwealthonline-probe.cpp.in"
    })

    set_default(false)
    add_deps("CommonwealthOnlineCore")
    add_files(
        "src/F4TProbePlugin.cpp",
        "src/F4TProbeManifest.cpp",
        "src/F4TProbeVersionGate.cpp",
        "src/F4TProbeCapture.cpp",
        "src/F4TProbeRegistry.cpp",
        "src/F4TProbeSites.cpp",
        "src/F4TInlineHook.cpp",
        "src/F4TProbeFlush.cpp",
        "src/F4TGamePaths.cpp",
        "src/F4TF4SECapabilities.cpp",
        "src/F4TDebugLogging.cpp")
    add_headerfiles("include/F4TProbe*.h")
    add_headerfiles("include/F4TGamePaths.h")
    add_includedirs("src", "include", co_core_include)
    add_defines("WIN32_LEAN_AND_MEAN", "NOMINMAX")
    add_packages("nlohmann_json", "safetyhook")
    add_syslinks("ntdll")

    after_build(function (target)
        os.cp(path.join(os.scriptdir(), "res/CommonwealthOnlineProbe.json"), target:targetdir())
    end)

target("CommonwealthOnlineNpcSyncCompileCheck")
    set_kind("object")
    set_default(false)
    add_deps("CommonwealthOnlineCore", "commonlibf4")
    add_defines("F4T_TARGET_AE=1")
    add_files(
        "src/F4TNpcSync.cpp",
        "src/F4TNpcSyncGns.cpp",
        "src/F4TNpcSyncPuppets.cpp",
        "src/F4TNpcSyncHost.cpp",
        "src/F4TNpcSyncSuppress.cpp",
        "src/F4TNpcSyncRegistry.cpp",
        "src/F4TNpcSyncHealth.cpp",
        "src/F4TNpcSyncLoot.cpp",
        "src/F4TNpcSyncLootTake.cpp",
        "src/F4TNpcSyncLootMirror.cpp",
        "src/F4TNpcSyncLootCapture.cpp",
        "src/F4TNpcSyncCombat.cpp",
        "src/F4TNpcActorGuard.cpp",
        "src/F4TNpcCombatCapture.cpp",
        "src/F4TNetworkingNpcCoop.cpp",
        "src/F4TNetworkingNpcLoot.cpp",
        "src/F4TProxyPuppet.cpp")
    add_includedirs("src")
    add_includedirs("include")
    add_packages("nlohmann_json", "safetyhook")
    set_pcxxheader("src/pch.h")

target("CommonwealthOnlineSteamApiLoaderCompileCheck")
    set_kind("object")
    set_default(false)
    add_deps("CommonwealthOnlineCore")
    add_files("src/F4TSteamApiLoader.cpp")
    add_includedirs("src", "include", co_core_include)
    add_steam_tunnel_includes()

target("CommonwealthOnlineSteamApiLoaderTests")
    set_kind("binary")
    add_deps("CommonwealthOnlineCore")
    add_files(
        "src/F4TSteamApiLoader.cpp",
        "src/F4TSteamTunnelService.cpp",
        "src/F4TSteamHostLinkClient.cpp",
        "src/F4TSteamInvite.cpp",
        "src/F4TWinsockLoopbackUdp.cpp",
        "tests/F4TSteamApiLoaderTests.cpp")
    add_includedirs("src", "include", co_core_include)
    add_steam_tunnel_includes()
    if is_plat("windows") then
        add_syslinks("ws2_32")
    end
    add_forceincludes("F4TAssertionsEnabled.h")
    add_undefines("NDEBUG")
    add_tests("default")

target("CommonwealthOnlineWinsockLoopbackUdpTests")
    set_kind("binary")
    add_deps("CommonwealthOnlineCore")
    add_files("src/F4TWinsockLoopbackUdp.cpp", "tests/F4TWinsockLoopbackUdpTests.cpp")
    add_includedirs("src", "include", co_core_include)
    add_steam_tunnel_includes()
    if is_plat("windows") then
        add_syslinks("ws2_32")
    end
    add_forceincludes("F4TAssertionsEnabled.h")
    add_undefines("NDEBUG")
    add_tests("default")

target("CommonwealthOnlineSteamTunnelServiceTests")
    set_kind("binary")
    add_deps("CommonwealthOnlineCore")
    add_files(
        "src/F4TSteamApiLoader.cpp",
        "src/F4TSteamTunnelService.cpp",
        "src/F4TSteamHostLinkClient.cpp",
        "src/F4TSteamInvite.cpp",
        "src/F4TWinsockLoopbackUdp.cpp",
        "tests/F4TSteamTunnelServiceTests.cpp")
    add_includedirs("src", "include", co_core_include)
    add_steam_tunnel_includes()
    if is_plat("windows") then
        add_syslinks("ws2_32")
    end
    add_forceincludes("F4TAssertionsEnabled.h")
    add_undefines("NDEBUG")
    add_tests("default")

target("CommonwealthOnlineSteamHostLinkClientTests")
    set_kind("binary")
    add_deps("CommonwealthOnlineCore")
    add_files("src/F4TSteamHostLinkClient.cpp", "tests/F4TSteamHostLinkClientTests.cpp")
    add_includedirs("src", "include", co_core_include)
    add_steam_tunnel_includes()
    if is_plat("windows") then
        add_syslinks("ws2_32")
    end
    add_forceincludes("F4TAssertionsEnabled.h")
    add_undefines("NDEBUG")
    add_tests("default")

target("CommonwealthOnlineServerModpackCacheTests")
    set_kind("binary")
    add_deps("CommonwealthOnlineCore")
    add_files(
        "src/F4TServerModpackCache.cpp",
        "tests/F4TServerModpackCacheTests.cpp")
    add_defines("F4T_SERVER_MODPACK_CACHE_TESTING=1")
    add_includedirs("src", "include", co_core_include)
    add_packages("nlohmann_json")
    add_forceincludes("F4TAssertionsEnabled.h")
    add_undefines("NDEBUG")
    add_tests("default")

target("CommonwealthOnlineServerProfileStoreTests")
    set_kind("binary")
    add_deps("CommonwealthOnlineCore")
    add_files(
        "src/F4TResumeTokenProtection.cpp",
        "src/F4TServerKey.cpp",
        "src/F4TServerProfileStore.cpp",
        "tests/F4TServerProfileStoreTests.cpp")
    add_defines("F4T_SERVER_PROFILE_STORE_TESTING=1")
    add_includedirs("src", "include", co_core_include)
    add_packages("nlohmann_json")
    if is_plat("windows") then
        add_syslinks("crypt32")
    end
    add_forceincludes("F4TAssertionsEnabled.h")
    add_undefines("NDEBUG")
    add_tests("default")

target("CommonwealthOnlineServerKeyTests")
    set_kind("binary")
    add_files("src/F4TServerKey.cpp", "tests/F4TServerKeyTests.cpp")
    add_includedirs("src", "include", co_core_include)
    add_forceincludes("F4TAssertionsEnabled.h")
    add_undefines("NDEBUG")
    add_tests("default")

target("CommonwealthOnlineSaveLineageStoreTests")
    set_kind("binary")
    add_deps("CommonwealthOnlineCore")
    add_files(
        "src/F4TResumeTokenProtection.cpp",
        "src/F4TServerKey.cpp",
        "src/F4TServerProfileStore.cpp",
        "src/F4TSaveLineageStore.cpp",
        "tests/F4TSaveLineageStoreTests.cpp")
    add_defines("F4T_SERVER_PROFILE_STORE_TESTING=1")
    add_includedirs("src", "include", co_core_include)
    add_packages("nlohmann_json")
    if is_plat("windows") then
        add_syslinks("crypt32")
    end
    add_forceincludes("F4TAssertionsEnabled.h")
    add_undefines("NDEBUG")
    add_tests("default")

target("CommonwealthOnlineConnectionIdentityGateTests")
    set_kind("binary")
    add_files("tests/F4TConnectionIdentityGateTests.cpp")
    add_includedirs("include", co_core_include)
    add_forceincludes("F4TAssertionsEnabled.h")
    add_undefines("NDEBUG")
    add_tests("default")

for _, name in ipairs({ "JoinState", "FlyOverRestore", "FlyOverPath" }) do
    target("CommonwealthOnline" .. name .. "Tests")
        set_kind("binary")
        add_files("tests/F4T" .. name .. "Tests.cpp")
        add_includedirs("include", co_core_include)
        add_forceincludes("F4TAssertionsEnabled.h")
        add_undefines("NDEBUG")
        add_tests("default")
end

target("CommonwealthOnlineValidationAttemptTests")
    set_kind("binary")
    add_files("tests/F4TValidationAttemptTests.cpp")
    add_includedirs("include", co_core_include)
    add_forceincludes("F4TAssertionsEnabled.h")
    add_undefines("NDEBUG")
    add_tests("default")

target("CommonwealthOnlineClientIdentityTests")
    set_kind("binary")
    add_deps("CommonwealthOnlineCore")
    add_files(
        "src/F4TClientIdentity.cpp",
        "src/F4TResumeTokenProtection.cpp",
        "src/F4TSaveLineageStore.cpp",
        "src/F4TServerKey.cpp",
        "src/F4TServerProfileStore.cpp",
        "tests/F4TClientIdentityTests.cpp")
    add_defines(
        "F4T_CLIENT_IDENTITY_TESTING=1",
        "F4T_SERVER_PROFILE_STORE_TESTING=1",
        "F4T_TARGET_OG=1")
    add_includedirs("src", "include", co_core_include)
    add_packages("nlohmann_json")
    if is_plat("windows") then
        add_syslinks("crypt32")
    end
    add_forceincludes("F4TAssertionsEnabled.h")
    add_undefines("NDEBUG")
    add_tests("default")

target("CommonwealthOnlineConsoleGuardTests")
    set_kind("binary")
    add_files("tests/F4TConsoleGuardPolicyTests.cpp")
    add_includedirs("include", co_core_include)
    add_packages("nlohmann_json")
    add_forceincludes("F4TAssertionsEnabled.h")
    add_undefines("NDEBUG")
    add_tests("default")

target("CommonwealthOnlineLocalServerLogTests")
    set_kind("binary")
    add_files("tests/F4TLocalServerLogTests.cpp")
    add_includedirs("include", co_core_include)
    add_forceincludes("F4TAssertionsEnabled.h")
    add_undefines("NDEBUG")
    add_tests("default")

target("CommonwealthOnlineEquipmentStateTests")
    set_kind("binary")
    add_deps("CommonwealthOnlineCore")
    add_files("tests/F4TEquipmentStateTests.cpp")
    add_includedirs("include", co_core_include)
    add_packages("nlohmann_json")
    add_forceincludes("F4TAssertionsEnabled.h")
    add_undefines("NDEBUG")
    add_tests("default")

target("CommonwealthOnlinePlayerVitalsTests")
    set_kind("binary")
    add_deps("CommonwealthOnlineCore")
    add_files("tests/F4TPlayerVitalsTests.cpp")
    add_includedirs("include", co_core_include)
    add_packages("nlohmann_json")
    add_forceincludes("F4TAssertionsEnabled.h")
    add_undefines("NDEBUG")
    add_tests("default")

target("CommonwealthOnlineDownedTests")
    set_kind("binary")
    add_deps("CommonwealthOnlineCore")
    add_files("tests/F4TDownedTests.cpp")
    add_files("tests/F4TDownedCheckpointAndChannelTests.cpp")
    add_includedirs("include", co_core_include)
    add_packages("nlohmann_json")
    add_forceincludes("F4TAssertionsEnabled.h")
    add_undefines("NDEBUG")
    add_tests("default")

target("CommonwealthOnlinePowerArmorSyncTests")
    set_kind("binary")
    add_deps("CommonwealthOnlineCore")
    add_files("tests/F4TPowerArmorSyncTests.cpp")
    add_includedirs("include", co_core_include)
    add_packages("nlohmann_json")
    add_forceincludes("F4TAssertionsEnabled.h")
    add_undefines("NDEBUG")
    add_tests("default")

target("CommonwealthOnlineFactionPayloadTests")
    set_kind("binary")
    add_files("tests/F4TFactionPayloadTests.cpp")
    add_includedirs("include", co_core_include)
    add_packages("nlohmann_json")
    add_forceincludes("F4TAssertionsEnabled.h")
    add_undefines("NDEBUG")
    add_tests("default")

target("CommonwealthOnlineSettlementCapturePolicyTests")
    set_kind("binary")
    add_files("tests/F4TSettlementCapturePolicyTests.cpp")
    add_includedirs("include", co_core_include)
    add_forceincludes("F4TAssertionsEnabled.h")
    add_undefines("NDEBUG")
    add_tests("default")

target("CommonwealthOnlineSessionSaveGuardPolicyTests")
    set_kind("binary")
    add_files("tests/F4TSessionSaveGuardPolicyTests.cpp")
    add_includedirs("include", co_core_include)
    add_forceincludes("F4TAssertionsEnabled.h")
    add_undefines("NDEBUG")
    add_tests("default")

target("CommonwealthOnlineSaveFilePluginsTests")
    set_kind("binary")
    add_files("src/F4TSaveFilePlugins.cpp", "tests/F4TSaveFilePluginsTests.cpp")
    add_includedirs("include", co_core_include)
    add_forceincludes("F4TAssertionsEnabled.h")
    add_undefines("NDEBUG")
    add_tests("default")

target("CommonwealthOnlineLocalPlayerTransformSendTests")
    set_kind("binary")
    add_files("tests/F4TLocalPlayerTransformSendTests.cpp")
    add_includedirs("include", co_core_include)
    add_forceincludes("F4TAssertionsEnabled.h")
    add_undefines("NDEBUG")
    add_tests("default")

target("CommonwealthOnlineProxyMovementDirectionTests")
    set_kind("binary")
    add_files("tests/F4TProxyMovementDirectionTests.cpp")
    add_includedirs("include", co_core_include)
    add_forceincludes("F4TAssertionsEnabled.h")
    add_undefines("NDEBUG")
    add_tests("default")

target("CommonwealthOnlineProxyProtectionTests")
    set_kind("binary")
    add_files("tests/F4TProxyProtectionTests.cpp")
    add_includedirs("include", co_core_include)
    add_forceincludes("F4TAssertionsEnabled.h")
    add_undefines("NDEBUG")
    add_tests("default")

target("CommonwealthOnlineFactionInviteOfferTests")
    set_kind("binary")
    add_files("tests/F4TFactionInviteOfferTests.cpp")
    add_includedirs("include", co_core_include)
    add_forceincludes("F4TAssertionsEnabled.h")
    add_undefines("NDEBUG")
    add_tests("default")

target("CommonwealthOnlineProxyRemoteAnimationStateTests")
    set_kind("binary")
    add_files("tests/F4TProxyRemoteAnimationStateTests.cpp")
    add_includedirs("include", co_core_include)
    add_forceincludes("F4TAssertionsEnabled.h")
    add_undefines("NDEBUG")
    add_tests("default")

target("CommonwealthOnlineProxySneakActionTests")
    set_kind("binary")
    add_files("tests/F4TProxySneakActionTests.cpp")
    add_includedirs("include", co_core_include)
    add_forceincludes("F4TAssertionsEnabled.h")
    add_undefines("NDEBUG")
    add_tests("default")

target("CommonwealthOnlineTransformAnimationFieldsTests")
    set_kind("binary")
    add_files("tests/F4TTransformAnimationFieldsTests.cpp")
    add_deps("CommonwealthOnlineCore")
    add_includedirs("include", co_core_include)
    add_packages("nlohmann_json")
    add_forceincludes("F4TAssertionsEnabled.h")
    add_undefines("NDEBUG")
    add_tests("default")

target("CommonwealthOnlineThrowableSyncTests")
    set_kind("binary")
    add_files("tests/F4TThrowableSyncTests.cpp")
    add_includedirs("include", co_core_include)
    add_packages("nlohmann_json")
    add_forceincludes("F4TAssertionsEnabled.h")
    add_undefines("NDEBUG")
    add_tests("default")

target("CommonwealthOnlineHitAttributionTests")
    set_kind("binary")
    add_files("tests/F4THitAttributionTests.cpp")
    add_includedirs("include", co_core_include)
    add_forceincludes("F4TAssertionsEnabled.h")
    add_undefines("NDEBUG")
    add_tests("default")

target("CommonwealthOnlineWorldTimeMathTests")
    set_kind("binary")
    add_files("tests/F4TWorldTimeMathTests.cpp")
    add_includedirs("include", co_core_include)
    add_forceincludes("F4TAssertionsEnabled.h")
    add_undefines("NDEBUG")
    add_tests("default")

target("CommonwealthOnlineProxyMovementSmoothingTests")
    set_kind("binary")
    add_files("tests/F4TProxyMovementSmoothingTests.cpp")
    add_includedirs("include", co_core_include)
    add_forceincludes("F4TAssertionsEnabled.h")
    add_undefines("NDEBUG")
    add_tests("default")

target("CommonwealthOnlineProxyWeaponDrawTests")
    set_kind("binary")
    add_files("tests/F4TProxyWeaponDrawTests.cpp")
    add_includedirs("include", co_core_include)
    add_forceincludes("F4TAssertionsEnabled.h")
    add_undefines("NDEBUG")
    add_tests("default")

target("CommonwealthOnlineProxySnapshotMovementTests")
    set_kind("binary")
    add_files("src/F4TProxyMovementDiagnostics.cpp", "tests/F4TProxySnapshotMovementTests.cpp")
    add_includedirs("include", co_core_include)
    add_forceincludes("F4TAssertionsEnabled.h")
    add_undefines("NDEBUG")
    add_tests("default")

target("CommonwealthOnlineProxyMovementDiagnosticsTests")
    set_kind("binary")
    add_files("src/F4TProxyMovementDiagnostics.cpp", "tests/F4TProxyMovementDiagnosticsTests.cpp")
    add_includedirs("include", co_core_include)
    add_forceincludes("F4TAssertionsEnabled.h")
    add_undefines("NDEBUG")
    add_tests("default")

target("CommonwealthOnlineProxySaveSafetyTests")
    set_kind("binary")
    add_files("tests/F4TProxySaveSafetyTests.cpp")
    add_includedirs("include", co_core_include)
    add_forceincludes("F4TAssertionsEnabled.h")
    add_undefines("NDEBUG")
    add_tests("default")

target("CommonwealthOnlineWorldPolicyTests")
    set_kind("binary")
    add_deps("CommonwealthOnlineCore")
    add_files(
        "src/F4TWorldPolicyClient.cpp",
        "tests/F4TWorldPolicyTests.cpp")
    add_includedirs("include", co_core_include)
    add_packages("nlohmann_json")
    add_forceincludes("F4TAssertionsEnabled.h")
    add_undefines("NDEBUG")
    add_tests("default")

target("CommonwealthOnlineVatsPolicyCommandsTests")
    set_kind("binary")
    add_deps("CommonwealthOnlineCore")
    add_files("tests/F4TVatsPolicyCommandsTests.cpp")
    add_includedirs("include", co_core_include)
    add_packages("nlohmann_json")
    add_forceincludes("F4TAssertionsEnabled.h")
    add_undefines("NDEBUG")
    add_tests("default")

target("CommonwealthOnlineWorkshopObserverTests")
    set_kind("binary")
    add_files("tests/F4TWorkshopObserverTests.cpp")
    add_includedirs("include", co_core_include)
    add_forceincludes("F4TAssertionsEnabled.h")
    add_undefines("NDEBUG")
    add_tests("default")

target("CommonwealthOnlineProxyEquipmentStacksTests")
    set_kind("binary")
    add_files("tests/F4TProxyEquipmentStacksTests.cpp")
    add_includedirs("include", co_core_include)
    add_forceincludes("F4TAssertionsEnabled.h")
    add_undefines("NDEBUG")
    add_tests("default")

target("CommonwealthOnlineHookThreadTableTests")
    set_kind("binary")
    add_files("tests/F4THookThreadTableTests.cpp")
    add_includedirs("include", co_core_include)
    add_forceincludes("F4TAssertionsEnabled.h")
    add_undefines("NDEBUG")
    add_tests("default")

target("CommonwealthOnlineFrameLimiterTests")
    set_kind("binary")
    add_files("tests/F4TFrameLimiterTests.cpp")
    add_includedirs("include", co_core_include)
    add_forceincludes("F4TAssertionsEnabled.h")
    add_tests("default")

target("CommonwealthOnlineCrashReportTests")
    set_kind("binary")
    add_files("tests/F4TCrashReportTests.cpp")
    add_includedirs("include", co_core_include)
    add_forceincludes("F4TAssertionsEnabled.h")
    add_undefines("NDEBUG")
    add_tests("default")

target("CommonwealthOnlineProxyEquipmentDebounceTests")
    set_kind("binary")
    add_files("tests/F4TProxyEquipmentDebounceTests.cpp")
    add_includedirs("include", co_core_include)
    add_forceincludes("F4TAssertionsEnabled.h")
    add_undefines("NDEBUG")
    add_tests("default")

target("CommonwealthOnlineProbeManifestTests")
    set_kind("binary")
    add_files("src/F4TProbeManifest.cpp", "tests/F4TProbeManifestTests.cpp")
    add_includedirs("include", co_core_include)
    add_packages("nlohmann_json")
    add_forceincludes("F4TAssertionsEnabled.h")
    add_undefines("NDEBUG")
    add_tests("default")

target("CommonwealthOnlineProbeVersionGateTests")
    set_kind("binary")
    add_files("src/F4TProbeVersionGate.cpp", "tests/F4TProbeVersionGateTests.cpp")
    add_includedirs("include", co_core_include)
    add_forceincludes("F4TAssertionsEnabled.h")
    add_undefines("NDEBUG")
    add_tests("default")

if is_plat("windows") then
    target("CommonwealthOnlineInlineHookTests")
        set_kind("binary")
        add_deps("CommonwealthOnlineCore")
        add_files("src/F4TInlineHook.cpp", "tests/F4TInlineHookTests.cpp")
        add_includedirs("include", co_core_include)
        add_packages("safetyhook")
        add_forceincludes("F4TAssertionsEnabled.h")
        add_undefines("NDEBUG")
        add_tests("default")

    target("CommonwealthOnlineProbeCaptureTests")
        set_kind("binary")
        add_deps("CommonwealthOnlineCore")
        add_files("src/F4TProbeCapture.cpp", "tests/F4TProbeCaptureTests.cpp")
        add_includedirs("include", co_core_include)
        add_defines("WIN32_LEAN_AND_MEAN", "NOMINMAX")
        add_syslinks("ntdll")
        add_forceincludes("F4TAssertionsEnabled.h")
        add_undefines("NDEBUG")
        add_tests("default")

    target("CommonwealthOnlineUIApiLayoutTests")
        set_kind("binary")
        add_files("tests/F4TUIApiLayoutTests.cpp")
        add_includedirs("include", co_core_include)
        add_forceincludes("F4TAssertionsEnabled.h")
        add_undefines("NDEBUG")
        add_tests("default")
end
