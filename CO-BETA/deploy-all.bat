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

powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%tools\deploy-staged-data.ps1" -StagedData "%STAGED_DATA%" -DataDir "%DATA_DIR%"
if errorlevel 1 (
    echo Deployment failed.
    exit /b 1
)

call "%SCRIPT_DIR%tools\debug-log.bat" Deploy "Deployment complete target=%DATA_DIR%"
echo Deployment complete.
echo   %DATA_DIR%\F4SE\Plugins\PrismaUI_F4.dll
echo   %DATA_DIR%\F4SE\Plugins\CommonwealthOnlineUI.dll
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

endlocal
exit /b 0
