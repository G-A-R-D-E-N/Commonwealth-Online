-- Native clang-cl/xwin build of the UI plugin, mirroring CO-BETA/plugin/xmake-xwin.lua.
--
-- The shipping target lives in xmake.lua exactly once; this file only switches the
-- project onto the cross toolchain and the staged Windows packages.
--
--   xmake f --file=xmake-xwin.lua --plat=windows --arch=x64 -m release -y
--   xmake build --file=xmake-xwin.lua CommonwealthOnlineUI

includes("../CO-BETA/plugin/toolchains/xwin-clang-cl.lua")
set_toolchains("xwin-clang-cl")
set_runtimes("MD")

-- xwin's ucrt stddef.h defines offsetof via reinterpret_cast, which is not a
-- constant expression, so CommonLibF4's layout static_asserts fail without this.
add_defines("_CRT_USE_BUILTIN_OFFSETOF")

-- REX::W32 exists so CommonLibF4 never includes windows.h. Anything dragging
-- windows.h in re-introduces MAX_PATH and ERROR, which collide with
-- REX::W32::MAX_PATH and REX::ERROR.
add_defines("NOGDI", "NOMINMAX", "WIN32_LEAN_AND_MEAN")

-- spdlog is the usual vector for that: a header-only spdlog pulls its *-inl.h,
-- which includes windows.h. SPDLOG_COMPILED_LIB keeps the implementation out of
-- every translation unit; the symbols come from the staged library below.
add_defines("SPDLOG_COMPILED_LIB", "SPDLOG_USE_STD_FORMAT", "SPDLOG_WCHAR_TO_UTF8_SUPPORT")

add_requireconfs("**.cmake", {system = true, override = true})
add_requireconfs("spdlog", {configs = {header_only = true, wchar = true, std_format = true}, override = true})

-- The staged Windows packages are world-readable under /opt on the build host,
-- so no runner needs its own copy and no ACL is involved.
local pkgroot = os.getenv("PRISMA_WIN_PACKAGES")
if not pkgroot then
    local toolchain_root = os.getenv("PRISMA_WIN_TOOLCHAIN")
    if toolchain_root then
        pkgroot = path.join(toolchain_root, "Users/Game/AppData/Local/.xmake/packages")
    else
        pkgroot = "/opt/prisma-toolchain/win-packages"
    end
end

-- xmake's --file description scope has no error()/raise() helper in the pinned
-- version. Print the reason and call an undefined sentinel to abort configure.
local function die(msg)
    print("error: " .. msg)
    return STAGED_WINDOWS_TOOLCHAIN_NOT_FOUND()
end

local function staged_lib(letter, name, libname)
    for _, dir in ipairs(os.dirs(path.join(pkgroot, letter, name, "*", "*", "lib"))) do
        if os.isfile(path.join(dir, libname .. ".lib")) then
            return dir
        end
    end
    die("staged " .. libname .. ".lib not found under " .. pkgroot ..
        " -- set PRISMA_WIN_PACKAGES or PRISMA_WIN_TOOLCHAIN")
end

add_linkdirs(staged_lib("s", "spdlog", "spdlog"))
add_links("spdlog")

includes("xmake.lua")
