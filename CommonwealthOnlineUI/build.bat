@echo off
setlocal EnableExtensions

set "SCRIPT_DIR=%~dp0"

cd /d "%SCRIPT_DIR%"
if errorlevel 1 (
    echo Failed to change to repo directory:
    echo "%SCRIPT_DIR%"
    exit /b 1
)

where xmake >nul 2>&1
if errorlevel 1 (
    echo xmake was not found on PATH.
    echo Install xmake 3.0.0 or newer from https://xmake.io
    exit /b 1
)

if not exist "%SCRIPT_DIR%ThirdParty\CommonLibF4\xmake.lua" (
    echo CommonLibF4 submodule missing. Run:
    echo   git submodule update --init --recursive
    exit /b 1
)

if not exist "%SCRIPT_DIR%include\PrismaUI_F4_API.h" (
    echo Prisma V12 API header missing:
    echo   %SCRIPT_DIR%include\PrismaUI_F4_API.h
    exit /b 1
)

echo.
echo Building CommonwealthOnlineUI...
xmake f -P . --plat=windows --arch=x64 -m releasedbg
if errorlevel 1 exit /b 1
xmake build -P . -r CommonwealthOnlineUI
if errorlevel 1 exit /b 1

echo.
echo Build succeeded.
echo   CommonwealthOnlineUI.dll

echo.
echo Staging mod package...
call "%SCRIPT_DIR%stage-mod.bat"
if errorlevel 1 (
    echo Staging failed.
    exit /b 1
)

echo.
echo All done. Mod package is in:
echo   %SCRIPT_DIR%build\Fallout 4\Data
echo.
echo Run deploy.bat to install CommonwealthOnlineUI into Fallout 4 Data.
echo Per getting-started.md: PrismaUI_F4 must load before this plugin.

endlocal
exit /b 0
