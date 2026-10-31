@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "SCRIPT_DIR=%~dp0"
set "GNS_DIR=%SCRIPT_DIR%ThirdParty\GameNetworkingSockets-src"
set "BRIDGE_DIR=%SCRIPT_DIR%plugin\gns-bridge"
set "BUILD_DIR=%SCRIPT_DIR%plugin\build\gns-bridge"
set "VCPKG_DIR=%SCRIPT_DIR%_deps\vcpkg"
set "VCPKG_INSTALLED_DIR=%SCRIPT_DIR%_deps\vcpkg-installed"
set "GNS_SHA=f4525e39ee10f6b45181fd92d01fee75f7d71756"
set "VCPKG_SHA=e4ed9022bd97e54d848104f7e1ab153d9532a57e"
set "VCPKG_TOOL_RELEASE_TAG=2026-07-27"
set "BRIDGE_DLL=%BUILD_DIR%\bin\commonwealth_online_gns_bridge.dll"

pushd "%SCRIPT_DIR%" >nul
if errorlevel 1 exit /b 1

if /I not "%CO_GNS_NATIVE_PREPARED%"=="1" (
    where git >nul 2>&1
    if errorlevel 1 (
        echo ERROR: git was not found on PATH.
        popd >nul
        exit /b 1
    )

    git submodule update --init --depth 1 -- ThirdParty/GameNetworkingSockets-src
    if errorlevel 1 (
        echo ERROR: Failed to initialize the pinned GameNetworkingSockets submodule.
        popd >nul
        exit /b 1
    )

    set "GNS_HEAD="
    for /f "delims=" %%I in ('git -C "%GNS_DIR%" rev-parse HEAD') do set "GNS_HEAD=%%I"
    if /I not "!GNS_HEAD!"=="%GNS_SHA%" (
        echo ERROR: GameNetworkingSockets is at !GNS_HEAD! but %GNS_SHA% is required.
        popd >nul
        exit /b 1
    )

    if not exist "%SCRIPT_DIR%_deps" mkdir "%SCRIPT_DIR%_deps"
    if not exist "%VCPKG_DIR%\.git" (
        if exist "%VCPKG_DIR%" rmdir /s /q "%VCPKG_DIR%"
        git init "%VCPKG_DIR%" >nul
        if errorlevel 1 (
            echo ERROR: Failed to initialize the local vcpkg checkout.
            popd >nul
            exit /b 1
        )
        git -C "%VCPKG_DIR%" remote add origin https://github.com/microsoft/vcpkg.git
        if errorlevel 1 (
            echo ERROR: Failed to configure the local vcpkg checkout.
            popd >nul
            exit /b 1
        )
    )

    git -C "%VCPKG_DIR%" fetch --depth 1 origin "%VCPKG_SHA%"
    if errorlevel 1 (
        echo ERROR: Failed to fetch pinned vcpkg revision %VCPKG_SHA%.
        popd >nul
        exit /b 1
    )

    git -C "%VCPKG_DIR%" checkout --detach --force FETCH_HEAD >nul
    if errorlevel 1 (
        echo ERROR: Failed to check out pinned vcpkg revision %VCPKG_SHA%.
        popd >nul
        exit /b 1
    )

    set "VCPKG_HEAD="
    for /f "delims=" %%I in ('git -C "%VCPKG_DIR%" rev-parse HEAD') do set "VCPKG_HEAD=%%I"
    if /I not "!VCPKG_HEAD!"=="%VCPKG_SHA%" (
        echo ERROR: vcpkg is at !VCPKG_HEAD! but %VCPKG_SHA% is required.
        popd >nul
        exit /b 1
    )
) else (
    if not exist "%GNS_DIR%\CMakeLists.txt" (
        echo ERROR: Prepared GameNetworkingSockets source is missing.
        popd >nul
        exit /b 1
    )
    if not exist "%VCPKG_DIR%\.git" (
        echo ERROR: Prepared vcpkg checkout is missing.
        popd >nul
        exit /b 1
    )
)

