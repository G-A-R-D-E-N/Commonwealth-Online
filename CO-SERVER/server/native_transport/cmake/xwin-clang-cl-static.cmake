# CO-SERVER Windows x64 cross-compile toolchain: clang-cl + the xwin-provided
# MSVC CRT and Windows SDK, targeting the MSVC ABI from Linux.
#
# Derived from GHRUNNER configs/toolchains/xwin-clang-cl.cmake (commit
# cd7f919c5cd3733161dd18dd24516ed8aa277b84). The one deliberate divergence is
# the CRT: GHRUNNER's shared toolchain selects MultiThreadedDLL because the
# CO-BETA bridge it was proven against links /MD. CO-SERVER packages a
# standalone server that must run on a machine with no Visual C++
# redistributable installed, so this file selects the static release CRT and
# server/native_transport/CMakeLists.txt keeps CMAKE_MSVC_RUNTIME_LIBRARY in
# agreement.
#
# Wine is not required to compile or link. It is only used, if configured, as
# CMAKE_CROSSCOMPILING_EMULATOR to execute generated PE test binaries.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

set(XWIN_ROOT "/opt/xwin" CACHE PATH "Root containing xwin CRT and Windows SDK trees")

set(CMAKE_C_COMPILER   clang-cl)
set(CMAKE_CXX_COMPILER clang-cl)
set(CMAKE_LINKER       lld-link)
set(CMAKE_AR           llvm-lib)
set(CMAKE_RC_COMPILER  llvm-rc)
set(CMAKE_MT           llvm-mt)

set(CMAKE_C_COMPILER_TARGET   x86_64-pc-windows-msvc)
set(CMAKE_CXX_COMPILER_TARGET x86_64-pc-windows-msvc)

foreach(_required
    "${XWIN_ROOT}/crt/include"
    "${XWIN_ROOT}/crt/lib/x86_64"
    "${XWIN_ROOT}/sdk/include/ucrt"
    "${XWIN_ROOT}/sdk/include/um"
    "${XWIN_ROOT}/sdk/include/shared"
    "${XWIN_ROOT}/sdk/include/winrt"
    "${XWIN_ROOT}/sdk/lib/ucrt/x86_64"
    "${XWIN_ROOT}/sdk/lib/um/x86_64")
  if(NOT EXISTS "${_required}")
    message(FATAL_ERROR "xwin toolchain path is missing: ${_required}")
  endif()
endforeach()

# The static release CRT this toolchain selects. xwin ships no debug CRT
# (libcmtd.lib / libucrtd.lib are absent), so Debug configurations cannot link
# and every configuration must be Release.
foreach(_required_lib
    "${XWIN_ROOT}/crt/lib/x86_64/libcmt.lib"
    "${XWIN_ROOT}/crt/lib/x86_64/libcpmt.lib"
    "${XWIN_ROOT}/crt/lib/x86_64/libvcruntime.lib"
    "${XWIN_ROOT}/sdk/lib/ucrt/x86_64/libucrt.lib")
  if(NOT EXISTS "${_required_lib}")
    message(FATAL_ERROR
      "static CRT library is missing from the xwin installation: ${_required_lib}")
  endif()
endforeach()

set(_xwin_incs
  "/imsvc${XWIN_ROOT}/crt/include"
  "/imsvc${XWIN_ROOT}/sdk/include/ucrt"
  "/imsvc${XWIN_ROOT}/sdk/include/um"
  "/imsvc${XWIN_ROOT}/sdk/include/shared"
  "/imsvc${XWIN_ROOT}/sdk/include/winrt")
string(JOIN " " _xwin_inc_flags ${_xwin_incs})

set(_xwin_libs
  "/libpath:${XWIN_ROOT}/crt/lib/x86_64"
  "/libpath:${XWIN_ROOT}/sdk/lib/ucrt/x86_64"
  "/libpath:${XWIN_ROOT}/sdk/lib/um/x86_64")
string(JOIN " " _xwin_lib_flags ${_xwin_libs})

# CMake normally links C/C++ targets through the compiler driver. Force the
# LLVM linker there; CMAKE_LINKER covers cases where CMake invokes lld-link
# directly. Keeping -fuse-ld=lld in compiler flags prevents it from being
# passed as an lld-link argument.
set(_xwin_driver_flags "--target=x86_64-pc-windows-msvc -fuse-ld=lld")
set(CMAKE_C_FLAGS_INIT   "${_xwin_driver_flags} ${_xwin_inc_flags}")
set(CMAKE_CXX_FLAGS_INIT "${_xwin_driver_flags} ${_xwin_inc_flags}")
foreach(_t EXE SHARED MODULE)
  set(CMAKE_${_t}_LINKER_FLAGS_INIT "${_xwin_lib_flags}")
endforeach()

set(CMAKE_FIND_ROOT_PATH "${XWIN_ROOT}/crt" "${XWIN_ROOT}/sdk")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# CMake's own compiler probes default to Debug; pin them to Release so they do
# not look for the debug CRT xwin does not ship.
set(CMAKE_TRY_COMPILE_CONFIGURATION Release)
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)
