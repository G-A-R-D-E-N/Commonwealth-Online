@echo off
setlocal EnableExtensions

set "SCRIPT_DIR=%~dp0"
call "%SCRIPT_DIR%tools\debug-log.bat" BuildAll "Full beta build requested"

echo Building Commonwealth Online plugin...
call "%SCRIPT_DIR%build.bat" %*
if errorlevel 1 exit /b 1

echo.
if not exist "%SCRIPT_DIR%creation-kit\scripts\compiled\CommonwealthOnlineDebug.pex" (
    echo ERROR: CommonwealthOnlineDebug.pex is missing from the checked-in package contract.
    echo Run compile-papyrus.bat only when intentionally regenerating Papyrus outputs.
    exit /b 1
)
if not exist "%SCRIPT_DIR%creation-kit\scripts\compiled\COCharacterSetupQuestScript.pex" (
    echo ERROR: COCharacterSetupQuestScript.pex is missing from the checked-in package contract.
    echo Run compile-papyrus.bat only when intentionally regenerating Papyrus outputs.
    exit /b 1
)
if not exist "%SCRIPT_DIR%creation-kit\scripts\compiled\COQuestSyncScript.pex" (
    echo ERROR: COQuestSyncScript.pex is missing from the checked-in package contract.
    echo Run compile-papyrus.bat only when intentionally regenerating Papyrus outputs.
    exit /b 1
)
if not exist "%SCRIPT_DIR%creation-kit\scripts\compiled\CommonwealthOnlineQuest.pex" (
    echo ERROR: CommonwealthOnlineQuest.pex is missing from the checked-in package contract.
    echo Run compile-papyrus.bat only when intentionally regenerating Papyrus outputs.
    exit /b 1
)

echo Staging mod package...
call "%SCRIPT_DIR%stage-mod.bat"
if errorlevel 1 exit /b 1

call "%SCRIPT_DIR%tools\debug-log.bat" BuildAll "Plugin Papyrus and staging completed"
echo.
echo All builds finished. Mod package is in:
echo   %SCRIPT_DIR%build\Fallout 4\Data
echo.
echo Merge build\Fallout 4 into your Fallout 4 install,
echo or run deploy-all.bat to install directly.

endlocal
exit /b 0