if not exist "%VCPKG_DIR%\vcpkg.exe" (
    set "VCPKG_DOWNLOADER=%VCPKG_DIR%\scripts\tls12-download.exe"
    if not exist "!VCPKG_DOWNLOADER!" (
        echo ERROR: vcpkg TLS downloader is missing.
        popd >nul
        exit /b 1
    )
    "!VCPKG_DOWNLOADER!" github.com "/microsoft/vcpkg-tool/releases/download/%VCPKG_TOOL_RELEASE_TAG%/vcpkg.exe" "%VCPKG_DIR%\vcpkg.exe"
    if errorlevel 1 (
        echo ERROR: vcpkg bootstrap download failed.
        popd >nul
        exit /b 1
    )
)

"%VCPKG_DIR%\vcpkg.exe" version --disable-metrics
if errorlevel 1 (
    echo ERROR: vcpkg.exe could not run.
    popd >nul
    exit /b 1
)

"%VCPKG_DIR%\vcpkg.exe" install ^
    --triplet x64-windows-static-md ^
    --x-manifest-root="%BRIDGE_DIR%" ^
    --x-install-root="%VCPKG_INSTALLED_DIR%" ^
    --disable-metrics
if errorlevel 1 (
    echo ERROR: vcpkg dependency installation failed.
    popd >nul
    exit /b 1
)

set "CMAKE_CMD="
set "CTEST_CMD="
for /f "delims=" %%I in ('where cmake.exe 2^>nul') do if not defined CMAKE_CMD set "CMAKE_CMD=%%I"
if defined CMAKE_CMD (
    for %%I in ("!CMAKE_CMD!") do set "CTEST_CMD=%%~dpIctest.exe"
) else (
    for /f "delims=" %%I in ('where /r "%VCPKG_DIR%\downloads\tools" cmake.exe 2^>nul') do if not defined CMAKE_CMD (
        set "CMAKE_CMD=%%I"
        set "CTEST_CMD=%%~dpIctest.exe"
    )
)
if not defined CMAKE_CMD (
    echo ERROR: vcpkg did not provision cmake.exe.
    popd >nul
    exit /b 1
)
if not exist "!CTEST_CMD!" (
    echo ERROR: ctest.exe was not found next to !CMAKE_CMD!.
    popd >nul
    exit /b 1
)

echo Configuring GameNetworkingSockets client bridge...
"!CMAKE_CMD!" -S "%BRIDGE_DIR%" -B "%BUILD_DIR%" -A x64 ^
    -DCMAKE_TOOLCHAIN_FILE="%VCPKG_DIR%\scripts\buildsystems\vcpkg.cmake" ^
    -DVCPKG_TARGET_TRIPLET=x64-windows-static-md ^
    -DVCPKG_MANIFEST_DIR="%BRIDGE_DIR%" ^
    -DVCPKG_INSTALLED_DIR="%VCPKG_INSTALLED_DIR%" ^
    -DGNS_SOURCE_DIR="%GNS_DIR%" ^
    -DCO_GNS_BUILD_TESTS=ON
if errorlevel 1 (
    echo ERROR: GNS bridge configuration failed.
    popd >nul
    exit /b 1
)

echo Building GameNetworkingSockets client bridge...
"!CMAKE_CMD!" --build "%BUILD_DIR%" --config Release --parallel 2
if errorlevel 1 (
    echo ERROR: GNS bridge build failed.
    popd >nul
    exit /b 1
)

"!CTEST_CMD!" --test-dir "%BUILD_DIR%" -C Release --output-on-failure
if errorlevel 1 (
    echo ERROR: GNS bridge smoke test failed.
    popd >nul
    exit /b 1
)

if not exist "%BRIDGE_DLL%" (
    echo ERROR: commonwealth_online_gns_bridge.dll was not produced at:
    echo   %BRIDGE_DLL%
    popd >nul
    exit /b 1
)

echo GNS bridge ready:
echo   %BRIDGE_DLL%
popd >nul
endlocal
exit /b 0
