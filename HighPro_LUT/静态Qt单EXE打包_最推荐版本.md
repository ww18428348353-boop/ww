# 静态 Qt 单 EXE 打包：最推荐版本清单

适用场景：自己用、内部使用、性能优先、文件大小不重要、希望最终生成一个独立 `HighPro_LUT.exe`。

当前项目情况：

- 工程：`HighPro_LUT`
- 构建系统：CMake
- 编译器：MSVC 2022 x64
- Qt：当前动态版路径 `C:/Qt/6.11.1/msvc2022_64`
- 使用模块：`Qt6::Core`、`Qt6::Gui`、`Qt6::Widgets`
- 渲染：D3D11
- 推荐目标：静态 Qt + 静态运行时 `/MT` + Release + LTO

---

## 1. 最推荐安装内容

### 1.1 Visual Studio 2022 Community

下载地址：

```text
https://visualstudio.microsoft.com/zh-hans/vs/community/
```

安装时勾选工作负载：

```text
使用 C++ 的桌面开发
```

右侧组件确认包含：

```text
MSVC v143 x64/x86 build tools
Windows 10/11 SDK
C++ CMake tools for Windows
```

推荐 Windows SDK：

```text
10.0.22621 或更高
```

用途：编译 Qt 静态库和项目 EXE。

---

### 1.2 CMake 3.27+

下载地址：

```text
https://cmake.org/download/
```

推荐版本：

```text
CMake 3.27 或更新
```

安装时勾选：

```text
Add CMake to PATH
```

验证命令：

```bat
cmake --version
```

---

### 1.3 Ninja

下载地址：

```text
https://github.com/ninja-build/ninja/releases
```

推荐放置路径：

```text
C:\Tools\ninja\ninja.exe
```

把下面目录加入系统 PATH：

```text
C:\Tools\ninja
```

验证命令：

```bat
ninja --version
```

用途：加快 Qt 和项目编译速度。

---

### 1.4 Python 3.10+

下载地址：

```text
https://www.python.org/downloads/windows/
```

推荐版本：

```text
Python 3.10 或更新
```

安装时勾选：

```text
Add Python to PATH
```

验证命令：

```bat
python --version
```

用途：Qt 构建工具依赖。

---

### 1.5 Strawberry Perl

下载地址：

```text
https://strawberryperl.com/
```

默认安装即可。

验证命令：

```bat
perl -v
```

用途：Qt 部分构建步骤依赖。

---

### 1.6 Git

下载地址：

```text
https://git-scm.com/download/win
```

默认安装即可。

验证命令：

```bat
git --version
```

---

### 1.7 Qt 6.11.1 Source Code

推荐和当前动态 Qt 保持同版本：

```text
Qt 6.11.1
```

下载地址：

```text
https://download.qt.io/official_releases/qt/6.11/6.11.1/single/
```

下载文件类似：

```text
qt-everywhere-src-6.11.1.zip
```

推荐解压路径：

```text
C:\QtSrc\qt-everywhere-src-6.11.1
```

静态 Qt 推荐安装路径：

```text
C:\Qt\6.11.1\msvc2022_64_static
```

不要覆盖当前动态 Qt：

```text
C:\Qt\6.11.1\msvc2022_64
```

---

## 2. 本项目需要的 Qt 模块

### 必须保留

```text
qtbase
```

包含：

```text
Core
Gui
Widgets
Windows platform plugin
```

### 建议保留

```text
qtsvg
qtimageformats
```

原因：

- `qtsvg`：支持 SVG 图标或 SVG 图片资源
- `qtimageformats`：支持 GIF、ICO、JPEG 等图片格式插件

### 可以跳过

```text
qtwebengine
qtdeclarative
qtmultimedia
qtquick3d
qt3d
qtcharts
qtdoc
examples
tests
```

原因：项目不是 QML、WebEngine、多媒体或 3D 图表项目。

---

## 3. 编译静态 Qt

打开：

```text
x64 Native Tools Command Prompt for VS 2022
```

