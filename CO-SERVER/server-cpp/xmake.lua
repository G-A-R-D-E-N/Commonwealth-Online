set_project("CommonwealthOnline.Server")
set_version("0.1.0")
set_languages("c++23")
set_warnings("allextra")
add_rules("mode.debug", "mode.release", "mode.releasedbg")
set_policy("build.intermediate_directory", false)
set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)")

if is_plat("linux") then
    -- Sanitizer lanes need a toolchain whose runtime is installed; CO_TOOLCHAIN
    -- lets CI pick gcc without changing the default build.
    set_toolchains(os.getenv("CO_TOOLCHAIN") or "clang")
end

add_requires("nlohmann_json v3.12.0")
if is_plat("linux") then
    add_requires("libcurl", {configs = {openssl = true}})
end
includes("../../co-core/xmake.lua")

local bridge_dir = os.getenv("CO_GNS_BRIDGE_DIR")

local function server_test(name, file, ...)
    target(name)
        set_kind("binary")
        add_deps(...)
        add_files(file)
        add_forceincludes("F4TAssertionsEnabled.h")
        add_undefines("NDEBUG")
        add_tests("default")
    target_end()
end

target("CommonwealthOnlineServerConfiguration")
    set_kind("static")
    add_deps("CommonwealthOnlineCore")
    add_files("src/ServerConfiguration.cpp")
    add_headerfiles("include/ServerConfiguration.h")
    add_includedirs("include", {public = true})
    add_packages("nlohmann_json", {public = true})

target("CommonwealthOnlineServerArguments")
    set_kind("static")
    add_deps("CommonwealthOnlineServerConfiguration")
    add_files("src/ServerArguments.cpp")
    add_headerfiles("src/ServerArguments.h")
    add_includedirs("src", {public = true})

target("CommonwealthOnlineServerPacketDecode")
    set_kind("static")
    add_deps("CommonwealthOnlineServerConfiguration")
    add_files("src/ServerPacketDecode.cpp")
    add_headerfiles("src/ServerPacketDecode.h")
    add_includedirs("src", {public = true})

target("CommonwealthOnlineServerTextHelpers")
    set_kind("static")
    add_deps("CommonwealthOnlineCore")
    add_files("src/ServerTextHelpers.cpp")
    add_headerfiles("src/ServerTextHelpers.h")
    add_includedirs("src", {public = true})

target("CommonwealthOnlineServerCombatChecks")
    set_kind("static")
    add_deps("CommonwealthOnlineCore")
    add_files("src/ServerCombatChecks.cpp")
    add_headerfiles("src/ServerCombatChecks.h")
    add_includedirs("src", "include", {public = true})

target("CommonwealthOnlineServerNpcLootChecks")
    set_kind("static")
    add_deps("CommonwealthOnlineCore")
    add_files("src/ServerNpcLootChecks.cpp")
    add_headerfiles("src/ServerNpcLootChecks.h")
    add_includedirs("src", "include", {public = true})

target("CommonwealthOnlineServerNpcHitChecks")
    set_kind("static")
    add_deps("CommonwealthOnlineCore")
    add_files("src/ServerNpcHitChecks.cpp")
    add_headerfiles("src/ServerNpcHitChecks.h")
    add_includedirs("src", "include", {public = true})

target("CommonwealthOnlineServerTokenService")
    set_kind("static")
    add_deps("CommonwealthOnlineCore")
    add_files("src/OpenSslTokenService.cpp")
    add_headerfiles("src/OpenSslTokenService.h")
    add_includedirs("include", "src", {public = true})
    if is_plat("linux") then
        add_syslinks("crypto", {public = true})
    elseif is_plat("windows") then
        add_syslinks("bcrypt", {public = true})
    end

