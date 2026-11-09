@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "RELEASE_MODE=0"
if not "%~2"=="" (
    echo Usage: stage-mod.bat [--release]
    exit /b 2
)
if not "%~1"=="" (
    if /I "%~1"=="--release" (
        set "RELEASE_MODE=1"
    ) else (
        echo Usage: stage-mod.bat [--release]
        exit /b 2
    )
)

set "SCRIPT_DIR=%~dp0"
call "%SCRIPT_DIR%tools\debug-log.bat" Stage "Rebuilding staged Commonwealth Online package"
set "BUILD_DIR=%SCRIPT_DIR%build"
set "DATA_DIR=%BUILD_DIR%\Fallout 4\Data"
set "UI_DIR=%SCRIPT_DIR%..\CommonwealthOnlineUI"
set "UI_PACKAGE=%UI_DIR%\package"
set "ASSETS_DIR=%SCRIPT_DIR%creation-kit\assets"
set "ESP_DIR=%SCRIPT_DIR%creation-kit\esp"
set "SCRIPTS_COMPILED_DIR=%SCRIPT_DIR%creation-kit\scripts\compiled"
set "SCRIPTS_SOURCE_DIR=%SCRIPT_DIR%creation-kit\scripts\source"
set "CO_DLL=%SCRIPT_DIR%plugin\build\windows\x64\release\CommonwealthOnline.dll"
set "GNS_BRIDGE_DLL=%SCRIPT_DIR%plugin\build\gns-bridge\bin\commonwealth_online_gns_bridge.dll"
set "ADDICTOL_DIR=%SCRIPT_DIR%ThirdParty\Addictol"
set "ADDICTOL_DLL=%ADDICTOL_DIR%\.Build\F4SE\Plugins\Addictol.dll"
set "CRASH_LOGGER_DIR=%SCRIPT_DIR%ThirdParty\AddictolCrashLogger"
set "CRASH_LOGGER_DLL=%CRASH_LOGGER_DIR%\build\windows\x64\release\AddictolCrashLogger.dll"
set "UI_STAGED=0"

if not exist "%CO_DLL%" (
    echo CommonwealthOnline.dll not found. Run build.bat first.
    exit /b 1
)

if not exist "%GNS_BRIDGE_DLL%" (
    echo commonwealth_online_gns_bridge.dll not found. Run build-gns-bridge.bat first.
    exit /b 1
)

if not exist "%ADDICTOL_DLL%" (
    echo Addictol.dll not found. Build ThirdParty\Addictol first.
    exit /b 1
)

if not exist "%CRASH_LOGGER_DLL%" (
    echo AddictolCrashLogger.dll not found. Build ThirdParty\AddictolCrashLogger first.
    exit /b 1
)

set "PLUGINS_DIR=%DATA_DIR%\F4SE\Plugins"
set "CLIENT_MANIFEST=%PLUGINS_DIR%\CommonwealthOnlineClient.json"
set "CONTENT_FINGERPRINT_FILE=%BUILD_DIR%\content-manifest.txt"
set "CONTENT_FINGERPRINT_VERIFY_FILE=%BUILD_DIR%\content-manifest.verify.txt"

echo Staging mod to:
echo   %DATA_DIR%
if "%RELEASE_MODE%"=="1" echo   Mode: strict release package
echo.

if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"

if not exist "%PLUGINS_DIR%" mkdir "%PLUGINS_DIR%"

echo   F4SE\Plugins\CommonwealthOnline.dll
copy /Y "%CO_DLL%" "%PLUGINS_DIR%\CommonwealthOnline.dll" >nul

echo   F4SE\Plugins\commonwealth_online_gns_bridge.dll
copy /Y "%GNS_BRIDGE_DLL%" "%PLUGINS_DIR%\commonwealth_online_gns_bridge.dll" >nul

