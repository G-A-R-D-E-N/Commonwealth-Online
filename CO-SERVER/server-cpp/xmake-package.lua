set_languages("c++23")

if is_plat("linux") then
    set_toolchains("clang")
end

local package_bridge_dir = os.getenv("CO_GNS_PACKAGE_BRIDGE_DIR")

includes("xmake.lua")

target("CommonwealthOnlineServerCpp")
    if package_bridge_dir then
        add_linkdirs(package_bridge_dir)
    end
    if is_plat("linux") then
        add_rpathdirs("$ORIGIN")
    end

for _, target_name in ipairs({
    "CommonwealthOnlineServerLiveGnsSmoke",
    "CommonwealthOnlineServerLiveIdentityGnsSmoke",
    "CommonwealthOnlineServerLivePersistenceGnsSmoke",
    "CommonwealthOnlineServerLiveAdminGnsSmoke",
    "CommonwealthOnlineServerLiveProfileBanGnsSmoke"
}) do
    target(target_name)
        if package_bridge_dir then
            add_linkdirs(package_bridge_dir)
        end
end