target("CommonwealthOnlineServerProtocol")
    set_kind("static")
    add_deps("CommonwealthOnlineCore", "CommonwealthOnlineServerConfiguration", "CommonwealthOnlineServerPersistence", "CommonwealthOnlineServerTextHelpers", "CommonwealthOnlineServerCombatChecks", "CommonwealthOnlineServerNpcLootChecks", "CommonwealthOnlineServerNpcHitChecks", "CommonwealthOnlineServerTokenService")
    add_files("src/ServerProtocol.cpp")
    add_files("src/ServerProtocolHandlers.cpp")
    add_files("src/ServerProtocolPublish.cpp")
    add_files("src/ServerProtocolCompatRoutes.cpp")
    add_files("src/ServerProtocolReconcile.cpp")
    add_files("src/ServerQuest.cpp")
    add_files("src/ServerDowned.cpp")
    add_files("src/ServerFactions.cpp")
    add_files("src/ServerChat.cpp")
    add_files("src/ServerWelcome.cpp")
    add_files("src/ServerShotReplayRelay.cpp")
    add_files("src/ServerFactionAdmin.cpp")
    add_files("src/ServerFactionInvites.cpp")
    add_files("src/ServerFactionStore.cpp")
    add_files("src/ServerSettlementAccess.cpp")
    add_files("src/ServerSettlementCapture.cpp")
    add_files("src/ServerCharacterSlots.cpp")
    add_files("src/ServerAdminMenu.cpp")
    add_files("src/IdentityGate.cpp")
    add_files("src/IdentityGatePassword.cpp")
    add_files("src/SteamIdentityLink.cpp")
    add_files("src/SteamWebApiVerifier.cpp")
    add_files("src/SteamIdentityRegistry.cpp")
    add_files("src/SteamAdmin.cpp")
    add_files("src/AdminControlServer.cpp")
    add_files("src/AdminTokenStore.cpp")
    add_files("src/AdminCommandDispatcher.cpp")
    add_files("src/BanStore.cpp")
    add_files("src/BanStorePersistence.cpp")
    add_files("src/LanDiscoveryService.cpp")
    add_files("src/DirectoryRegistrar.cpp")
    add_headerfiles("include/ServerProtocol.h")
    add_headerfiles("include/IdentityGate.h")
    add_headerfiles("include/SteamIdentityLink.h")
    add_headerfiles("include/SteamWebApiVerifier.h")
    add_headerfiles("include/SteamIdentityRegistry.h")
    add_headerfiles("include/SteamAdmin.h")
    add_headerfiles("include/AdminControlServer.h")
    add_headerfiles("include/AdminTokenStore.h")
    add_headerfiles("include/AdminCommandDispatcher.h")
    add_headerfiles("include/BanStore.h")
    add_headerfiles("include/LanDiscoveryService.h")
    add_headerfiles("include/DirectoryRegistrar.h")
    add_headerfiles("include/WorkerExit.h")
    add_includedirs("include", {public = true})
    add_packages("nlohmann_json", {public = true})
    if is_plat("windows") then
        add_syslinks("advapi32", "ws2_32", {public = true})
    end

target("CommonwealthOnlineServerDirectoryHttp")
    set_kind("static")
    add_deps("CommonwealthOnlineServerProtocol")
    add_files("src/DirectoryHttpSender.cpp")
    if is_plat("windows") then
        add_syslinks("winhttp", "ws2_32", {public = true})
    elseif is_plat("linux") then
        add_packages("libcurl", {public = true})
    end

target("CommonwealthOnlineServerLiveSettings")
    set_kind("static")
    add_deps("CommonwealthOnlineServerProtocol", "CommonwealthOnlineServerPersistence", "CommonwealthOnlineServerDirectoryHttp")
    add_files("src/ServerLiveSettings.cpp")
    add_headerfiles("include/ServerLiveSettings.h")
    add_includedirs("include", "../server/native_transport", {public = true})
    add_packages("nlohmann_json", {public = true})

target("CommonwealthOnlineServerPersistence")
    set_kind("static")
    add_deps("CommonwealthOnlineCore")
    add_files("src/DurableStateFileStore.cpp")
    add_files("src/PersistenceGate.cpp")
    add_files("src/DurableCommitWriter.cpp")
    add_files("src/SaveAttestationAuthority.cpp")
    add_headerfiles("include/DurableStateFileStore.h")
    add_headerfiles("include/DurableCommitWriter.h")
    add_headerfiles("include/PersistenceGate.h")
    add_headerfiles("include/SaveAttestationAuthority.h")
    add_includedirs("include", {public = true})
    add_packages("nlohmann_json", {public = true})