if exist "%UI_PACKAGE%\PrismaUI_F4\views" (
    echo   PrismaUI_F4 views
    xcopy /Y /E /I /Q "%UI_PACKAGE%\PrismaUI_F4\*" "%DATA_DIR%\PrismaUI_F4\" >nul
    set "UI_STAGED=1"
) else (
    echo   WARNING: PrismaUI_F4 views not found - staging the plugin DLL only.
    echo   Stage the views first:
    echo     cd ..\CommonwealthOnlineUI
    echo     powershell -File scripts\stage-package.ps1
)

if exist "%ESP_DIR%" (
    for %%F in ("%ESP_DIR%\*.esp") do (
        echo   %%~nxF
        copy /Y "%%~fF" "%DATA_DIR%\%%~nxF" >nul
    )
) else (
    echo   WARNING: creation-kit\esp not found - no plugin files staged.
)

call :StageAssetFolder Materials
call :StageAssetFolder Meshes
call :StageAssetFolder Textures
call :StageAssetFolder Sound
call :StageAssetFolder Seq

call :StagePapyrusScripts
call :ValidateReleasePayload
if errorlevel 1 exit /b 1

powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%tools\stage-client-manifest.ps1" -PluginDir "%SCRIPT_DIR%plugin" -DataDir "%DATA_DIR%" -ClientManifest "%CLIENT_MANIFEST%" -FingerprintFile "%CONTENT_FINGERPRINT_FILE%" -VerifyFile "%CONTENT_FINGERPRINT_VERIFY_FILE%" -RepoDir "%SCRIPT_DIR%.."
if errorlevel 1 (
    echo Failed to generate and verify CommonwealthOnlineClient.json from the staged package.
    exit /b 1
)

call :StageAddictol
if errorlevel 1 exit /b 1

call :AssembleRuntimePackages

call "%SCRIPT_DIR%tools\debug-log.bat" Stage "Staging complete data=%DATA_DIR%"
echo.
echo Mod staged successfully.
if "%UI_STAGED%"=="0" (
    echo Note: UI/Prisma package was not included. Custom menus will not work until it is built.
)
echo Content fingerprint:
type "%CONTENT_FINGERPRINT_FILE%"
echo Install runtime-specific package matching your Fallout 4 version:
echo   AE (1.11.240): %BUILD_DIR%\AE\Fallout 4\Data
echo   NG (1.10.984): %BUILD_DIR%\NG\Fallout 4\Data
if exist "%BUILD_DIR%\OG" echo   OG (1.10.163): %BUILD_DIR%\OG\Fallout 4\Data
echo Default staged AE tree for deploy-all.bat:
echo   %DATA_DIR%

endlocal
exit /b 0

:StageAssetFolder
set "FOLDER=%~1"
set "SOURCE="

if exist "%ASSETS_DIR%\%FOLDER%" set "SOURCE=%ASSETS_DIR%\%FOLDER%"
if not defined SOURCE if exist "%ASSETS_DIR%\DataFolder\%FOLDER%" set "SOURCE=%ASSETS_DIR%\DataFolder\%FOLDER%"

if not defined SOURCE exit /b 0

echo   %FOLDER%\
xcopy /Y /E /I /Q "%SOURCE%\*" "%DATA_DIR%\%FOLDER%\" >nul
exit /b 0

:StagePapyrusScripts
if not exist "%SCRIPTS_COMPILED_DIR%\CommonwealthOnlineDebug.pex" echo   WARNING: CommonwealthOnlineDebug.pex is missing. Run compile-papyrus.bat before beta deployment.
if not exist "%SCRIPTS_COMPILED_DIR%\COCharacterSetupQuestScript.pex" echo   WARNING: COCharacterSetupQuestScript.pex is missing. Run compile-papyrus.bat before beta deployment.
if not exist "%SCRIPTS_COMPILED_DIR%\COQuestSyncScript.pex" echo   WARNING: COQuestSyncScript.pex is missing. Run compile-papyrus.bat before beta deployment.
if not exist "%SCRIPTS_COMPILED_DIR%\CommonwealthOnlineQuest.pex" echo   WARNING: CommonwealthOnlineQuest.pex is missing. Run compile-papyrus.bat before beta deployment.
call :StagePapyrusFolder "%SCRIPTS_COMPILED_DIR%" "%DATA_DIR%\Scripts" "*.pex" "Scripts"
call :StagePapyrusFolder "%SCRIPTS_SOURCE_DIR%" "%DATA_DIR%\Scripts\Source\User" "*.psc" "Scripts\Source\User"
exit /b 0

