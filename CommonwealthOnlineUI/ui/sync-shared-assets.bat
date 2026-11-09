@echo off
setlocal EnableExtensions

set "SCRIPT_DIR=%~dp0"
set "SHARED_ICONS=%SCRIPT_DIR%views\CommonwealthOnline\shared\icons"

if not exist "%SHARED_ICONS%" mkdir "%SHARED_ICONS%"

echo Syncing controller icons to shared views folder...
xcopy /Y /E /I "%SCRIPT_DIR%Icons\*" "%SHARED_ICONS%\"
if errorlevel 1 (
    echo Failed to copy icons from ui\Icons.
    exit /b 1
)

echo Shared assets ready under ui\views\CommonwealthOnline\shared\
endlocal
exit /b 0
