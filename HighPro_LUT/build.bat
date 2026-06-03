@echo off
chcp 65001 >nul
setlocal EnableDelayedExpansion

rem ==== build mode ====
set "BUILD_TYPE=Release"
set "PRESET=msvc-release"
set "BUILDDIR=build\release"
if /i "%~1"=="debug" (
    set "BUILD_TYPE=Debug"
    set "PRESET=msvc-debug"
    set "BUILDDIR=build\debug"
)

rem ==== tools ====
set "VSDEVCMD=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
set "QT_BIN=C:\Qt\6.11.1\msvc2022_64\bin"
set "CMAKE_BIN=C:\Qt\Tools\CMake_64\bin"
set "NINJA_BIN=C:\Qt\Tools\Ninja"

if not exist "%VSDEVCMD%" (
    echo [ERROR] vcvars64.bat not found:
    echo   %VSDEVCMD%
    exit /b 1
)
if not exist "%QT_BIN%\qmake.exe" (
    echo [ERROR] Qt not found at %QT_BIN%
    exit /b 1
)

set "PATH=%CMAKE_BIN%;%NINJA_BIN%;%QT_BIN%;%PATH%"

echo === MSVC env ===
call "%VSDEVCMD%" >nul || (echo [ERROR] vcvars64 failed & exit /b 1)

cd /d "%~dp0"

echo.
echo === Configure (%PRESET%) ===
cmake --preset %PRESET%
if errorlevel 1 (
    echo [ERROR] configure failed
    exit /b 1
)

echo.
echo === Build (%BUILD_TYPE%) ===
cmake --build "%BUILDDIR%" --config %BUILD_TYPE%
if errorlevel 1 (
    echo [ERROR] build failed
    exit /b 1
)

echo.
echo === Deploy Qt dll (windeployqt) ===
rem windeployqt 不支持中文路径, 用临时英文目录中转
set "TMP_DIR=%TEMP%\HighPro_LUT_deploy"
if exist "%TMP_DIR%" rmdir /s /q "%TMP_DIR%"
mkdir "%TMP_DIR%"

copy /y "%~dp0HighPro_LUT.exe" "%TMP_DIR%\HighPro_LUT.exe" >nul

set "DEPLOY_FLAG=--release"
if /i "%BUILD_TYPE%"=="Debug" set "DEPLOY_FLAG=--debug"

"%QT_BIN%\windeployqt.exe" %DEPLOY_FLAG% --no-translations --no-system-d3d-compiler --no-opengl-sw "%TMP_DIR%\HighPro_LUT.exe" >nul
if errorlevel 1 (
    echo [WARN] windeployqt 失败, 但 EXE 已生成
) else (
    rem 拷回除 EXE 之外的所有 dll/插件
    for %%F in ("%TMP_DIR%\*.dll") do copy /y "%%F" "%~dp0" >nul
    for /d %%D in ("%TMP_DIR%\*") do (
        if not exist "%~dp0%%~nxD" mkdir "%~dp0%%~nxD"
        xcopy /y /e /q "%%D\*" "%~dp0%%~nxD\" >nul
    )
    rmdir /s /q "%TMP_DIR%"
    echo Qt dll 已部署到根目录
)

echo.
echo === OK ===
echo 双击运行: %~dp0HighPro_LUT.exe

endlocal
