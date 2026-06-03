# HighPro_LUT 静态单 EXE 打包完整记录

> 本文档记录 2026/06/04 完成的从动态 Qt 到静态 Qt 单 EXE 打包全过程,含环境检测、踩坑、解决方案、最终产物。

---

## 0. 目标

| 项 | 值 |
|---|---|
| 工程 | `HighPro_LUT`(CMake) |
| 编译器 | MSVC v143(VS 2022 Community 18 / `14.51.36231`) |
| Qt 版本 | 6.11.1 |
| 链接方式 | **静态 Qt + 静态 MSVC 运行时(/MT)** |
| 输出 | 单文件 `LUT_Pro.exe` 24.1 MB,无外部 dll 依赖 |

---

## 1. 环境检测结果

| 项 | 状态 | 路径 / 版本 |
|---|---|---|
| VS 2022 Community | OK | `C:\Program Files\Microsoft Visual Studio\18\Community`,MSVC `14.51.36231` |
| Windows 10 SDK | OK | `10.0.26100.0` |
| CMake | OK | `C:\Qt\Tools\CMake_64\bin\cmake.exe` v3.30.5 |
| Ninja | OK | `C:\Qt\Tools\Ninja\ninja.exe` v1.12.1 |
| Python | OK | 3.12.3(转 ico 用) |
| Git | OK | 2.54.0 |
| Strawberry Perl | 后装 | `C:\Strawberry\perl\bin\perl.exe` v5.42.2(Qt 静态编译必备) |
| Qt 动态 6.11.1 | 已有 | `C:\Qt\6.11.1\msvc2022_64`(保留原状) |
| Qt 6.11.1 源码 | 后下 | `Y:\qt-everywhere-src-6.11.1` |
| Qt 静态 6.11.1 | 编译产 | `C:\Qt\6.11.1\msvc2022_64_static`(新增) |

---

## 2. 下载安装

### 2.1 Strawberry Perl

下载页:https://strawberryperl.com/

选 `5.42.2.1 MSI (200 MB)`,默认安装,自动入 PATH。验证:

```bat
perl -v
```

### 2.2 Qt 6.11.1 源码

下载页:https://download.qt.io/official_releases/qt/6.11/6.11.1/single/

选 `qt-everywhere-src-6.11.1.zip`(约 1.5 GB)。解压到**无中文、无空格**路径:

```text
Y:\qt-everywhere-src-6.11.1
```

> ⚠️ 不要放中文路径(例如 `Y:\迅雷下载\...`),Qt 的 configure 脚本对中文路径有诡异问题。

---

## 3. 静态 Qt 编译

### 3.1 关键发现:VS 2026 (v18) 路径与传统不同

| 旧版 | VS 2022 v17 | VS Community 18 (本机) |
|---|---|---|
| 安装根 | `C:\Program Files\Microsoft Visual Studio\2022\Community` | `C:\Program Files\Microsoft Visual Studio\18\Community` |
| `vswhere.exe` | 自身 PATH 自动可见 | 在 `C:\Program Files (x86)\Microsoft Visual Studio\Installer\`,但 `vcvars64.bat` 假设它在 PATH |

**解决:** 调用 `vcvars64.bat` 前,先把 `vswhere` 目录加到 PATH。

### 3.2 编译脚本 `_run_configure.bat`

放在 `Y:\qt-everywhere-src-6.11.1\_run_configure.bat`,内容:

```bat
@echo off
set "PATH=C:\Program Files (x86)\Microsoft Visual Studio\Installer;%PATH%"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 (
    echo VCVARS_FAIL
    exit /b 1
)
set "PATH=C:\Strawberry\perl\bin;C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;%PATH%"
cd /d Y:\qt-everywhere-src-6.11.1
call "Y:\qt-everywhere-src-6.11.1\configure.bat" ^
  -prefix C:\Qt\6.11.1\msvc2022_64_static ^
  -static ^
  -static-runtime ^
  -release ^
  -opensource ^
  -confirm-license ^
  -nomake examples ^
  -nomake tests ^
  -submodules qtbase,qtsvg,qtimageformats
