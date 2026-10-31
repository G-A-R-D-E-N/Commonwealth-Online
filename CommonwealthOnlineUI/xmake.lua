-- CommonwealthOnlineUI
-- Commonwealth Online UI F4SE plugin using CommonLibF4 + PrismaUI.

includes("ThirdParty/CommonLibF4")

local co_core_dir = path.join(os.scriptdir(), "../co-core")

includes(path.join(co_core_dir, "xmake.lua"))

set_project("CommonwealthOnlineUI")
set_version("0.1.0")
set_license("MIT")
set_languages("c++23")
set_warnings("allextra")

add_rules("mode.debug", "mode.releasedbg")
add_rules("plugin.vsxmake.autoupdate")
add_requires("nlohmann_json v3.12.0")

target("CommonwealthOnlineUI")
    add_rules("commonlibf4.plugin", {
        name = "CommonwealthOnlineUI",
        author = "Commonwealth Online",
        description = "Commonwealth Online UI F4SE plugin",
        plugin_template = "res/commonwealthonlineui-plugin.cpp.in"
    })

    add_files("src/**.cpp")
    add_headerfiles("src/**.h")
    add_headerfiles("include/**.h")
    add_includedirs("src", "include", path.join(co_core_dir, "include"))
    add_deps("CommonwealthOnlineCore")
    add_packages("nlohmann_json")
    add_syslinks("winhttp", "ws2_32")
    set_pcxxheader("src/pch.h")

    after_build(function (target)
        local outdir = path.join(os.projectdir(), "package", "F4SE", "Plugins")
        os.mkdir(outdir)
        os.cp(target:targetfile(), outdir)

        local pdb = path.join(target:targetdir(), target:basename() .. ".pdb")
        if os.isfile(pdb) then
            os.cp(pdb, outdir)
        end
    end)
