includes("../CO-BETA/plugin/toolchains/xwin-clang-cl.lua")
set_toolchains("xwin-clang-cl")
set_runtimes("MD")
add_requireconfs("zstd", {configs = {cmake = false}, override = true})

includes("xmake.lua")