:StagePapyrusFolder
set "PAPYRUS_SOURCE=%~1"
set "PAPYRUS_DEST=%~2"
set "PAPYRUS_PATTERN=%~3"
set "PAPYRUS_LABEL=%~4"

if not exist "%PAPYRUS_SOURCE%" exit /b 0

for /r "%PAPYRUS_SOURCE%" %%F in (%PAPYRUS_PATTERN%) do (
    set "BASE_NAME=%%~nF"
    set "STAGE_OK=0"
    if /i "!BASE_NAME!"=="COCharacterSetupQuestScript" set "STAGE_OK=1"
    if /i "!BASE_NAME!"=="COQuestSyncScript" set "STAGE_OK=1"
    if /i "!BASE_NAME!"=="CommonwealthOnlineDebug" set "STAGE_OK=1"
    if /i "!BASE_NAME!"=="CommonwealthOnlineQuest" set "STAGE_OK=1"
    if /i "!BASE_NAME!"=="CommonwealthOnlineNeeds" set "STAGE_OK=1"
    if "!STAGE_OK!"=="1" (
        set "REL=%%~fF"
        set "REL=!REL:%PAPYRUS_SOURCE%\=!"
        set "DEST_FILE=%PAPYRUS_DEST%\!REL!"
        for %%D in ("!DEST_FILE!") do set "DEST_DIR=%%~dpD"

        if not exist "!DEST_DIR!" mkdir "!DEST_DIR!"
        echo   %PAPYRUS_LABEL%\!REL!
        copy /Y "%%~fF" "!DEST_FILE!" >nul
    ) else (
        echo   Skipping non-shipped script: %%~nxF
    )
)
exit /b 0

:AssembleRuntimePackages
echo Assembling runtime-specific packages:
echo   build\AE
if exist "%BUILD_DIR%\AE" rmdir /s /q "%BUILD_DIR%\AE"
xcopy /Y /E /I /Q "%DATA_DIR%\*" "%BUILD_DIR%\AE\Fallout 4\Data\" >nul

echo   build\NG
if exist "%BUILD_DIR%\NG" rmdir /s /q "%BUILD_DIR%\NG"
xcopy /Y /E /I /Q "%DATA_DIR%\*" "%BUILD_DIR%\NG\Fallout 4\Data\" >nul

echo   build\OG
if exist "%BUILD_DIR%\OG" rmdir /s /q "%BUILD_DIR%\OG"
xcopy /Y /E /I /Q "%DATA_DIR%\*" "%BUILD_DIR%\OG\Fallout 4\Data\" >nul
exit /b 0

:StageAddictol
echo Staging Addictol plugins:
echo   F4SE\Plugins\Addictol.dll
copy /Y "%ADDICTOL_DLL%" "%PLUGINS_DIR%\Addictol.dll" >nul || exit /b 1
echo   F4SE\Plugins\AddictolCrashLogger.dll
copy /Y "%CRASH_LOGGER_DLL%" "%PLUGINS_DIR%\AddictolCrashLogger.dll" >nul || exit /b 1
for %%F in ("%ADDICTOL_DIR%\data\F4SE\Plugins\Addictol*.toml" "%ADDICTOL_DIR%\data\F4SE\Plugins\Addictol_*.ini" "%CRASH_LOGGER_DIR%\data\F4SE\Plugins\AddictolCrashLogger*.toml") do (
    echo   F4SE\Plugins\%%~nxF
    copy /Y "%%~fF" "%PLUGINS_DIR%\%%~nxF" >nul || exit /b 1
)
if not exist "%DATA_DIR%\Scripts" mkdir "%DATA_DIR%\Scripts"
for %%F in ("%ADDICTOL_DIR%\data\Scripts\Addictol.pex" "%ADDICTOL_DIR%\data\Scripts\XCELL.pex") do (
    echo   Scripts\%%~nxF
    copy /Y "%%~fF" "%DATA_DIR%\Scripts\%%~nxF" >nul || exit /b 1
)
call :StageAddictolLicenses "%DATA_DIR%\CommonwealthOnline\Licenses"
if errorlevel 1 exit /b 1
exit /b 0