target("CommonwealthOnlineServerPersistenceSeedCommitTests")
    set_kind("binary")
    add_files("tests/PersistenceSeedCommitTests.cpp")
    add_packages("nlohmann_json")
    add_includedirs("../../co-core/include")
    add_forceincludes("F4TAssertionsEnabled.h")
    add_undefines("NDEBUG")
    add_tests("default")

target("CommonwealthOnlineServerPersistenceBenchmark")
    set_kind("binary")
    set_default(false)
    add_deps("CommonwealthOnlineServerPersistence")
    add_files("benchmarks/PersistenceBenchmark.cpp")

target("CommonwealthOnlineServerDurableCommitCrashProbe")
    set_kind("binary")
    set_default(false)
    add_deps("CommonwealthOnlineCore")
    add_defines("CO_DURABLE_COMMIT_CRASH_PROBE")
    add_files("src/DurableStateFileStore.cpp", "src/DurableCommitWriter.cpp", "benchmarks/DurableCommitCrashProbe.cpp")
    add_includedirs("include")
    add_packages("nlohmann_json")

-- Every test target below carries add_undefines("NDEBUG"). The suites are
-- written against bare assert, so under NDEBUG the checks and the side effects
-- inside them are stripped and a release-mode run reports success having
-- verified nothing.
server_test("CommonwealthOnlineServerConfigurationTests", "tests/ServerConfigurationTests.cpp", "CommonwealthOnlineServerConfiguration")

server_test("CommonwealthOnlineServerManifestChunkPasswordTests", "tests/ServerManifestChunkPasswordTests.cpp", "CommonwealthOnlineServerConfiguration")

server_test("CommonwealthOnlineServerSettingsFileTests", "tests/ServerSettingsFileTests.cpp", "CommonwealthOnlineServerConfiguration")

server_test("CommonwealthOnlineServerLiveRelayTests", "tests/ServerLiveRelayTests.cpp", "CommonwealthOnlineServerConfiguration")
server_test("CommonwealthOnlineServerLiveSettingsTests", "tests/ServerLiveSettingsTests.cpp", "CommonwealthOnlineServerProtocol")
server_test("CommonwealthOnlineServerLiveApplyTests", "tests/ServerLiveApplyTests.cpp", "CommonwealthOnlineServerLiveSettings")

target("CommonwealthOnlineServerLiveSettingsVatsTests")
    set_kind("binary")
    add_deps("CommonwealthOnlineServerProtocol", "CommonwealthOnlineServerPersistence")
    add_files("src/ServerLiveSettings.cpp", "tests/ServerLiveSettingsVatsTests.cpp")
    add_includedirs("include", "../server/native_transport")
    add_packages("nlohmann_json")
    add_forceincludes("F4TAssertionsEnabled.h")
    add_undefines("NDEBUG")
    add_tests("default")


target("CommonwealthOnlineServerPolicySampleTests")
    set_kind("binary")
    add_deps("CommonwealthOnlineServerConfiguration")
    add_files("tests/ServerPolicySampleTests.cpp")
    add_defines('CO_SERVER_SAMPLE_DIR="' .. os.scriptdir():gsub("\\", "/") .. '"')
    add_forceincludes("F4TAssertionsEnabled.h")
    add_undefines("NDEBUG")
    add_tests("default")

server_test("CommonwealthOnlineServerArgumentsTests", "tests/ServerArgumentsTests.cpp", "CommonwealthOnlineServerArguments")

server_test("CommonwealthOnlineServerPacketDecodeTests", "tests/ServerPacketDecodeTests.cpp", "CommonwealthOnlineServerPacketDecode")

server_test("CommonwealthOnlineServerTextHelpersTests", "tests/ServerTextHelpersTests.cpp", "CommonwealthOnlineServerTextHelpers")

