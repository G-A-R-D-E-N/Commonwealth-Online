-- Native clang-cl/xwin build of the F4SE plugin, replacing the Wine+MSVC path.
--
-- Same shape as PrismaPrivate's xmake-xwin.lua: configure the cross toolchain
-- and the staged Windows libraries here, then include the normal project
-- description unchanged, so there is exactly one definition of the targets.
--
--   xmake f --file=xmake-xwin.lua --plat=windows --arch=x64 -m release -y
--   xmake build --file=xmake-xwin.lua CommonwealthOnline

includes("toolchains/xwin-clang-cl.lua")
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
-- so no runner needs its own copy and no ACL is involved -- unlike the Wine tree
-- this replaces, which had to be staged per runner at 0750.
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

local function staged_package(letter, name, version, file)
    for _, dir in ipairs(os.dirs(path.join(pkgroot, letter, name, version, "*"))) do
        if os.isfile(path.join(dir, file)) then
            return dir
        end
    end
    print("warning: staged " .. name .. " " .. version .. " (" .. file .. ") not found under " .. pkgroot ..
        "; xmake will build it from source")
end

local safetyhook_dir = staged_package("s", "safetyhook", "v0.7.0", "lib/safetyhook.lib")
local zydis_dir = staged_package("z", "zydis", "v4.1.0", "lib/Zydis.lib")
local zycore_dir = staged_package("z", "zycore-c", "v1.5.0", "lib/Zycore.lib")
local json_dir = staged_package("n", "nlohmann_json", "v3.12.0", "include/nlohmann/json.hpp")

-- Must match the add_requires("safetyhook v0.7.0") pin in xmake.lua.
if safetyhook_dir and zydis_dir and zycore_dir then
    package("safetyhook")
        on_fetch(function (package)
            return {
                version = "0.7.0",
                includedirs = { path.join(safetyhook_dir, "include") },
                linkdirs = { path.join(safetyhook_dir, "lib"), path.join(zydis_dir, "lib"), path.join(zycore_dir, "lib") },
                links = { "safetyhook", "Zydis", "Zycore" }
            }
        end)
    package_end()
end

-- package.tools.cmake requires Visual Studio when the package platform is windows.
local opus_patch = path.join(os.scriptdir(), "patches", "libopus", "1.5.2", "add-sse4.1-flag-when-using-clang-cl.patch")
local opus_toolchain = path.join(os.scriptdir(), "..", "ci", "toolchains", "xwin-clang-cl.cmake")

package("libopus")
    set_homepage("https://opus-codec.org")
    set_description("Modern audio compression for the internet.")
    set_license("BSD-3-Clause")

    add_urls("https://downloads.xiph.org/releases/opus/opus-$(version).tar.gz", {alias = "home"})
    add_urls("https://github.com/xiph/opus/archive/refs/tags/v$(version).tar.gz", {alias = "github"})
    add_versions("home:1.5.2", "65c1d2f78b9f2fb20082c38cbe47c951ad5839345876e46941612ee87f9a7ce1")
    add_versions("github:1.5.2", "9480e329e989f70d69886ded470c7f8cfe6c0667cc4196d4837ac9e668fb7404")
    add_patches("1.5.2", opus_patch, "a5810124b43a3a80dc311192a49027869bcd244859a42f8338d62ae0525ab45b")
    add_deps("cmake")

    on_install(function (package)
        io.replace("CMakeLists.txt", [[set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")]], "", {plain = true})
        io.replace("CMakeLists.txt", [[set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")]], "", {plain = true})
        io.replace("silk/x86/NSQ_del_dec_avx2.c", "#include <Intsafe.h>", "#include <intsafe.h>", {plain = true})

        local toolchain = path.absolute(opus_toolchain)
        if not os.isfile(toolchain) then
            raise("xwin cmake toolchain not found: " .. toolchain)
        end
        local xwin_root = os.getenv("XWIN_ROOT") or "/opt/xwin"
        local builddir = "build-xwin"
        local configs = {
            "-S", ".",
            "-B", builddir,
            "-G", "Ninja",
            "-DCMAKE_TOOLCHAIN_FILE=" .. toolchain,
            "-DXWIN_ROOT=" .. xwin_root,
            "-DCMAKE_BUILD_TYPE=Release",
            "-DCMAKE_INSTALL_PREFIX=" .. path.absolute(package:installdir()),
            "-DBUILD_SHARED_LIBS=" .. (package:config("shared") and "ON" or "OFF"),
            "-DOPUS_STATIC_RUNTIME=OFF",
            "-DOPUS_PACKAGE_VERSION=" .. package:version():shortstr(),
            "-DAVX_SUPPORTED=ON",
            "-DOPUS_X86_MAY_HAVE_AVX=ON"
        }
        os.vrunv("cmake", configs)
        os.vrunv("cmake", {"--build", builddir})
        os.vrunv("cmake", {"--install", builddir})
    end)

    on_test(function (package)
        assert(package:has_cfuncs("opus_encoder_create", {includes = "opus/opus.h"}))
    end)
package_end()

-- Must match the add_requires("nlohmann_json v3.12.0") pin in xmake.lua.
if json_dir then
    package("nlohmann_json")
        on_fetch(function (package)
            return { version = "3.12.0", includedirs = { path.join(json_dir, "include") } }
        end)
    package_end()
end

includes("xmake.lua")
