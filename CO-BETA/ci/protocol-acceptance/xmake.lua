local root_dir = path.absolute("../../..", os.scriptdir())

set_project("CommonwealthOnlineClientProtocolAcceptance")
set_version("0.1.0")
set_languages("c++23")
set_warnings("allextra")
add_rules("mode.debug", "mode.release")

includes(path.join(root_dir, "co-core/xmake.lua"))

target("CommonwealthOnlineClientProtocolAcceptance")
    set_kind("binary")
    add_deps("CommonwealthOnlineCore")
    add_files("ClientAdapterGnsAcceptance.cpp")
    add_includedirs(path.join(root_dir, "CO-BETA/plugin/gns-bridge"))
    if is_plat("linux") then
        add_syslinks("dl")
    end
