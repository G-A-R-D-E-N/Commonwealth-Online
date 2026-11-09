@echo off
setlocal EnableExtensions

set "SCRIPT_DIR=%~dp0"
call "%SCRIPT_DIR%tools\debug-log.bat" Deploy "Full deployment requested"

call "%SCRIPT_DIR%resolve-fallout4-path.bat" "%~1"
if errorlevel 1 exit /b 1

echo.
echo Compiling Papyrus scripts...
call "%SCRIPT_DIR%compile-papyrus.bat" "%F4_DIR%"
if errorlevel 1 exit /b 1
if not exist "%SCRIPT_DIR%creation-kit\scripts\compiled\CommonwealthOnlineDebug.pex" (
    echo ERROR: CommonwealthOnlineDebug.pex was not produced. Beta deployment is blocked.
    exit /b 1
)
if not exist "%SCRIPT_DIR%creation-kit\scripts\compiled\COCharacterSetupQuestScript.pex" (
    echo ERROR: COCharacterSetupQuestScript.pex was not produced. Beta deployment is blocked.
    exit /b 1
)
if not exist "%SCRIPT_DIR%creation-kit\scripts\compiled\COQuestSyncScript.pex" (
    echo ERROR: COQuestSyncScript.pex was not produced. Beta deployment is blocked.
    exit /b 1
)
if not exist "%SCRIPT_DIR%creation-kit\scripts\compiled\CommonwealthOnlineQuest.pex" (
    echo ERROR: CommonwealthOnlineQuest.pex was not produced. Beta deployment is blocked.
    exit /b 1
)

echo.
echo Staging mod package...
call "%SCRIPT_DIR%stage-mod.bat"
if errorlevel 1 exit /b 1

set "STAGED_DATA=%SCRIPT_DIR%build\Fallout 4\Data"

if not exist "%STAGED_DATA%" (
    echo Staged Data folder not found:
    echo   %STAGED_DATA%
    exit /b 1
)

echo.
echo Deploying staged mod to:
echo   %DATA_DIR%
echo.
call "%SCRIPT_DIR%tools\debug-log.bat" Deploy "Deploy source=%STAGED_DATA% target=%DATA_DIR%"

call :KeepUserAddictolFiles save
powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%tools\deploy-staged-data.ps1" -StagedData "%STAGED_DATA%" -DataDir "%DATA_DIR%"
set "DEPLOY_RESULT=%errorlevel%"
call :KeepUserAddictolFiles restore
if not "%DEPLOY_RESULT%"=="0" (
    echo Deployment failed.
    exit /b 1
)

echo.
echo Deploying the native server package to:
echo   %DATA_DIR%\server
echo.
powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%tools\deploy-server.ps1" -DataDir "%DATA_DIR%"
if errorlevel 1 (
    echo Native server deployment failed.
    exit /b 1
)

call "%SCRIPT_DIR%tools\debug-log.bat" Deploy "Deployment complete target=%DATA_DIR%"
echo Deployment complete.
echo   %DATA_DIR%\F4SE\Plugins\PrismaUI_F4.dll
echo   %DATA_DIR%\F4SE\Plugins\CommonwealthOnline.dll
echo   %DATA_DIR%\PrismaUI_F4\views\CommonwealthOnline\browser\index.html
echo   %DATA_DIR%\PrismaUI_F4\libs\*.dll
echo   %DATA_DIR%\PrismaUI_F4\resources\
echo   %DATA_DIR%\*.esp
echo   %DATA_DIR%\Materials\
echo   %DATA_DIR%\Meshes\
echo   %DATA_DIR%\Textures\
echo   %DATA_DIR%\Scripts\*.pex
echo   %DATA_DIR%\Scripts\Source\User\*.psc
echo   %DATA_DIR%\server\  (when a server package was found)

endlocal
exit /b 0

:KeepUserAddictolFiles
if "%~1"=="restore" goto RestoreUserAddictolFiles
del /q "%DATA_DIR%\F4SE\Plugins\*.user-keep" >nul 2>&1
for %%F in ("%DATA_DIR%\F4SE\Plugins\Addictol*.toml" "%DATA_DIR%\F4SE\Plugins\Addictol_*.ini") do (
    if exist "%%~fF" copy /Y "%%~fF" "%%~fF.user-keep" >nul
)
exit /b 0

:RestoreUserAddictolFiles
for %%F in ("%DATA_DIR%\F4SE\Plugins\*.user-keep") do move /Y "%%~fF" "%%~dpnF" >nul 2>&1
exit /b 0