echo CONFIGURE_EXIT=%errorlevel%
```

**关键点:**

1. **`-submodules` 白名单**:本项目只用 `qtbase + qtsvg + qtimageformats`。比 `-skip` 黑名单稳得多 — Qt 6.11.1 有 43 个子模块,逐个 skip 还会撞到依赖错误(如 `qtcanvaspainter → qtdeclarative`、`qtquick3dphysics → qtdeclarative`)。
2. **`call "全路径\configure.bat"`**:不能写 `call configure.bat`,因为 `vcvars64` 不让 cwd 下的 .bat 通过 cmd path 解析找到。
3. **`-static -static-runtime`**:静态 Qt + /MT 静态 C 运行时,EXE 无任何外部依赖。

### 3.3 编译脚本 `_run_build.bat`

```bat
@echo off
set "PATH=C:\Program Files (x86)\Microsoft Visual Studio\Installer;%PATH%"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
set "PATH=C:\Strawberry\perl\bin;C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;%PATH%"
cd /d Y:\qt-everywhere-src-6.11.1
cmake --build . --parallel
echo BUILD_EXIT=%errorlevel%
```

### 3.4 安装脚本 `_run_install.bat`

```bat
@echo off
set "PATH=C:\Program Files (x86)\Microsoft Visual Studio\Installer;%PATH%"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
set "PATH=C:\Strawberry\perl\bin;C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;%PATH%"
cd /d Y:\qt-everywhere-src-6.11.1
cmake --install .
echo INSTALL_EXIT=%errorlevel%
```

### 3.5 全流程耗时

| 阶段 | 耗时 |
|---|---|
| configure | 约 1 分钟(2155 个 ninja 目标) |
| build | 约 3 分钟(全核 parallel) |
| install | 约 30 秒 |

> 本机 CPU 强 + 只编 3 个子模块,所以快。一般机器整体预算 20-60 分钟。

### 3.6 结果验证

```
C:\Qt\6.11.1\msvc2022_64_static\
  bin\qmake.exe                          9 MB
  lib\Qt6Core.lib                       37 MB  ← 静态库体积正常
  lib\Qt6Widgets.lib                    37 MB
  lib\Qt6Svg.lib                       2.7 MB
  plugins\platforms\qwindows.lib       10 MB  ← 关键!平台插件
  plugins\styles\qmodernwindowsstyle.lib
  plugins\imageformats\qgif.lib, qjpeg.lib, qsvg.lib, ...
```

CMake cache 关键字段(`CMakeCache.txt`):

```
BUILD_SHARED_LIBS:BOOL=OFF
FEATURE_static_runtime:BOOL=ON
CMAKE_BUILD_TYPE:STRING=Release
CMAKE_INSTALL_PREFIX:PATH=C:/Qt/6.11.1/msvc2022_64_static
```

---

## 4. 本项目静态构建

### 4.1 `CMakeLists.txt` 关键改动

```cmake
# /MT 静态 C 运行时(HIGHPRO_STATIC_RUNTIME 开关)
if(MSVC AND HIGHPRO_STATIC_RUNTIME)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Windows EXE 图标
set(APP_WIN_RC "")
if(WIN32)
    set(APP_WIN_RC "${CMAKE_SOURCE_DIR}/app.rc")
endif()

qt_add_executable(HighPro_LUT WIN32
    ${APP_SOURCES}
    ${APP_RESOURCES}
    ${APP_WIN_RC}
)

# 静态版叫 LUT_Pro.exe,动态版叫 HighPro_LUT.exe
if(HIGHPRO_STATIC_RUNTIME)
    set_target_properties(HighPro_LUT PROPERTIES OUTPUT_NAME "LUT_Pro")
else()
    set_target_properties(HighPro_LUT PROPERTIES OUTPUT_NAME "HighPro_LUT")
endif()
```

### 4.2 本项目构建脚本 `C:\Temp\_build_highpro_static.bat`

> ⚠️ 必须放**非中文路径**(本项目根 `x:\Lut变色\` 含中文,bat 编码无法稳定处理)。项目路径通过环境变量 `HIGHPRO_DIR` 注入。

```bat
@echo off
set "PATH=C:\Program Files (x86)\Microsoft Visual Studio\Installer;%PATH%"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 (
    echo VCVARS_FAIL
    exit /b 1
)
set "PATH=C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;%PATH%"
cd /d "%HIGHPRO_DIR%"
cmake -S . -B build-static -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/msvc2022_64_static ^
  -DHIGHPRO_STATIC_RUNTIME=ON
