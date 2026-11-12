package("quickjs-ng-xwin")
    set_homepage("https://github.com/quickjs-ng/quickjs")
    set_description("QuickJS, the Next Generation, compiled for the Windows x64 cross lane with the project's own clang-cl toolchain")
    set_license("MIT")

    add_urls("https://github.com/quickjs-ng/quickjs/archive/refs/tags/$(version).tar.gz",
             "https://github.com/quickjs-ng/quickjs.git")
    add_versions("v0.15.1", "c4e813951b7c46845096a948e978c620b11ab4cf5fd622ca09c727ec31f42623")

    add_configs("libc", {description = "Build the standard library module set into the library", default = false, type = "boolean"})

    on_install(function (package)
        local xwin_root = os.getenv("XWIN_ROOT") or "/opt/xwin"
        local cc = package:build_getenv("cc")
        local ar = package:build_getenv("ar") or "llvm-ar"

        local sysroot = {
            "--target=x86_64-pc-windows-msvc",
            "/imsvc" .. path.join(xwin_root, "crt/include"),
            "/imsvc" .. path.join(xwin_root, "sdk/include/ucrt"),
            "/imsvc" .. path.join(xwin_root, "sdk/include/um"),
            "/imsvc" .. path.join(xwin_root, "sdk/include/shared"),
            "/imsvc" .. path.join(xwin_root, "sdk/include/winrt")
        }

        local runtime = table.wrap(package:runtimes())[1] or "MD"

        local sources = { "dtoa.c", "libregexp.c", "libunicode.c", "quickjs.c" }
        if package:config("libc") then
            table.insert(sources, "quickjs-libc.c")
        end

        local defines = {
            "/DNDEBUG",
            "/D_GNU_SOURCE",
            "/DWIN32_LEAN_AND_MEAN",
            "/D_WIN32_WINNT=0x0601",
            "/DQUICKJS_NG_BUILD",
            "/D_CRT_SECURE_NO_WARNINGS",
            "/D_CRT_NONSTDC_NO_DEPRECATE"
        }
        if package:config("libc") then
            table.insert(defines, "/DQJS_BUILD_LIBC")
        end

        local flags = table.join({ "/nologo", "/c", "/O2", "/" .. runtime }, sysroot, defines)
        local objects = {}
        for _, source in ipairs(sources) do
            local object = source .. ".obj"
            os.vrunv(cc, table.join(flags, { source, "/Fo" .. object }))
            table.insert(objects, object)
        end
        os.vrunv(ar, table.join({ "-rcs", "qjs.lib" }, objects))

        os.trycp("quickjs.h", package:installdir("include"))
        os.trycp("quickjs-libc.h", package:installdir("include"))
        os.cp("qjs.lib", package:installdir("lib"))
        package:add("links", "qjs")
    end)
