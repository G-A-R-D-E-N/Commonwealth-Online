@echo off
setlocal EnableExtensions

set "SCRIPT_DIR=%~dp0"

call "%SCRIPT_DIR%resolve-fallout4-path.bat" "%~1"
if errorlevel 1 exit /b 1

echo.
echo Staging UI package...
call "%SCRIPT_DIR%stage-mod.bat"
if errorlevel 1 exit /b 1

set "STAGED_DATA=%SCRIPT_DIR%build\Fallout 4\Data"

if not exist "%STAGED_DATA%\F4SE\Plugins\CommonwealthOnlineUI.dll" (
    echo Staged UI DLL not found:
    echo   %STAGED_DATA%\F4SE\Plugins\CommonwealthOnlineUI.dll
    echo Run build.bat first.
    exit /b 1
)

echo.
echo Deploying staged UI mod to:
echo   %DATA_DIR%
echo.

xcopy /Y /E /I /Q "%STAGED_DATA%\*" "%DATA_DIR%\" >nul
if errorlevel 1 (
    echo Deployment failed.
    exit /b 1
)

echo Deployment complete.
echo   %DATA_DIR%\F4SE\Plugins\CommonwealthOnlineUI.dll
echo   %DATA_DIR%\PrismaUI_F4\views\

if exist "%DATA_DIR%\F4SE\Plugins\PrismaUI_F4.dll" (
    echo PrismaUI_F4.dll found at:
    echo   %DATA_DIR%\F4SE\Plugins\PrismaUI_F4.dll
) else (
    echo WARNING: PrismaUI_F4 2.1.0 is not installed in Fallout 4 Data.
    echo Install the separate PrismaUI_F4 runtime before launching the game.
)

endlocal
exit /b 0