target("CommonwealthOnlineServerManifestChunkQueueTests")
    set_kind("binary")
    add_files("tests/ServerManifestChunkQueueTests.cpp")
    add_includedirs("include", {public = true})
    add_includedirs("../../co-core/include")
    add_forceincludes("F4TAssertionsEnabled.h")
    add_undefines("NDEBUG")
    add_tests("default")
target_end()

server_test("CommonwealthOnlineServerCombatChecksTests", "tests/ServerCombatChecksTests.cpp", "CommonwealthOnlineServerCombatChecks")

server_test("CommonwealthOnlineServerNpcLootChecksTests", "tests/ServerNpcLootChecksTests.cpp", "CommonwealthOnlineServerNpcLootChecks")

server_test("CommonwealthOnlineServerNpcHitChecksTests", "tests/ServerNpcHitChecksTests.cpp", "CommonwealthOnlineServerNpcHitChecks")

server_test("CommonwealthOnlineServerTokenServiceTests", "tests/TokenServiceTests.cpp", "CommonwealthOnlineServerTokenService")

server_test("CommonwealthOnlineServerConsoleRoleTests", "tests/ServerConsoleRoleTests.cpp", "CommonwealthOnlineServerProtocol")

server_test("CommonwealthOnlineServerProtocolTests", "tests/ServerProtocolTests.cpp", "CommonwealthOnlineServerProtocol")

server_test("CommonwealthOnlineServerAdapterWireTests", "tests/ServerAdapterWireTests.cpp", "CommonwealthOnlineServerProtocol", "CommonwealthOnlineCore")

target("CommonwealthOnlineServerDownedTests")
    set_kind("binary")
    add_deps("CommonwealthOnlineServerProtocol")
    add_files("tests/ServerDownedTests.cpp", "tests/ServerDownedRespawnAndChannelTests.cpp")
    add_forceincludes("F4TAssertionsEnabled.h")
    add_undefines("NDEBUG")
    add_tests("default")
target_end()

server_test("CommonwealthOnlineServerDownedServiceTests", "tests/ServerDownedServiceTests.cpp", "CommonwealthOnlineServerProtocol")

server_test("CommonwealthOnlineServerLoadOrderTests", "tests/ServerLoadOrderTests.cpp", "CommonwealthOnlineServerProtocol")

server_test("CommonwealthOnlineServerInterestTests", "tests/ServerInterestTests.cpp", "CommonwealthOnlineServerProtocol")

server_test("CommonwealthOnlineServerRoutingParityTests", "tests/ServerRoutingParityTests.cpp", "CommonwealthOnlineServerProtocol")

server_test("CommonwealthOnlineServerSnapshotEnvelopeTests", "tests/ServerSnapshotEnvelopeTests.cpp", "CommonwealthOnlineServerProtocol")

server_test("CommonwealthOnlineServerCompatibilityShimTests", "tests/ServerCompatibilityShimTests.cpp", "CommonwealthOnlineServerProtocol")

server_test("CommonwealthOnlineServerReplayTests", "tests/ServerReplayTests.cpp", "CommonwealthOnlineServerProtocol")

server_test("CommonwealthOnlineServerWorkshopTests", "tests/ServerWorkshopTests.cpp", "CommonwealthOnlineServerProtocol")
server_test("CommonwealthOnlineServerSettlementAccessTests", "tests/ServerSettlementAccessTests.cpp", "CommonwealthOnlineServerProtocol")
server_test("CommonwealthOnlineServerSettlementCaptureTests", "tests/ServerSettlementCaptureTests.cpp", "CommonwealthOnlineServerProtocol")

server_test("CommonwealthOnlineServerWorldCombatTests", "tests/ServerWorldCombatTests.cpp", "CommonwealthOnlineServerProtocol")

server_test("CommonwealthOnlineServerFriendlyFireTests", "tests/ServerFriendlyFireTests.cpp", "CommonwealthOnlineServerProtocol")

server_test("CommonwealthOnlineServerEquipmentSyncTests", "tests/ServerEquipmentSyncTests.cpp", "CommonwealthOnlineServerProtocol")

