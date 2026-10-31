includes("../CO-BETA/plugin/toolchains/xwin-clang-cl.lua")
set_toolchains("xwin-clang-cl")
set_runtimes("MD")
set_project("CommonwealthOnlineUIRetryCheck")
set_languages("c++23")
set_warnings("allextra")

add_defines("_CRT_USE_BUILTIN_OFFSETOF", "NOGDI", "NOMINMAX", "WIN32_LEAN_AND_MEAN")
add_defines("SPDLOG_HEADER_ONLY", "SPDLOG_USE_STD_FORMAT", "SPDLOG_WCHAR_TO_UTF8_SUPPORT")

add_requires("nlohmann_json v3.12.0")
add_requires("spdlog v1.16.0", {configs = {header_only = true, wchar = true, std_format = true}})

target("CommonwealthOnlineUIRetryCompileCheck")
    set_kind("static")
    add_files("src/SessionCallbacks.cpp", "src/PluginAPI.cpp")
    add_includedirs(
        "ci",
        "src",
        "include",
        "ThirdParty/CommonLibF4/include",
        "ThirdParty/CommonLibF4/lib/commonlib-shared/include")
    add_packages("nlohmann_json", "spdlog")

target("CommonwealthOnlineUIProductionCompileCheck")
    set_kind("static")
    add_files("src/**.cpp")
    add_includedirs(
        "src",
        "include",
        "../co-core/include",
        "ThirdParty/CommonLibF4/include",
        "ThirdParty/CommonLibF4/lib/commonlib-shared/include")
    add_packages("nlohmann_json", "spdlog")

target("CommonwealthOnlineUIPrismaCompileCheck")
    set_kind("static")
    add_files(
        "src/F4TPrismaUI.cpp",
        "src/F4TPrismaUI_Cursor.cpp",
        "src/F4TPrismaUI_Browser.cpp",
        "src/F4TPrismaUI_Menu.cpp",
        "src/F4TPrismaUI_MainMenu.cpp",
        "src/F4TPrismaUI_Lifecycle.cpp",
        "src/F4TPrismaUI_ServerBrowserView.cpp",
        "src/F4TMainMenuLauncher.cpp",
        "src/F4TMenuInput.cpp",
        "src/F4TCharacterCreator.cpp",
        "src/F4TCharacterCreatorCommands.cpp",
        "src/F4TCharacterCreatorState.cpp",
        "src/F4TIatPatch.cpp",
        "src/F4TVtableHookGuard.cpp",
        "../co-core/src/runtime/F4TVtableGuard.cpp",
        "../co-core/src/runtime/F4TF4SECapabilityPolicy.cpp")
    add_includedirs(
        "src",
        "include",
        "../co-core/include",
        "ThirdParty/CommonLibF4/include",
        "ThirdParty/CommonLibF4/lib/commonlib-shared/include")
    add_packages("nlohmann_json", "spdlog")
