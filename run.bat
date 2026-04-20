@echo off
setlocal

:: Add MSYS2 runtime DLLs to PATH so the exe can find them
set "MSYS2_BIN=C:\msys64\ucrt64\bin"
if exist "%MSYS2_BIN%\libstdc++-6.dll" (
    set "PATH=%MSYS2_BIN%;%PATH%"
) else (
    echo [WARN] MSYS2 ucrt64 bin not found at %MSYS2_BIN%, trying to run anyway...
)

if not exist "%~dp0build\dbms.exe" (
    echo [FAIL] build\dbms.exe not found. Run build.bat first.
    pause & exit /b 1
)

"%~dp0build\dbms.exe"
endlocal