server_test("CommonwealthOnlineServerPlayerVitalsTests", "tests/ServerPlayerVitalsTests.cpp", "CommonwealthOnlineServerProtocol")

server_test("CommonwealthOnlineServerDropSyncTests", "tests/ServerDropSyncTests.cpp", "CommonwealthOnlineServerProtocol")

server_test("CommonwealthOnlineServerNpcCombatTests", "tests/ServerNpcCombatTests.cpp", "CommonwealthOnlineServerProtocol")

server_test("CommonwealthOnlineServerNpcDamageTests", "tests/ServerNpcDamageTests.cpp", "CommonwealthOnlineServerProtocol")

server_test("CommonwealthOnlineServerNpcLootTests", "tests/ServerNpcLootTests.cpp", "CommonwealthOnlineServerProtocol")

server_test("CommonwealthOnlineServerChatTests", "tests/ServerChatTests.cpp", "CommonwealthOnlineServerProtocol")

server_test("CommonwealthOnlineServerFactionTests", "tests/ServerFactionTests.cpp", "CommonwealthOnlineServerProtocol")

server_test("CommonwealthOnlineServerCharacterSlotsTests", "tests/ServerCharacterSlotsTests.cpp", "CommonwealthOnlineServerProtocol")

server_test("CommonwealthOnlineServerFactionChatTests", "tests/ServerFactionChatTests.cpp", "CommonwealthOnlineServerProtocol")

server_test("CommonwealthOnlineServerFactionPersistenceTests", "tests/ServerFactionPersistenceTests.cpp", "CommonwealthOnlineServerProtocol")

server_test("CommonwealthOnlineServerFactionOfficerTests", "tests/ServerFactionOfficerTests.cpp", "CommonwealthOnlineServerProtocol")

server_test("CommonwealthOnlineServerFactionProfileTests", "tests/ServerFactionProfileTests.cpp", "CommonwealthOnlineServerProtocol")

server_test("CommonwealthOnlineServerFactionAdminTests", "tests/ServerFactionAdminTests.cpp", "CommonwealthOnlineServerProtocol")

server_test("CommonwealthOnlineServerFactionGapTests", "tests/ServerFactionGapTests.cpp", "CommonwealthOnlineServerProtocol")

server_test("CommonwealthOnlineServerFactionResilienceTests", "tests/ServerFactionResilienceTests.cpp", "CommonwealthOnlineServerProtocol")

server_test("CommonwealthOnlineServerFactionListTests", "tests/ServerFactionListTests.cpp", "CommonwealthOnlineServerProtocol")

server_test("CommonwealthOnlineServerFactionLimitsTests", "tests/ServerFactionLimitsTests.cpp", "CommonwealthOnlineServerProtocol")

server_test("CommonwealthOnlineServerSharedCoreParityTests", "tests/ServerSharedCoreParityTests.cpp", "CommonwealthOnlineServerProtocol", "CommonwealthOnlineCore")

server_test("CommonwealthOnlineServerLoadTests", "tests/ServerLoadTests.cpp", "CommonwealthOnlineServerProtocol")

server_test("CommonwealthOnlineServerIdentityGateSteamTests", "tests/IdentityGateSteamTests.cpp", "CommonwealthOnlineServerProtocol")

server_test("CommonwealthOnlineServerSteamIdentityLinkTests", "tests/SteamIdentityLinkTests.cpp", "CommonwealthOnlineServerProtocol")

server_test("CommonwealthOnlineServerSteamWebApiVerifierTests", "tests/SteamWebApiVerifierTests.cpp", "CommonwealthOnlineServerProtocol")

server_test("CommonwealthOnlineServerSteamIdentityRegistryTests", "tests/SteamIdentityRegistryTests.cpp", "CommonwealthOnlineServerProtocol")

server_test("CommonwealthOnlineServerIdentityGateTests", "tests/IdentityGateTests.cpp", "CommonwealthOnlineServerProtocol")

server_test("CommonwealthOnlineServerAdmissionTests", "tests/AdmissionTests.cpp", "CommonwealthOnlineServerProtocol")

