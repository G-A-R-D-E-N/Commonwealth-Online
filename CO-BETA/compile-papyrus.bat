@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "SCRIPT_DIR=%~dp0"
call "%SCRIPT_DIR%tools\debug-log.bat" Papyrus "Papyrus compile requested"
set "SCRIPTS_SOURCE_DIR=%SCRIPT_DIR%creation-kit\scripts\source"
set "SCRIPTS_COMPILED_DIR=%SCRIPT_DIR%creation-kit\scripts\compiled"
set "CONFIG=%SCRIPT_DIR%.fallout4-path"
set "RESOLVE_INPUT=%~1"
if not defined RESOLVE_INPUT if defined FALLOUT4_PATH set "RESOLVE_INPUT=%FALLOUT4_PATH%"
if not defined RESOLVE_INPUT if exist "%CONFIG%" set /p "RESOLVE_INPUT=" < "%CONFIG%"

call "%SCRIPT_DIR%resolve-fallout4-path.bat" "%RESOLVE_INPUT%"
if errorlevel 1 exit /b 1

set "PAPYRUS_COMPILER=%PAPYRUS_COMPILER_EXE%"
if not defined PAPYRUS_COMPILER set "PAPYRUS_COMPILER=%F4_DIR%\Papyrus Compiler\PapyrusCompiler.exe"

set "PAPYRUS_FLAGS=%DATA_DIR%\Scripts\Source\Base\Institute_Papyrus_Flags.flg"
set "PAPYRUS_IMPORTS=%SCRIPTS_SOURCE_DIR%;%DATA_DIR%\Scripts\Source\User;%DATA_DIR%\Scripts\Source;%DATA_DIR%\Scripts\Source\Base"

if not exist "%PAPYRUS_COMPILER%" (
    echo Papyrus compiler not found:
    echo   %PAPYRUS_COMPILER%
    echo Install the Creation Kit or set PAPYRUS_COMPILER_EXE.
    exit /b 1
)

if not exist "%PAPYRUS_FLAGS%" (
    echo Papyrus flags file not found:
    echo   %PAPYRUS_FLAGS%
    exit /b 1
)

if not exist "%SCRIPTS_SOURCE_DIR%" (
    echo Papyrus source folder not found:
    echo   %SCRIPTS_SOURCE_DIR%
    exit /b 1
)

if not exist "%SCRIPTS_COMPILED_DIR%" mkdir "%SCRIPTS_COMPILED_DIR%"

for %%F in ("%SCRIPTS_COMPILED_DIR%\CoSync*.pex") do (
    if exist "%%~fF" del /q "%%~fF"
)

call "%SCRIPT_DIR%tools\debug-log.bat" Papyrus "Compiler=%PAPYRUS_COMPILER% Output=%SCRIPTS_COMPILED_DIR%"
echo Compiling Papyrus scripts...
echo   Source: %SCRIPTS_SOURCE_DIR%
echo   Output: %SCRIPTS_COMPILED_DIR%
echo   Compiler: %PAPYRUS_COMPILER%
echo.

set "SCRIPT_COUNT=0"

for /r "%SCRIPTS_SOURCE_DIR%" %%F in (*.psc) do (
    set "REL=%%~fF"
    set "REL=!REL:%SCRIPTS_SOURCE_DIR%\=!"
    set "SCRIPT_NAME=!REL:~0,-4!"
    set "SCRIPT_NAME=!SCRIPT_NAME:\=:!"
    set "SCRIPT_OUTPUT=!SCRIPT_NAME::=\!"
    set "EXPECTED_PEX=%SCRIPTS_COMPILED_DIR%\!SCRIPT_OUTPUT!.pex"
    set /a SCRIPT_COUNT+=1

    echo   !SCRIPT_NAME!
    if exist "!EXPECTED_PEX!" del /q "!EXPECTED_PEX!"
    "%PAPYRUS_COMPILER%" "!SCRIPT_NAME!" -f="%PAPYRUS_FLAGS%" -i="%PAPYRUS_IMPORTS%" -o="%SCRIPTS_COMPILED_DIR%"
    if not exist "!EXPECTED_PEX!" (
        echo Papyrus compilation failed for !SCRIPT_NAME!.
        exit /b 1
    )
)

if "%SCRIPT_COUNT%"=="0" (
    echo No active Papyrus sources found.
) else (
    echo.
    echo Papyrus compilation complete. Scripts compiled: %SCRIPT_COUNT%
    call "%SCRIPT_DIR%tools\debug-log.bat" Papyrus "Compilation complete scripts=%SCRIPT_COUNT%"
)

endlocal
exit /b 0
