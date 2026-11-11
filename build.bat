@echo off
rem Usage: build.bat [msvc-debug|msvc-release]   (default msvc-debug)

setlocal
cd /d "%~dp0"
set PRESET=%1
if "%PRESET%"=="" set PRESET=msvc-debug

set VSROOT=C:\Program Files\Microsoft Visual Studio\18\Community
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if "%VCPKG_ROOT%"=="" set VCPKG_ROOT=%VSROOT%\VC\vcpkg

cmake --preset %PRESET% || exit /b 1
cmake --build --preset %PRESET% || exit /b 1
ctest --preset %PRESET% || exit /b 1