进入 Qt 源码目录：

```bat
cd /d C:\QtSrc\qt-everywhere-src-6.11.1
```

配置命令：

```bat
configure.bat ^
  -prefix C:\Qt\6.11.1\msvc2022_64_static ^
  -static ^
  -static-runtime ^
  -release ^
  -opensource ^
  -confirm-license ^
  -nomake examples ^
  -nomake tests ^
  -skip qtwebengine ^
  -skip qtdeclarative ^
  -skip qtmultimedia ^
  -skip qt3d ^
  -skip qtcharts ^
  -skip qtquick3d ^
  -skip qtlocation ^
  -skip qtpositioning ^
  -skip qtsensors ^
  -skip qtserialbus ^
  -skip qtserialport ^
  -skip qtvirtualkeyboard ^
  -skip qtwayland ^
  -skip qtwebsockets ^
  -skip qtwebview ^
  -skip qtlottie ^
  -skip qtpdf ^
  -skip qtconnectivity ^
  -skip qtdoc ^
  -skip qttranslations ^
  -skip qttools ^
  -skip qtscxml ^
  -skip qtremoteobjects ^
  -skip qtcoap ^
  -skip qtmqtt ^
  -skip qtopcua ^
  -skip qtgrpc ^
  -skip qtgraphs ^
  -skip qthttpserver ^
  -skip qtquicktimeline ^
  -skip qtshadertools ^
  -skip qtspeech ^
  -skip qt5compat ^
  -skip qtactiveqt ^
  -skip qtdebugging ^
  -skip qtinterfaceframework
```

开始编译：

```bat
cmake --build . --parallel
```

安装到目标目录：

```bat
cmake --install .
```

完成后应存在：

```text
C:\Qt\6.11.1\msvc2022_64_static
```

---

## 4. 编译本项目为静态单 EXE

打开：

```text
x64 Native Tools Command Prompt for VS 2022
```

进入项目目录：

```bat
cd /d x:\Lut变色\HighPro_LUT
```

配置项目：

```bat
cmake -S . -B build-static ^
  -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_PREFIX_PATH=C:\Qt\6.11.1\msvc2022_64_static ^
  -DHIGHPRO_STATIC_RUNTIME=ON
```

编译：

```bat
cmake --build build-static --parallel
```

预期输出：

```text
x:\Lut变色\HighPro_LUT\HighPro_LUT.exe
```

---

## 5. 推荐性能优化配置

建议后续在 `CMakeLists.txt` 里加入 Release 优化：

```cmake
if(MSVC)
    target_compile_options(HighPro_LUT PRIVATE
        $<$<CONFIG:Release>:/O2 /Ob2 /GL>
    )

    target_link_options(HighPro_LUT PRIVATE
        $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF>
    )
endif()
```

含义：

- `/O2`：速度优化
- `/Ob2`：积极内联
- `/GL`：全程序优化编译阶段
- `/LTCG`：链接时全程序优化
- `/OPT:REF`：移除未使用代码
- `/OPT:ICF`：合并重复函数

不建议默认加：

```text
/arch:AVX2
```

原因：会降低老电脑兼容性。除非确定使用电脑都支持 AVX2。

---

## 6. 不推荐方案

### 不推荐 UPX

原因：

- 增加启动解压成本
- 容易杀软误报
- 不利于签名和排错
- 你不在乎大小，所以没必要

### 不推荐单 EXE 壳工具作为正式方案

例如：

```text
Enigma Virtual Box
BoxedApp
7z SFX
```

原因：

- 可能启动慢
- 可能被杀软误报
- Qt 插件路径可能出问题
- 不是原生静态单 EXE

---

## 7. 内部使用注意事项

静态 Qt 内部使用可以。若未来公开商业分发，要重新考虑 Qt 授权。

当前目标：

```text
内部使用 + 单 EXE + 性能优先
```

推荐路线：

```text
静态 Qt 6.11.1 + MSVC 2022 + /MT + Release + LTO
```
