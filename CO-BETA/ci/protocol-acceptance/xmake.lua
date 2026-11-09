rule("windows-host-only")
    on_load(function ()
        if not is_host("windows") then
            raise("Game client tests can only be built on a Windows host")
        end
    end)
rule_end()
add_rules("windows-host-only")

local root_dir = path.absolute("../../..", os.scriptdir())

set_project("CommonwealthOnlineClientProtocolAcceptance")
set_version("0.1.0")
set_languages("c++23")
set_warnings("allextra")
add_rules("mode.debug", "mode.release")

if os.host() == "windows" then
    includes(path.join(root_dir, "co-core/xmake.lua"))
end

target("CommonwealthOnlineClientProtocolAcceptance")
    set_kind("binary")
    add_deps("CommonwealthOnlineCore")
    add_files("ClientAdapterGnsAcceptance.cpp")
    add_includedirs(path.join(root_dir, "CO-BETA/plugin/gns-bridge"))