:StageAddictolLicenses
set "LICENSE_DIR=%~1"
if not exist "%LICENSE_DIR%" mkdir "%LICENSE_DIR%"
copy /Y "%ADDICTOL_DIR%\LICENSE" "%LICENSE_DIR%\Addictol-LICENSE.txt" >nul || exit /b 1
copy /Y "%ADDICTOL_DIR%\EXCEPTIONS" "%LICENSE_DIR%\Addictol-EXCEPTIONS.txt" >nul || exit /b 1
copy /Y "%CRASH_LOGGER_DIR%\LICENSE" "%LICENSE_DIR%\AddictolCrashLogger-LICENSE.txt" >nul || exit /b 1
copy /Y "%CRASH_LOGGER_DIR%\EXCEPTIONS" "%LICENSE_DIR%\AddictolCrashLogger-EXCEPTIONS.txt" >nul || exit /b 1
copy /Y "%SCRIPT_DIR%Licenses\Addictol.md" "%LICENSE_DIR%\Addictol.md" >nul || exit /b 1
exit /b 0

:ValidateReleasePayload
if not "%RELEASE_MODE%"=="1" exit /b 0

set "RELEASE_MISSING=0"
call :RequireReleaseFile "%PLUGINS_DIR%\CommonwealthOnline.dll"
call :RequireReleaseFile "%PLUGINS_DIR%\commonwealth_online_gns_bridge.dll"
call :RequireReleaseDir "%DATA_DIR%\PrismaUI_F4\views"
call :RequireReleaseFile "%DATA_DIR%\CommonwealthOnline.esp"
call :RequireReleaseFile "%DATA_DIR%\Scripts\COCharacterSetupQuestScript.pex"
call :RequireReleaseFile "%DATA_DIR%\Scripts\COQuestSyncScript.pex"
call :RequireReleaseFile "%DATA_DIR%\Scripts\CommonwealthOnlineDebug.pex"
call :RequireReleaseFile "%DATA_DIR%\Scripts\CommonwealthOnlineQuest.pex"
call :RequireReleaseFile "%DATA_DIR%\Scripts\Source\User\COCharacterSetupQuestScript.psc"
call :RequireReleaseFile "%DATA_DIR%\Scripts\Source\User\COQuestSyncScript.psc"
call :RequireReleaseFile "%DATA_DIR%\Scripts\Source\User\CommonwealthOnlineDebug.psc"
call :RequireReleaseFile "%DATA_DIR%\Scripts\Source\User\CommonwealthOnlineQuest.psc"
call :RequireReleaseDir "%DATA_DIR%\Materials"
call :RequireReleaseDir "%DATA_DIR%\Meshes"
call :RequireReleaseDir "%DATA_DIR%\Textures"

if "%RELEASE_MISSING%"=="1" (
    echo Strict release staging failed because required package content is missing.
    exit /b 1
)

echo Strict release payload validation passed for 1.11.240, 1.10.984, and 1.10.163.
exit /b 0

:RequireReleaseFile
if exist "%~1" exit /b 0
echo   MISSING FILE: %~1
set "RELEASE_MISSING=1"
exit /b 0

:RequireReleaseDir
if exist "%~1\" exit /b 0
echo   MISSING DIRECTORY: %~1
set "RELEASE_MISSING=1"
exit /b 0
