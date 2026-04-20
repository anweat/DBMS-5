@echo off
setlocal

:: ---- Check g++ ----
where g++ >nul 2>&1
if errorlevel 1 (
    echo [FAIL] g++ not found.
    echo        Install MSYS2: https://www.msys2.org/
    echo        Then run in MSYS2 UCRT64: pacman -S mingw-w64-ucrt-x86_64-gcc
    pause & exit /b 1
)
for /f "delims=" %%v in ('g++ --version 2^>^&1 ^| findstr /r "g++"') do echo [OK] %%v

:: ---- Check C++17 ----
echo #include^<optional^> > "%TEMP%\dbms_test17.cpp"
echo int main(){} >> "%TEMP%\dbms_test17.cpp"
g++ -std=c++17 "%TEMP%\dbms_test17.cpp" -o "%TEMP%\dbms_test17.exe" >nul 2>&1
if errorlevel 1 (
    echo [FAIL] g++ does not support C++17. Please upgrade to GCC 8+.
    del "%TEMP%\dbms_test17.cpp" >nul 2>&1
    pause & exit /b 1
)
echo [OK] C++17 supported
del "%TEMP%\dbms_test17.cpp" "%TEMP%\dbms_test17.exe" >nul 2>&1

:: ---- Check cmake ----
where cmake >nul 2>&1
if errorlevel 1 (
    echo [FAIL] cmake not found. Install: https://cmake.org/download/
    pause & exit /b 1
)
for /f "tokens=3" %%v in ('cmake --version 2^>nul ^| findstr /r "cmake version"') do echo [OK] cmake %%v

:: ---- Clean stale cache if generator changed ----
if exist build\CMakeCache.txt (
    findstr /i "CMAKE_GENERATOR" build\CMakeCache.txt | findstr /i "MinGW" >nul 2>&1
    if errorlevel 1 del build\CMakeCache.txt >nul 2>&1
)

:: ---- Build ----
echo.
cmake -B build -G "MinGW Makefiles" -DCMAKE_CXX_COMPILER=g++ -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 ( echo [FAIL] cmake configure failed & pause & exit /b 1 )

cmake --build build
if errorlevel 1 ( echo [FAIL] build failed & pause & exit /b 1 )

echo.
echo [OK] Build successful.  Run: build\dbms.exe
pause
endlocal