server_test("CommonwealthOnlineServerIdentityRecoveryTests", "tests/IdentityRecoveryTests.cpp", "CommonwealthOnlineServerProtocol", "CommonwealthOnlineServerPersistence")

server_test("CommonwealthOnlineServerAdminControlTests", "tests/AdminControlServerTests.cpp", "CommonwealthOnlineServerProtocol")

server_test("CommonwealthOnlineServerAdminTokenStoreTests", "tests/AdminTokenStoreTests.cpp", "CommonwealthOnlineServerProtocol")

server_test("CommonwealthOnlineServerAdminCommandDispatcherTests", "tests/AdminCommandDispatcherTests.cpp", "CommonwealthOnlineServerProtocol")

server_test("CommonwealthOnlineServerAdminMenuTests", "tests/ServerAdminMenuTests.cpp", "CommonwealthOnlineServerProtocol")

server_test("CommonwealthOnlineServerBanStoreTests", "tests/BanStoreTests.cpp", "CommonwealthOnlineServerProtocol")

target("CommonwealthOnlineServerWorldPolicyTests")
    set_kind("binary")
    add_deps("CommonwealthOnlineServerProtocol")
    add_files("tests/ServerWorldPolicyTests.cpp")
    add_defines('CO_WORLD_POLICY_SAMPLE_DIR="' .. os.scriptdir():gsub("\\", "/") .. '"')
    add_forceincludes("F4TAssertionsEnabled.h")
    add_undefines("NDEBUG")
    add_tests("default")

server_test("CommonwealthOnlineServerLanDiscoveryTests", "tests/LanDiscoveryServiceTests.cpp", "CommonwealthOnlineServerProtocol")

server_test("CommonwealthOnlineServerDirectoryRegistrarTests", "tests/DirectoryRegistrarTests.cpp", "CommonwealthOnlineServerProtocol")

target("CommonwealthOnlineServerDirectoryLiveSmoke")
    set_kind("binary")
    set_default(false)
    add_deps("CommonwealthOnlineServerProtocol")
    add_files("tests/DirectoryLiveSmoke.cpp", "src/DirectoryHttpSender.cpp")
    if is_plat("windows") then
        add_syslinks("winhttp", "ws2_32")
    elseif is_plat("linux") then
        add_packages("libcurl")
    end

server_test("CommonwealthOnlineServerPersistenceTests", "tests/DurableStateFileStoreTests.cpp", "CommonwealthOnlineServerPersistence")

server_test("CommonwealthOnlineServerPersistenceGateTests", "tests/PersistenceGateTests.cpp", "CommonwealthOnlineServerPersistence")
server_test("CommonwealthOnlineServerInventoryTransferTests", "tests/InventoryTransferTests.cpp", "CommonwealthOnlineServerPersistence")

target("CommonwealthOnlineServerInventoryReadyDeliveryTests")
    set_kind("binary")
    add_deps("CommonwealthOnlineServerProtocol", "CommonwealthOnlineServerPersistence", "CommonwealthOnlineServerTokenService")
    add_files("tests/InventoryReadyDeliveryTests.cpp")
    add_includedirs("../server/native_transport")
    add_defines("CO_GNS_BRIDGE_BUILD")
    add_forceincludes("F4TAssertionsEnabled.h")
    add_undefines("NDEBUG")
    add_tests("default")

server_test("CommonwealthOnlineServerObjectStateTests", "tests/ServerObjectStateTests.cpp", "CommonwealthOnlineServerProtocol")
server_test("CommonwealthOnlineServerShotReplayRelayTests", "tests/ServerShotReplayRelayTests.cpp", "CommonwealthOnlineServerProtocol")

