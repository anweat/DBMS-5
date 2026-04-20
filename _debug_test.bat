@echo off
setlocal EnableDelayedExpansion
set "MSYS2_ROOT=C:\msys64"
set "MINGW_BIN=!MSYS2_ROOT!\ucrt64\bin"
echo MINGW_BIN=[!MINGW_BIN!]
if exist "!MINGW_BIN!\g++.exe" (echo g++.exe FOUND) else (echo g++.exe NOT FOUND)
if exist "!MINGW_BIN!\ninja.exe" (echo ninja FOUND in msys2) else (echo ninja NOT in msys2)
if exist "!MINGW_BIN!\mingw32-make.exe" (echo make FOUND in msys2) else (echo make NOT in msys2)
where ninja >nul 2>&1
echo where-ninja errorlevel=%errorlevel%
if %errorlevel%==0 (
    for /f "delims=" %%p in ('where ninja') do set "MAKE_EXE=%%p"
    set "CMAKE_GENERATOR=Ninja"
    echo Build tool: Ninja [!MAKE_EXE!]
)
echo CMAKE_GENERATOR=[!CMAKE_GENERATOR!]
endlocal
