@echo off
setlocal EnableExtensions DisableDelayedExpansion
set "CO_DEBUG_VALUE=%COMMONWEALTH_ONLINE_DEBUG%"
if not defined CO_DEBUG_VALUE set "CO_DEBUG_VALUE=on"
for /f "tokens=*" %%A in ("%CO_DEBUG_VALUE%") do set "CO_DEBUG_VALUE=%%A"
:trim_debug_value
if not defined CO_DEBUG_VALUE goto debug_value_ready
if not "%CO_DEBUG_VALUE:~-1%"==" " goto debug_value_ready
set "CO_DEBUG_VALUE=%CO_DEBUG_VALUE:~0,-1%"
goto trim_debug_value
:debug_value_ready
if not defined CO_DEBUG_VALUE set "CO_DEBUG_VALUE=on"
if /i "%CO_DEBUG_VALUE%"=="0" exit /b 0
if /i "%CO_DEBUG_VALUE%"=="false" exit /b 0
if /i "%CO_DEBUG_VALUE%"=="no" exit /b 0
if /i "%CO_DEBUG_VALUE%"=="off" exit /b 0
if /i "%CO_DEBUG_VALUE%"=="release" exit /b 0
if /i "%CO_DEBUG_VALUE%"=="disabled" exit /b 0
set "CO_DEBUG_COMPONENT=%~1"
set "CO_DEBUG_MESSAGE=%~2"
if not defined CO_DEBUG_COMPONENT set "CO_DEBUG_COMPONENT=Script"
setlocal EnableDelayedExpansion
echo([CO DEBUG][!CO_DEBUG_COMPONENT!] !CO_DEBUG_MESSAGE!
endlocal
endlocal
