@echo off
setlocal EnableExtensions

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..\..") do set "ROOT_DIR=%%~fI"
set "GNS_SHA=f4525e39ee10f6b45181fd92d01fee75f7d71756"
set "VCPKG_SHA=e4ed9022bd97e54d848104f7e1ab153d9532a57e"
set "DEPS_DIR=%CO_GNS_DEPS_DIR%"
if not defined DEPS_DIR set "DEPS_DIR=%ROOT_DIR%\_deps"
set "GNS_DIR=%DEPS_DIR%\GameNetworkingSockets-src"
set "VCPKG_DIR=%DEPS_DIR%\vcpkg"
set "VCPKG_INSTALLED_DIR=%DEPS_DIR%\vcpkg-installed-server"
set "BUILD_DIR=%CO_GNS_BUILD_DIR%"
if not defined BUILD_DIR set "BUILD_DIR=%ROOT_DIR%\build\gns-server-bridge-windows"
set "BRIDGE=%BUILD_DIR%\Release\commonwealth_online_gns_bridge.dll"

where git >nul 2>&1
if errorlevel 1 (
    echo ERROR: git was not found on PATH.
    exit /b 1
)
where cmake >nul 2>&1
if errorlevel 1 (
    echo ERROR: cmake was not found on PATH.
    exit /b 1
)

if not exist "%DEPS_DIR%" mkdir "%DEPS_DIR%"
if not exist "%GNS_DIR%\.git" (
    if exist "%GNS_DIR%" rmdir /s /q "%GNS_DIR%"
    git init "%GNS_DIR%" >nul
    if errorlevel 1 exit /b 1
    git -C "%GNS_DIR%" remote add origin https://github.com/ValveSoftware/GameNetworkingSockets.git
    if errorlevel 1 exit /b 1
)
git -C "%GNS_DIR%" fetch --depth 1 origin "%GNS_SHA%"
if errorlevel 1 exit /b 1
git -C "%GNS_DIR%" checkout --detach --force FETCH_HEAD >nul
if errorlevel 1 exit /b 1
set "GNS_HEAD="
for /f "delims=" %%I in ('git -C "%GNS_DIR%" rev-parse HEAD') do set "GNS_HEAD=%%I"
if /I not "%GNS_HEAD%"=="%GNS_SHA%" (
    echo ERROR: GameNetworkingSockets is at %GNS_HEAD% but %GNS_SHA% is required.
    exit /b 1
)

if not exist "%VCPKG_DIR%\.git" (
    if exist "%VCPKG_DIR%" rmdir /s /q "%VCPKG_DIR%"
    git init "%VCPKG_DIR%" >nul
    if errorlevel 1 exit /b 1
    git -C "%VCPKG_DIR%" remote add origin https://github.com/microsoft/vcpkg.git
    if errorlevel 1 exit /b 1
)
git -C "%VCPKG_DIR%" fetch --depth 1 origin "%VCPKG_SHA%"
if errorlevel 1 exit /b 1
git -C "%VCPKG_DIR%" checkout --detach --force FETCH_HEAD >nul
if errorlevel 1 exit /b 1
set "VCPKG_HEAD="
for /f "delims=" %%I in ('git -C "%VCPKG_DIR%" rev-parse HEAD') do set "VCPKG_HEAD=%%I"
if /I not "%VCPKG_HEAD%"=="%VCPKG_SHA%" (
    echo ERROR: vcpkg is at %VCPKG_HEAD% but %VCPKG_SHA% is required.
    exit /b 1
)
if not exist "%VCPKG_DIR%\vcpkg.exe" (
    call "%VCPKG_DIR%\bootstrap-vcpkg.bat" -disableMetrics
    if errorlevel 1 exit /b 1
)

cmake -S "%ROOT_DIR%\server\native_transport" -B "%BUILD_DIR%" -A x64 ^
    -DCMAKE_TOOLCHAIN_FILE="%VCPKG_DIR%\scripts\buildsystems\vcpkg.cmake" ^
    -DVCPKG_TARGET_TRIPLET=x64-windows-static ^
    -DVCPKG_MANIFEST_DIR="%ROOT_DIR%\server\native_transport" ^
    -DVCPKG_INSTALLED_DIR="%VCPKG_INSTALLED_DIR%" ^
    -DGNS_SOURCE_DIR="%GNS_DIR%" ^
    -DCO_GNS_BUILD_TESTS=ON ^
    -DCO_GNS_ENABLE_TEST_NETWORK=ON
if errorlevel 1 exit /b 1

cmake --build "%BUILD_DIR%" --config Release --parallel 2
if errorlevel 1 exit /b 1
ctest --test-dir "%BUILD_DIR%" -C Release --output-on-failure
if errorlevel 1 exit /b 1

if not exist "%BRIDGE%" (
    echo ERROR: native GNS bridge was not produced at %BRIDGE%.
    exit /b 1
)

echo %BRIDGE%
endlocal
exit /b 0