if errorlevel 1 exit /b 1
cmake --build build-static --parallel
echo BUILD_EXIT=%errorlevel%
```

调用方式:

```bat
set "HIGHPRO_DIR=x:\Lut变色\HighPro_LUT" && call C:\Temp\_build_highpro_static.bat
```

### 4.3 构建耗时

| 阶段 | 耗时 |
|---|---|
| 首次 configure | ~5 秒 |
| 首次 build(31 个 cpp) | ~70 秒 |
| 增量(改 1 个 cpp) | ~10 秒 |
| 仅 link(改 .rc/图标) | ~3 秒 |

---

## 5. 添加 EXE 图标

### 5.1 PNG → ICO 转换

源图 `LutP2.png` 1024×1024 RGBA。Python + PIL 一行搞定:

```python
from PIL import Image
im = Image.open(r'x:\Lut变色\HighPro_LUT\LutP2.png').convert('RGBA')
im.save(
    r'x:\Lut变色\HighPro_LUT\assets\icons\app.ico',
    format='ICO',
    sizes=[(16,16),(24,24),(32,32),(48,48),(64,64),(128,128),(256,256)]
)
```

生成 `assets\icons\app.ico` 93 KB,含 7 个分辨率。

### 5.2 EXE 文件图标(Explorer / 任务栏文件预览)

新建 `app.rc`(项目根):

```rc
IDI_APP_ICON ICON "assets/icons/app.ico"
```

由 `CMakeLists.txt` 已加进 `qt_add_executable` 的源文件列表,链接时 MSVC `rc.exe` 自动嵌入 `.rsrc` 段。

### 5.3 窗口 / Alt+Tab / 运行时任务栏图标

`assets\assets.qrc` 加一行:

```xml
<file alias="icons/app.ico">icons/app.ico</file>
```

`src/main.cpp`:

```cpp
#include <QIcon>
// ...
QApplication::setStyle(QStyleFactory::create("Fusion"));
QApplication::setWindowIcon(QIcon(":/icons/app.ico"));
```

---

## 6. 验证

### 6.1 隔离 dll 测试(确认真静态)

把项目根 5 个 Qt6*.dll + platforms/styles/imageformats/iconengines/tls/networkinformation/generic 子目录里的 dll 全挪到 `_bak_dll\`,然后双击 `LUT_Pro.exe`:

- 进程内存稳定在 170-190 MB(正常)
- 无 WerFault 崩溃
- 无 Not Responding 卡死
- 主界面正常显示

✅ **完全独立于动态 Qt 运行。**

### 6.2 图标三处确认

| 位置 | 来源 | 状态 |
|---|---|---|
| 桌面 / Explorer 文件图标 | `.rsrc` (app.rc) | ✓ |
| 窗口左上角 | `setWindowIcon` | ✓ |
| 任务栏运行时 | `setWindowIcon` | ✓ |
| Alt+Tab 预览 | `setWindowIcon` | ✓ |

---

## 7. 最终产物

| 文件 | 大小 | 说明 |
|---|---|---|
| `LUT_Pro.exe` | 24.1 MB | 静态版本,单文件,可拷到任何 Win10/11 x64 直接跑 |
| `HighPro_LUT.exe` | 76 KB | 原动态版备份,需配合同目录 Qt6*.dll |
| `build\release\bin\` | - | 原完整动态版打包目录(备份) |

---

## 8. 踩坑回顾

| 问题 | 现象 | 解决 |
|---|---|---|
| `'configure.bat' 不是命令` | call 找不到 cwd 下的 .bat | 用全路径 `call "Y:\...\configure.bat"` |
| `vswhere.exe 不是命令` | VS18 vswhere 在老路径 | 先把 `C:\Program Files (x86)\Microsoft Visual Studio\Installer` 加进 PATH |
| `Module 'qtcanvaspainter' depends on 'qtdeclarative'` | `-skip` 黑名单遗漏新模块 | 改用 `-submodules qtbase,qtsvg,qtimageformats` 白名单 |
| 中文路径 `Y:\迅雷下载\...` 风险 | configure 可能挂 | 挪到 `Y:\qt-everywhere-src-6.11.1` |
| bat 中文路径乱码 | UTF-8 写的 bat 在 cmd 里变 `Lut鍙樿壊` | bat 放无中文目录 + 路径通过环境变量传入 |
| link 失败 LNK1104 | 旧 EXE 进程在跑占文件 | `taskkill /F /IM xxx.exe` 后再 link |

---

## 9. 下次怎么用

### 改代码 → 重出 EXE

```bat
taskkill /F /IM LUT_Pro.exe 2>nul
set "HIGHPRO_DIR=x:\Lut变色\HighPro_LUT" && call C:\Temp\_build_highpro_static.bat
```

约 10 秒(增量)。

### 改图标

替换 `assets/icons/app.ico`(或重跑 PIL 脚本),然后跑上一条命令即可。

### 升级 Qt 版本

重跑 §3 的 configure → build → install,prefix 改成 `C:\Qt\<新版>\msvc2022_64_static`,然后 `CMAKE_PREFIX_PATH` 跟着改。
