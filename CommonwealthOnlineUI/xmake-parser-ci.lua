set_project("CommonwealthOnlineUIParserTests")
set_languages("c++23")
set_warnings("allextra")

add_requires("nlohmann_json v3.12.0")

target("MultiplayerProfileTests")
    set_kind("binary")
    add_defines("COUI_PROFILE_TESTING", "NOMINMAX", "WIN32_LEAN_AND_MEAN")
    add_undefines("NDEBUG")
    add_files("src/F4TMultiplayerProfile.cpp", "ci/MultiplayerProfileTests.cpp")
    add_includedirs("include")
    add_packages("nlohmann_json")

target("ServerBrowserBoundsTests")
    set_kind("binary")
    add_defines("F4T_SERVER_BROWSER_DATA_TESTING")
    add_undefines("NDEBUG")
    add_files("src/F4TServerBrowserData.cpp", "src/F4TWorldModePolicy.cpp", "ci/ServerBrowserBoundsTests.cpp")
    add_includedirs(
        "ci",
        "src",
        "include",
        "ThirdParty/CommonLibF4/include",
        "ThirdParty/CommonLibF4/lib/commonlib-shared/include")
    add_packages("nlohmann_json")

if is_plat("windows") then
    target("IatPatchTests")
        set_kind("binary")
        add_files("src/F4TIatPatch.cpp", "ci/F4TIatPatchTests.cpp")
        add_includedirs("src")
        add_undefines("NDEBUG")
end

target("LauncherEventProvenanceTests")
    set_kind("binary")
    add_files("ci/LauncherEventProvenanceTests.cpp")
    add_includedirs("include")

target("QuestCallbacksTests")
    set_kind("binary")
    add_defines("F4T_QUEST_CALLBACKS_TESTING")
    add_undefines("NDEBUG")
    add_files("src/QuestCallbacks.cpp", "ci/QuestCallbacksTests.cpp")
    add_includedirs("include")
    add_packages("nlohmann_json")

target("SettingsRefreshRouteTests")
    set_kind("binary")
    add_undefines("NDEBUG")
    add_files("ci/SettingsRefreshRouteTests.cpp")
    add_includedirs("include", "../co-core/include")

target("ExitSaveGateTests")
    set_kind("binary")
    add_undefines("NDEBUG")
    add_files("ci/ExitSaveGateTests.cpp")
    add_includedirs("include")

target("WorldModeTests")
    set_kind("binary")
    add_undefines("NDEBUG")
    add_files("src/F4TWorldModePolicy.cpp", "ci/WorldModeTests.cpp")
    add_includedirs("src", "include")

target("SteamActionTests")
    set_kind("binary")
    add_undefines("NDEBUG")
    add_files("ci/SteamActionTests.cpp")
    add_includedirs("include")
    add_packages("nlohmann_json")

target("DownedStateTests")
    set_kind("binary")
    add_undefines("NDEBUG")
    add_defines("F4T_DOWNED_TESTING")
    add_files("src/F4TDownedUiState.cpp", "ci/DownedStateTests.cpp")
    add_includedirs("ci", "include")
    add_packages("nlohmann_json")

target("VoiceBridgeTests")
    set_kind("binary")
    add_undefines("NDEBUG")
    add_defines("F4T_VOICE_BRIDGE_TESTING")
    add_files("src/F4TVoiceBridgeState.cpp", "ci/VoiceBridgeTests.cpp")
    add_includedirs("ci", "include")
    add_packages("nlohmann_json")
