@echo off
setlocal EnableExtensions

set "SCRIPT_DIR=%~dp0"
set "PLUGIN_DIR=%SCRIPT_DIR%plugin"
call "%SCRIPT_DIR%tools\debug-log.bat" Build "Starting plugin build from %PLUGIN_DIR%"

cd /d "%PLUGIN_DIR%"
if errorlevel 1 (
    echo Failed to change to plugin directory:
    echo "%PLUGIN_DIR%"
    pause
    exit /b 1
)

where xmake >nul 2>&1
if errorlevel 1 (
    echo xmake was not found on PATH.
    echo Install XMake and add it to PATH: https://xmake.io
    pause
    exit /b 1
)

echo Configuring Commonwealth Online plugin...
call "%SCRIPT_DIR%tools\debug-log.bat" Build "Running xmake configure"
xmake f -y
if errorlevel 1 (
    echo xmake configuration failed.
    pause
    exit /b 1
)
xmake
if errorlevel 1 (
    echo xmake configuration failed.
    pause
    exit /b 1
)

echo Building Commonwealth Online plugin (AE, 1.11.240)...
call "%SCRIPT_DIR%tools\debug-log.bat" Build "Running xmake build CommonwealthOnline"
xmake build CommonwealthOnline
if errorlevel 1 (
    echo Build failed.
    pause
    exit /b 1
)

echo Building Commonwealth Online plugin (NG, 1.10.984)...
call "%SCRIPT_DIR%tools\debug-log.bat" Build "Running xmake build CommonwealthOnlineNG"
xmake build CommonwealthOnlineNG
if errorlevel 1 (
    echo NG build failed.
    pause
    exit /b 1
)

echo Building Commonwealth Online plugin (Old-Gen, 1.10.163)...
call "%SCRIPT_DIR%tools\debug-log.bat" Build "Running xmake build CommonwealthOnlineOG"
xmake build CommonwealthOnlineOG
if errorlevel 1 (
    echo Old-Gen build failed.
    pause
    exit /b 1
)

cd /d "%SCRIPT_DIR%"
if errorlevel 1 exit /b 1

echo Building GameNetworkingSockets client bridge...
call "%SCRIPT_DIR%build-gns-bridge.bat"
if errorlevel 1 (
    echo GNS bridge build failed.
    pause
    exit /b 1
)

call "%SCRIPT_DIR%tools\debug-log.bat" Build "Plugin and GNS bridge build completed successfully"
echo.
echo Build succeeded.
echo   plugin\build\windows\x64\release\CommonwealthOnline.dll
echo   plugin\build\windows\x64\release\CommonwealthOnline-NG.dll
echo   plugin\build\windows\x64\release\CommonwealthOnline-OG.dll
echo   plugin\build\gns-bridge\bin\commonwealth_online_gns_bridge.dll

echo.
echo Staging mod package...
call "%SCRIPT_DIR%stage-mod.bat"
if errorlevel 1 (
    echo Staging failed.
    pause
    exit /b 1
)

endlocal
exit /b 0
