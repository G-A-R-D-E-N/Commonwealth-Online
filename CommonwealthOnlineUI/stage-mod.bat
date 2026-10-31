@echo off
REM Assembles CommonwealthOnlineUI into build/Fallout 4/Data and package/.
REM PrismaUI_F4 2.1.0 is installed separately.
setlocal EnableExtensions

set "SCRIPT_DIR=%~dp0"

powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%scripts\stage-package.ps1"
if errorlevel 1 exit /b 1

echo.
echo Mod staged successfully.
echo   %SCRIPT_DIR%build\Fallout 4\Data
echo   %SCRIPT_DIR%package\

endlocal
exit /b 0