target("CommonwealthOnlineServerCpp")
    set_kind("binary")
    add_deps(
        "CommonwealthOnlineServerProtocol",
        "CommonwealthOnlineServerPersistence",
        "CommonwealthOnlineServerTokenService",
        "CommonwealthOnlineServerArguments",
        "CommonwealthOnlineServerPacketDecode",
        "CommonwealthOnlineServerTextHelpers",
        "CommonwealthOnlineServerLiveSettings",
        "CommonwealthOnlineServerDirectoryHttp")
    add_files("src/ServerMain.cpp")
    add_files("src/ServerBootstrap.cpp")
    add_files("src/ServerRuntimeSupport.cpp")
    add_files("src/ServerManifestChunks.cpp")
    add_files("src/ServerModpackFiles.cpp")
    add_headerfiles("include/ServerBootstrap.h")
    add_headerfiles("include/ServerRuntimeSupport.h")
    add_headerfiles("include/ServerManifestChunks.h")
    add_headerfiles("include/ServerModpackFiles.h")
    add_files("src/SteamWebApiSetup.cpp")
    add_includedirs("../server/native_transport")
    add_links("commonwealth_online_gns_bridge")
    if is_plat("windows") then
        add_syslinks("bcrypt", "ws2_32", "winhttp", "shell32")
    elseif is_plat("linux") then
        add_packages("libcurl")
    end
    if bridge_dir then
        add_linkdirs(bridge_dir)
        if is_plat("linux") then
            add_rpathdirs(bridge_dir)
        end
    end

target("CommonwealthOnlineServerLiveGnsSmoke")
    set_kind("binary")
    add_files("tests/LiveGnsSmoke.cpp")
    add_includedirs("../server/native_transport")
    add_packages("nlohmann_json")
    add_links("commonwealth_online_gns_bridge")
    if bridge_dir then
        add_linkdirs(bridge_dir)
        if is_plat("linux") then
            add_rpathdirs(bridge_dir)
        end
    end

target("CommonwealthOnlineServerLiveNamedGnsSmoke")
    set_kind("binary")
    add_deps("CommonwealthOnlineCore")
    add_files("tests/LiveNamedGnsSmoke.cpp")
    add_includedirs("../server/native_transport")
    add_packages("nlohmann_json")
    add_links("commonwealth_online_gns_bridge")
    if bridge_dir then
        add_linkdirs(bridge_dir)
        if is_plat("linux") then
            add_rpathdirs(bridge_dir)
        end
    end

target("CommonwealthOnlineServerLiveNetworkFaultGnsSmoke")
    set_kind("binary")
    add_deps("CommonwealthOnlineCore")
    add_files("tests/LiveNetworkFaultGnsSmoke.cpp")
    add_includedirs("../server/native_transport")
    add_packages("nlohmann_json")
    add_links("commonwealth_online_gns_bridge")
    if bridge_dir then
        add_linkdirs(bridge_dir)
        if is_plat("linux") then
            add_rpathdirs(bridge_dir)
        end
    end

target("CommonwealthOnlineServerLiveProtocolFaultGnsSmoke")
    set_kind("binary")
    add_deps("CommonwealthOnlineCore")
    add_files("tests/LiveProtocolFaultGnsSmoke.cpp")
    add_includedirs("../server/native_transport")
    add_packages("nlohmann_json")
    add_links("commonwealth_online_gns_bridge")
    if bridge_dir then
        add_linkdirs(bridge_dir)
        if is_plat("linux") then
            add_rpathdirs(bridge_dir)
        end
    end

target("CommonwealthOnlineServerLiveAuthorityChurnGnsSmoke")
    set_kind("binary")
    add_deps("CommonwealthOnlineCore")
    add_files("tests/LiveAuthorityChurnGnsSmoke.cpp")
    add_includedirs("../server/native_transport")
    add_packages("nlohmann_json")
    add_links("commonwealth_online_gns_bridge")
    if bridge_dir then
        add_linkdirs(bridge_dir)
        if is_plat("linux") then
            add_rpathdirs(bridge_dir)
        end
    end

target("CommonwealthOnlineServerLiveReconnectChurnGnsSmoke")
    set_kind("binary")
    add_deps("CommonwealthOnlineCore")
    add_files("tests/LiveReconnectChurnGnsSmoke.cpp")
    add_includedirs("../server/native_transport")
    add_packages("nlohmann_json")
    add_links("commonwealth_online_gns_bridge")
    if bridge_dir then
        add_linkdirs(bridge_dir)
        if is_plat("linux") then
            add_rpathdirs(bridge_dir)
        end
    end

