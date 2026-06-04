@echo off
chcp 65001 >nul
setlocal EnableDelayedExpansion

set "VSDEVCMD=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
set "CMAKE_BIN=C:\Qt\Tools\CMake_64\bin"
set "NINJA_BIN=C:\Qt\Tools\Ninja"
set "QT_STATIC_BIN=C:\Qt\6.11.1\msvc2022_64_static\bin"

echo === MSVC env ===
call "%VSDEVCMD%" >nul || (echo [ERROR] vcvars64 failed & exit /b 1)

rem 确保 CMAKE/NINJA/QT 在 PATH 最前面, 排除 Strawberry 干扰
set "PATH=%CMAKE_BIN%;%NINJA_BIN%;%QT_STATIC_BIN%;%PATH%"
set "PATH=%PATH:C:\Strawberry\c\bin;=%"
set "PATH=%PATH:C:\Strawberry\perl\bin;=%"

cd /d "%~dp0"

echo.
echo === Configure (static) ===
cmake --preset msvc-release-static
if errorlevel 1 (
    echo [ERROR] configure failed
    exit /b 1
)

echo.
echo === Build (static release) ===
cmake --build build/release_static --config Release
if errorlevel 1 (
    echo [ERROR] build failed
    exit /b 1
)

echo.
echo === OK ===
echo 静态单 EXE: %~dp0build\release_static\LUT_Pro.exe
for %%I in ("build\release_static\LUT_Pro.exe") do echo 大小: %%~zI 字节
copy /y "build\release_static\LUT_Pro.exe" "%~dp0LUT_Pro.exe" >nul
echo 已复制到: %~dp0LUT_Pro.exe

endlocal
