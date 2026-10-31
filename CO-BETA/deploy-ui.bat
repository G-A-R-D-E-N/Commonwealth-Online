@echo off
setlocal EnableExtensions

set "SCRIPT_DIR=%~dp0"
set "UI_DIR=%SCRIPT_DIR%..\CommonwealthOnlineUI"
set "UI_PACKAGE=%SCRIPT_DIR%..\CommonwealthOnlineUI\package"

call "%SCRIPT_DIR%resolve-fallout4-path.bat" "%~1"
if errorlevel 1 exit /b 1

if not exist "%UI_DIR%\scripts\install-dev.ps1" (
    echo CommonwealthOnlineUI source package is missing. Expected:
    echo   ..\CommonwealthOnlineUI
    exit /b 1
)

if not exist "%UI_PACKAGE%\F4SE\Plugins\CommonwealthOnlineUI.dll" (
    echo UI package not staged. Build the reviewed top-level UI source first:
    echo   cd ..\CommonwealthOnlineUI
    echo   build.bat
    exit /b 1
)

echo Deploying CommonwealthOnlineUI package...
powershell -NoProfile -ExecutionPolicy Bypass -File "%UI_DIR%\scripts\install-dev.ps1" -Fallout4Path "%F4_DIR%"
if errorlevel 1 exit /b 1

if not "%~2"=="" (
    set "PLUGINS_DIR=%DATA_DIR%\F4SE\Plugins"
    if not exist "%PLUGINS_DIR%" mkdir "%PLUGINS_DIR%"
    echo Copying CommonwealthOnline.dll...
    copy /Y "%~2" "%PLUGINS_DIR%\CommonwealthOnline.dll"
    if errorlevel 1 (
        echo Failed to copy plugin DLL.
        exit /b 1
    )
)

echo.
echo UI deployment complete.

endlocal
exit /b 0