target("CommonwealthOnlineServerLiveAdmissionGnsSmoke")
    set_kind("binary")
    add_files("tests/LiveAdmissionGnsSmoke.cpp")
    add_includedirs("../server/native_transport")
    add_packages("nlohmann_json")
    add_links("commonwealth_online_gns_bridge")
    if bridge_dir then
        add_linkdirs(bridge_dir)
        if is_plat("linux") then
            add_rpathdirs(bridge_dir)
        end
    end

target("CommonwealthOnlineServerLiveIdentityGnsSmoke")
    set_kind("binary")
    add_files("tests/LiveIdentityGnsSmoke.cpp")
    add_includedirs("../server/native_transport")
    add_packages("nlohmann_json")
    add_links("commonwealth_online_gns_bridge")
    if bridge_dir then
        add_linkdirs(bridge_dir)
        if is_plat("linux") then
            add_rpathdirs(bridge_dir)
        end
    end

target("CommonwealthOnlineServerLivePersistenceGnsSmoke")
    set_kind("binary")
    add_deps("CommonwealthOnlineCore")
    add_files("tests/LivePersistenceGnsSmoke.cpp")
    add_includedirs("../server/native_transport")
    add_packages("nlohmann_json")
    add_links("commonwealth_online_gns_bridge")
    if bridge_dir then
        add_linkdirs(bridge_dir)
        if is_plat("linux") then
            add_rpathdirs(bridge_dir)
        end
    end

target("CommonwealthOnlineServerLiveAdminGnsSmoke")
    set_kind("binary")
    add_files("tests/LiveAdminGnsSmoke.cpp")
    add_includedirs("../server/native_transport")
    add_packages("nlohmann_json")
    add_links("commonwealth_online_gns_bridge")
    -- The admin channel is plain TCP, so this is the only smoke that needs a
    -- socket library of its own.
    if is_plat("windows") then
        add_syslinks("ws2_32")
    end
    if bridge_dir then
        add_linkdirs(bridge_dir)
        if is_plat("linux") then
            add_rpathdirs(bridge_dir)
        end
    end

target("CommonwealthOnlineServerLiveProfileBanGnsSmoke")
    set_kind("binary")
    add_files("tests/LiveProfileBanGnsSmoke.cpp")
    add_includedirs("../server/native_transport")
    add_packages("nlohmann_json")
    add_links("commonwealth_online_gns_bridge")
    if is_plat("windows") then
        add_syslinks("ws2_32")
    end
    if bridge_dir then
        add_linkdirs(bridge_dir)
        if is_plat("linux") then
            add_rpathdirs(bridge_dir)
        end
    end

includes("xmake-voice.lua")
target("CommonwealthOnlineServerQuestTests")
    set_kind("binary")
    add_deps("CommonwealthOnlineServerProtocol")
    add_files("tests/ServerQuestTests.cpp")
    add_forceincludes("F4TAssertionsEnabled.h")
    add_undefines("NDEBUG")
    add_tests("default")

target("CommonwealthOnlineServerQuestStoryTests")
    set_kind("binary")
    add_deps("CommonwealthOnlineServerProtocol")
    add_files("tests/ServerQuestStoryTests.cpp")
    add_forceincludes("F4TAssertionsEnabled.h")
    add_undefines("NDEBUG")
    add_tests("default")

target("CommonwealthOnlineServerQuestAdminTests")
    set_kind("binary")
    add_deps("CommonwealthOnlineServerProtocol")
    add_files("tests/ServerQuestAdminTests.cpp")
    add_forceincludes("F4TAssertionsEnabled.h")
    add_undefines("NDEBUG")
    add_tests("default")

target("CommonwealthOnlineServerQuestPartyTests")
    set_kind("binary")
    add_deps("CommonwealthOnlineServerProtocol")
    add_files("tests/ServerQuestPartyTests.cpp")
    add_forceincludes("F4TAssertionsEnabled.h")
    add_undefines("NDEBUG")
    add_tests("default")
