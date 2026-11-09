@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "SCRIPT_DIR=%~dp0"
set "CONFIG=%SCRIPT_DIR%.fallout4-path"
set "INPUT=%~1"
set "F4_DIR="
set "DATA_DIR="
set "EXPLICIT=0"
if defined INPUT set "EXPLICIT=1"

if defined INPUT call :NormalizePath "!INPUT!"
if defined DATA_DIR if exist "!DATA_DIR!" goto :SaveAndDone

if "!EXPLICIT!"=="1" (
    echo.
    echo Path not found or invalid: !INPUT!
    echo Expected the Fallout 4 folder or its Data subfolder.
    set "F4_DIR="
    set "DATA_DIR="
    goto :PromptLoop
)

if exist "%CONFIG%" (
    set /p "SAVED=" < "%CONFIG%"
    call :StripQuotes SAVED
    if defined SAVED call :NormalizePath "!SAVED!"
    if defined DATA_DIR if exist "!DATA_DIR!" (
        echo.
        echo Saved Fallout 4 path: !F4_DIR!
        set /p "USE_SAVED=Use this path? [Y/n]: "
        if /i "!USE_SAVED!"=="" set "USE_SAVED=Y"
        if /i "!USE_SAVED!"=="Y" goto :SaveAndDone
        set "F4_DIR="
        set "DATA_DIR="
    )
)

for %%P in (
    "D:\SteamLibrary\steamapps\common\Fallout 4"
    "%ProgramFiles(x86)%\Steam\steamapps\common\Fallout 4"
    "C:\Program Files (x86)\Steam\steamapps\common\Fallout 4"
    "%ProgramFiles%\Steam\steamapps\common\Fallout 4"
) do (
    if exist "%%~P\Data" (
        echo.
        echo Found Fallout 4 at: %%~P
        set /p "USE_FOUND=Use this path? [Y/n]: "
        if /i "!USE_FOUND!"=="" set "USE_FOUND=Y"
        if /i "!USE_FOUND!"=="Y" (
            set "F4_DIR=%%~P"
            set "DATA_DIR=%%~P\Data"
            goto :SaveAndDone
        )
    )
)

:PromptLoop
echo.
echo Enter your Fallout 4 installation directory.
echo Example: D:\SteamLibrary\steamapps\common\Fallout 4
set /p "F4_DIR="
if not defined F4_DIR (
    echo No path entered.
    exit /b 1
)
call :StripQuotes F4_DIR
if "!F4_DIR:~-1!"=="\" set "F4_DIR=!F4_DIR:~0,-1!"
set "DATA_DIR=!F4_DIR!\Data"
if not exist "!DATA_DIR!" (
    echo Data folder not found:
    echo   !DATA_DIR!
    echo Check the path and try again.
    goto :PromptLoop
)

:SaveAndDone
> "%CONFIG%" echo !F4_DIR!
echo Using Fallout 4 Data: !DATA_DIR!
set "XF4=!F4_DIR!"
set "XDATA=!DATA_DIR!"
endlocal & set "F4_DIR=%XF4%" & set "DATA_DIR=%XDATA%"
exit /b 0

:NormalizePath
set "RAW=%~1"
call :StripQuotes RAW
if "!RAW:~-1!"=="\" set "RAW=!RAW:~0,-1!"

if exist "!RAW!\F4SE" (
    set "DATA_DIR=!RAW!"
    for %%D in ("!RAW!\..") do set "F4_DIR=%%~fD"
    exit /b 0
)
if exist "!RAW!\Data" (
    set "F4_DIR=!RAW!"
    set "DATA_DIR=!RAW!\Data"
    exit /b 0
)
if /i "!RAW:~-5!"=="\Data" (
    if exist "!RAW!" (
        set "DATA_DIR=!RAW!"
        for %%D in ("!RAW!\..") do set "F4_DIR=%%~fD"
    )
)
exit /b 0

:StripQuotes
set "TMP=!%~1!"
set "TMP=!TMP:"=!"
set "%~1=!TMP!"
exit /b 0
