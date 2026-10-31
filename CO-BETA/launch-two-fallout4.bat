@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "SCRIPT_DIR=%~dp0"
set "F4SE_DIR=D:\SteamLibrary\steamapps\common\Fallout 4"
set "F4SE_EXE=%F4SE_DIR%\f4se_loader.exe"
set "SANDBOXIE_DIR=C:\Program Files\Sandboxie-Plus"
set "SANDBOXIE_START=%SANDBOXIE_DIR%\Start.exe"
set "SANDBOXIE_INI=%SANDBOXIE_DIR%\SbieIni.exe"
set "SANDBOX_NAME=Fallout4Client2"
set "SANDBOX_CONFIG=%SCRIPT_DIR%.sandbox-file-root"
set "START_DELAY_SECONDS=8"

if not exist "%F4SE_EXE%" (
    echo Could not find f4se_loader.exe at:
    echo "%F4SE_EXE%"
    echo.
    echo Update F4SE_DIR in this script if Fallout 4 was moved.
    pause
    exit /b 1
)

if not exist "%SANDBOXIE_START%" (
    echo Fallout 4 allows only one normal instance at a time.
    echo Install Sandboxie-Plus, then run this script again.
    echo.
    echo Expected Sandboxie launcher:
    echo "%SANDBOXIE_START%"
    pause
    exit /b 1
)

call :ResolveSandboxRoot
if errorlevel 1 (
    echo Failed to resolve sandbox file location.
    pause
    exit /b 1
)

call :EnsureSandbox
if errorlevel 1 (
    echo Failed to create or configure sandbox "%SANDBOX_NAME%".
    pause
    exit /b 1
)

echo Starting Fallout 4 instance 1 ^(unsandboxed^)...
start "Fallout 4 Instance 1" /D "%F4SE_DIR%" "%F4SE_EXE%"

echo Waiting %START_DELAY_SECONDS% seconds before starting instance 2...
timeout /t %START_DELAY_SECONDS% /nobreak >nul

echo Starting Fallout 4 instance 2 in Sandboxie box "%SANDBOX_NAME%"...
echo Sandbox files: "%SANDBOX_FILE_ROOT%"
pushd "%F4SE_DIR%"
"%SANDBOXIE_START%" /box:%SANDBOX_NAME% "%F4SE_EXE%"
set "LAUNCH_ERROR=%ERRORLEVEL%"
popd

if not "%LAUNCH_ERROR%"=="0" (
    echo Sandboxie returned error code %LAUNCH_ERROR%.
    pause
    exit /b %LAUNCH_ERROR%
)

echo Done.
echo.
echo Instance 1 log ^(unsandboxed^):
echo   "%USERPROFILE%\Documents\My Games\Fallout4\F4SE\CommonwealthOnline.log"
echo Instance 2 log ^(Sandboxie^):
echo   "%SANDBOX_FILE_ROOT%\user\current\Documents\My Games\Fallout4\F4SE\CommonwealthOnline.log"
echo Look for "Assigned server playerId: 2" in the instance 2 log to confirm that client loaded the plugin.
endlocal
exit /b 0

:ResolveSandboxRoot
if exist "%SANDBOX_CONFIG%" (
    set /p "SAVED=" < "%SANDBOX_CONFIG%"
    call :StripQuotes SAVED
    if defined SAVED (
        set "SANDBOX_FILE_ROOT=!SAVED!"
        echo Using saved sandbox location: !SANDBOX_FILE_ROOT!
        goto :EnsureSandboxDirs
    )
)

echo.
echo First run: choose where Sandboxie should store the second Fallout 4 instance.
echo Enter a drive letter ^(e.g. D:\^) or a full folder path.
echo A drive-only answer creates: ^<drive^>\sandboxes\%SANDBOX_NAME%
echo Example: D:\  -^>  D:\sandboxes\%SANDBOX_NAME%
echo.

:PromptSandboxRoot
set "SANDBOX_INPUT="
set /p "SANDBOX_INPUT=Sandbox location: "
if not defined SANDBOX_INPUT (
    echo No path entered.
    goto :PromptSandboxRoot
)

call :StripQuotes SANDBOX_INPUT
set "SANDBOX_INPUT=!SANDBOX_INPUT:/=\!"

if "!SANDBOX_INPUT:~1,1!"==":" (
    set "DRIVE_REST=!SANDBOX_INPUT:~2!"
    if "!DRIVE_REST!"=="" goto :DriveOnlyRoot
    if "!DRIVE_REST!"=="\" goto :DriveOnlyRoot
)

if "!SANDBOX_INPUT:~-1!"=="\" set "SANDBOX_INPUT=!SANDBOX_INPUT:~0,-1!"
if not defined SANDBOX_INPUT (
    echo Invalid path.
    goto :PromptSandboxRoot
)
set "SANDBOX_FILE_ROOT=!SANDBOX_INPUT!"
goto :SaveSandboxRoot

:DriveOnlyRoot
set "SANDBOX_FILE_ROOT=!SANDBOX_INPUT:~0,1!:\sandboxes\%SANDBOX_NAME%"

:SaveSandboxRoot
> "%SANDBOX_CONFIG%" echo !SANDBOX_FILE_ROOT!
echo Saved sandbox location to:
echo   "%SANDBOX_CONFIG%"
echo Sandbox files will be stored at:
echo   !SANDBOX_FILE_ROOT!

:EnsureSandboxDirs
if not exist "!SANDBOX_FILE_ROOT!" (
    mkdir "!SANDBOX_FILE_ROOT!" >nul 2>&1
    if not exist "!SANDBOX_FILE_ROOT!" (
        echo Could not create sandbox folder:
        echo   !SANDBOX_FILE_ROOT!
        echo Check the drive letter and permissions, then try again.
        exit /b 1
    )
)
exit /b 0

:EnsureSandbox
"%SANDBOXIE_INI%" query %SANDBOX_NAME% Enabled 2>nul | findstr /i "y" >nul
if not errorlevel 1 goto :ConfigureSandboxRoot

echo Creating Sandboxie box "%SANDBOX_NAME%"...
"%SANDBOXIE_INI%" set %SANDBOX_NAME% Enabled y
if errorlevel 1 exit /b 1
"%SANDBOXIE_INI%" set %SANDBOX_NAME% ConfigLevel 7
if errorlevel 1 exit /b 1

:ConfigureSandboxRoot
"%SANDBOXIE_INI%" delete %SANDBOX_NAME% OpenFilePath "%F4SE_DIR%\*" >nul 2>&1

echo Ensuring sandbox "%SANDBOX_NAME%" stores files at:
echo   "%SANDBOX_FILE_ROOT%"
"%SANDBOXIE_INI%" set %SANDBOX_NAME% FileRootPath "%SANDBOX_FILE_ROOT%"
if errorlevel 1 exit /b 1
"%SANDBOXIE_INI%" set %SANDBOX_NAME% UsePrivacyMode n
if errorlevel 1 exit /b 1

:ReloadSandboxConfig
"%SANDBOXIE_START%" /reload >nul
exit /b 0

:StripQuotes
set "TMP=!%~1!"
set "TMP=!TMP:"=!"
set "%~1=!TMP!"
exit /b 0
