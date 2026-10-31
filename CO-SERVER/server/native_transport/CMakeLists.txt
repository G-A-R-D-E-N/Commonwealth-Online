cmake_minimum_required(VERSION 3.20)
project(CommonwealthOnlineGnsServerBridge LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    # Release packages must not require a separately installed VC++ runtime.
    # The bridge API keeps allocation ownership inside the DLL, so static CRT
    # linkage is safe across the C ABI boundary.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(GNS_SOURCE_DIR "" CACHE PATH "Path to the pinned GameNetworkingSockets source tree")
if(NOT EXISTS "${GNS_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "GNS_SOURCE_DIR must point to an initialized GameNetworkingSockets source tree")
endif()

# The release bridge owns the GNS runtime so packaged servers do not need a
# second loose GameNetworkingSockets library beside the bridge.
set(BUILD_STATIC_LIB ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIB OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(ENABLE_ICE OFF CACHE BOOL "" FORCE)
set(USE_STEAMWEBRTC OFF CACHE BOOL "" FORCE)
if(WIN32)
    set(USE_CRYPTO BCrypt CACHE STRING "" FORCE)
    set(Protobuf_USE_STATIC_LIBS ON CACHE BOOL "" FORCE)
else()
    set(USE_CRYPTO OpenSSL CACHE STRING "" FORCE)
    set(USE_CRYPTO25519 OpenSSL CACHE STRING "" FORCE)
endif()

set(CO_GNS_BINARY_DIR "${CMAKE_BINARY_DIR}/gns" CACHE PATH "Binary directory for the GameNetworkingSockets build")
add_subdirectory("${GNS_SOURCE_DIR}" "${CO_GNS_BINARY_DIR}")

option(CO_GNS_BUILD_TESTS "Build native GNS bridge tests" ON)
# Global fake packet loss and reorder are test instrumentation. They used to be
# compiled into every bridge, including shipped ones, where anything able to
# call into the library could degrade the whole server's networking. They now
# follow the test build, so a bare configure works and a packaged bridge - which
# builds with both off - carries neither.
option(CO_GNS_ENABLE_TEST_NETWORK
    "Export the fake packet loss and reorder controls" ${CO_GNS_BUILD_TESTS})

add_library(commonwealth_online_gns_bridge SHARED
    co_gns_server_bridge.cpp
    co_gns_server_endpoint.cpp
)
if(CO_GNS_ENABLE_TEST_NETWORK)
    target_sources(commonwealth_online_gns_bridge PRIVATE co_gns_test_network.cpp)
    target_compile_definitions(commonwealth_online_gns_bridge PUBLIC CO_GNS_TEST_NETWORK_ENABLED)
endif()
target_include_directories(commonwealth_online_gns_bridge PUBLIC "${CMAKE_CURRENT_LIST_DIR}")
target_compile_definitions(commonwealth_online_gns_bridge PRIVATE CO_GNS_BRIDGE_BUILD)
if(MSVC)
    target_compile_options(commonwealth_online_gns_bridge PRIVATE
        /W4
        /WX
        /FIsteam/isteamnetworkingutils.h
    )
else()
    # steamnetworkingtypes.h declares several address helpers inline, while the
    # standalone definitions live in isteamnetworkingutils.h. Make that public
    # GNS utility header explicit for this translation unit instead of
    # suppressing the undefined-inline diagnostic.
    target_compile_options(commonwealth_online_gns_bridge PRIVATE
        -Wall
        -Wextra
        -Werror
        -include
        steam/isteamnetworkingutils.h
    )
endif()
target_link_libraries(commonwealth_online_gns_bridge PRIVATE GameNetworkingSockets::static)
set_target_properties(commonwealth_online_gns_bridge PROPERTIES OUTPUT_NAME "commonwealth_online_gns_bridge")

if(CO_GNS_BUILD_TESTS)
    add_executable(co-gns-server-bridge-tests test_co_gns_server_bridge.cpp)
    add_executable(co-gns-client-bridge-tests test_co_gns_client_bridge.cpp)
    add_executable(co-gns-server-endpoint-tests test_co_gns_server_endpoint.cpp)
    add_executable(co-gns-ipv6-tests test_co_gns_ipv6.cpp)
    # This one drives the fake-network exports, so it only exists when they do.
    if(CO_GNS_ENABLE_TEST_NETWORK)
        add_executable(co-gns-network-config-tests test_co_gns_network_config.cpp)
    endif()

    if(MSVC)
        target_compile_options(co-gns-server-bridge-tests PRIVATE /W4 /WX /UNDEBUG)
        target_compile_options(co-gns-client-bridge-tests PRIVATE /W4 /WX /UNDEBUG)
        if(CO_GNS_ENABLE_TEST_NETWORK)
            target_compile_options(co-gns-network-config-tests PRIVATE /W4 /WX /UNDEBUG)
        endif()
        target_compile_options(co-gns-server-endpoint-tests PRIVATE /W4 /WX /UNDEBUG)
        target_compile_options(co-gns-ipv6-tests PRIVATE /W4 /WX /UNDEBUG)
        # The test harnesses read CO_REQUIRE_LIVE_GNS with std::getenv, which the
        # Microsoft CRT marks deprecated in favour of _dupenv_s. /WX would turn
        # that advisory into a build failure. std::getenv is standard C++ and
        # correct here, so silence the CRT-specific deprecation for the test
        # targets only rather than weakening /W4 /WX or rewriting portable code
        # into a Microsoft-only API.
        target_compile_definitions(co-gns-server-bridge-tests PRIVATE _CRT_SECURE_NO_WARNINGS)
        target_compile_definitions(co-gns-client-bridge-tests PRIVATE _CRT_SECURE_NO_WARNINGS)
        if(CO_GNS_ENABLE_TEST_NETWORK)
            target_compile_definitions(co-gns-network-config-tests PRIVATE _CRT_SECURE_NO_WARNINGS)
        endif()
        target_compile_definitions(co-gns-server-endpoint-tests PRIVATE _CRT_SECURE_NO_WARNINGS)
        target_compile_definitions(co-gns-ipv6-tests PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(co-gns-server-bridge-tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)
        target_compile_options(co-gns-client-bridge-tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)
        if(CO_GNS_ENABLE_TEST_NETWORK)
            target_compile_options(co-gns-network-config-tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)
        endif()
        target_compile_options(co-gns-server-endpoint-tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)
        target_compile_options(co-gns-ipv6-tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    endif()

    target_link_libraries(co-gns-server-bridge-tests PRIVATE
        commonwealth_online_gns_bridge
        GameNetworkingSockets::static
    )
    target_link_libraries(co-gns-client-bridge-tests PRIVATE
        commonwealth_online_gns_bridge
        GameNetworkingSockets::static
    )
    if(CO_GNS_ENABLE_TEST_NETWORK)
        target_link_libraries(co-gns-network-config-tests PRIVATE
            commonwealth_online_gns_bridge
            GameNetworkingSockets::static
        )
    endif()
    target_link_libraries(co-gns-server-endpoint-tests PRIVATE
        commonwealth_online_gns_bridge
        GameNetworkingSockets::static
    )
    target_link_libraries(co-gns-ipv6-tests PRIVATE
        commonwealth_online_gns_bridge
        GameNetworkingSockets::static
    )

    enable_testing()
    add_test(NAME co-gns-server-bridge COMMAND co-gns-server-bridge-tests)
    add_test(NAME co-gns-client-bridge COMMAND co-gns-client-bridge-tests)
    add_test(NAME co-gns-server-endpoint COMMAND co-gns-server-endpoint-tests)
    add_test(NAME co-gns-ipv6 COMMAND co-gns-ipv6-tests)
    if(CO_GNS_ENABLE_TEST_NETWORK)
        add_test(NAME co-gns-network-config COMMAND co-gns-network-config-tests)
    endif()
endif()
