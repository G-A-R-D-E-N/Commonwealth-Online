local bridge_dir = os.getenv("CO_GNS_BRIDGE_DIR")

target("CommonwealthOnlineServerVoiceFloodTests")
    set_kind("binary")
    add_deps("CommonwealthOnlineCore")
    add_includedirs("src")
    add_files("tests/ServerVoiceFloodTests.cpp")
    add_forceincludes("F4TAssertionsEnabled.h")
    add_undefines("NDEBUG")
    add_tests("default")

target("CommonwealthOnlineServerProtocol")
    add_deps("CommonwealthOnlineServerPacketDecode")
    add_files("src/ServerVoice.cpp", "src/ServerVoiceHost.cpp")
target_end()

target("CommonwealthOnlineServerVoiceTests")
    set_kind("binary")
    add_deps("CommonwealthOnlineServerProtocol")
    add_files("tests/ServerVoiceTests.cpp")
    add_forceincludes("F4TAssertionsEnabled.h")
    add_undefines("NDEBUG")
    add_tests("default")

target("CommonwealthOnlineServerLiveVoiceFloodGnsSmoke")
    set_kind("binary")
    add_deps("CommonwealthOnlineCore")
    add_files("tests/LiveVoiceFloodGnsSmoke.cpp")
    add_includedirs("../server/native_transport")
    add_packages("nlohmann_json")
    add_links("commonwealth_online_gns_bridge")
    if bridge_dir then
        add_linkdirs(bridge_dir)
        if is_plat("linux") then
            add_rpathdirs(bridge_dir)
        end
    end

target("CommonwealthOnlineServerVoiceControlTests")
    set_kind("binary")
    add_deps("CommonwealthOnlineServerProtocol")
    add_files("tests/ServerVoiceControlTests.cpp")
    add_forceincludes("F4TAssertionsEnabled.h")
    add_undefines("NDEBUG")
    add_tests("default")

target("CommonwealthOnlineServerVoiceHostTests")
    set_kind("binary")
    add_deps("CommonwealthOnlineServerProtocol")
    add_files("tests/ServerVoiceHostTests.cpp")
    add_forceincludes("F4TAssertionsEnabled.h")
    add_undefines("NDEBUG")
    add_tests("default")
