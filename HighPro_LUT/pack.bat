@echo off
chcp 65001 >nul
setlocal EnableDelayedExpansion

rem ============================================================
rem  HighPro_LUT M10 打包脚本 (7z SFX 单 EXE)
rem
rem  流程:
rem    1. 先跑 build.bat release 确保 EXE 最新
rem    2. 把要发布的文件收到 dist\
rem    3. 7z a dist.7z dist\*  打包
rem    4. copy /b 7zsd.sfx + config.txt + dist.7z = HighPro_LUT_setup.exe
rem ============================================================

set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"

set "DIST=%ROOT%\dist"
set "TOOLS=%ROOT%\tools"
set "OUT=%ROOT%\HighPro_LUT_setup.exe"

set "SEVENZ=C:\Program Files\7-Zip\7z.exe"
if not exist "%SEVENZ%" (
    echo [ERROR] 7-Zip not found at %SEVENZ%
    echo 请先安装 7-Zip
    exit /b 1
)
if not exist "%TOOLS%\7zsd.sfx" (
    echo [ERROR] %TOOLS%\7zsd.sfx 缺失
    exit /b 1
)
if not exist "%TOOLS%\sfx_config.txt" (
    echo [ERROR] %TOOLS%\sfx_config.txt 缺失
    exit /b 1
)
if not exist "%ROOT%\HighPro_LUT.exe" (
    echo [ERROR] HighPro_LUT.exe 不存在, 请先跑 build.bat release
    exit /b 1
)

rem ==== 1) 准备 dist\ ====
echo === 清理 dist\ ===
if exist "%DIST%" rmdir /s /q "%DIST%"
mkdir "%DIST%"

echo === 复制运行时文件到 dist\ ===
copy /y "%ROOT%\HighPro_LUT.exe" "%DIST%\" >nul
for %%F in ("%ROOT%\*.dll") do copy /y "%%F" "%DIST%\" >nul

rem Qt 插件目录
for %%D in (platforms imageformats iconengines styles tls networkinformation generic) do (
    if exist "%ROOT%\%%D" (
        if not exist "%DIST%\%%D" mkdir "%DIST%\%%D"
        xcopy /y /e /q "%ROOT%\%%D\*" "%DIST%\%%D\" >nul
    )
)

rem ==== 2) 打包 7z ====
set "TMP_7Z=%TOOLS%\dist.7z"
if exist "%TMP_7Z%" del /q "%TMP_7Z%"

echo.
echo === 7z 最高压缩 (LZMA2) ===
"%SEVENZ%" a -t7z -mx=9 -mfb=64 -ms=on -mmt=on "%TMP_7Z%" "%DIST%\*" >nul
if errorlevel 1 (
    echo [ERROR] 7z 压缩失败
    exit /b 1
)

rem ==== 3) 拼 SFX + config + 7z = setup.exe ====
echo.
echo === 拼接 SFX 头 ===
if exist "%OUT%" del /q "%OUT%"
copy /b "%TOOLS%\7zsd.sfx" + "%TOOLS%\sfx_config.txt" + "%TMP_7Z%" "%OUT%" >nul
if errorlevel 1 (
    echo [ERROR] 拼接失败
    exit /b 1
)

rem 清理中间文件
del /q "%TMP_7Z%"

for %%I in ("%OUT%") do (
    set /a SIZE_MB=%%~zI/1048576
    echo.
    echo === OK ===
    echo 生成: %OUT%
    echo 大小: %%~zI 字节 ^(约 !SIZE_MB! MB^)
    echo.
    echo 双击单 EXE 即可运行, 程序会自动解压到 %%TEMP%% 后启动.
)

endlocal
