# HighPro_LUT 工具技术文档

> 版本: **v1.0.1**
> 项目根: `x:\Lut变色\HighPro_LUT`
> 技术栈: C++17 / Qt 6 Widgets / Direct3D 11 / HLSL / CMake / Ninja / MSVC
> 适用对象: 开发者、维护者、打包发布人员、渲染/工具链接入人员
> 配套文档: `使用指南_HighPro_LUT.md`, `README.md`, `03_TGA加载与渲染管线.md`, `06_GIF输出管线.md`

---

## 1. 项目定位

`HighPro_LUT` 是 Windows 桌面端高性能序列帧角色换色工具。

核心流程:

```text
扫描角色 TGA 分层资源
  → D3D11 实时合成预览
  → 每层独立 7 效果链调色
  → 多方案画廊管理
  → GPU 烘焙 HALD-CLUT PNG
  → 输出 add_lut/0N.png 或 GIF 预览
```

设计目标:

- 支持 `body / 00..NN / addon` 多层角色序列帧结构
- 支持方向、动作、帧播放
- 7 个 AE 风格效果: HSL、亮度对比度、曲线、通道混合、颜色平衡、照片滤镜、自然饱和度
- 支持多套方案、锁定方案、智能随机、方案细化、配色转移
- 使用 Direct3D 11 实时渲染与 GPU LUT 烘焙
- 输出标准 256×16 HALD-CLUT PNG, 兼容已有 `add_lut` 资产链路

---

## 2. 版本 v1.0.1 范围

### 2.1 已落地能力

| 模块 | 能力 |
|------|------|
| 资源扫描 | 自动扫描 `body`, 数字层 `00..NN`, `addon/<sub>` |
| 帧解析 | `^(\d)(\d{3})\.tga$`, 第 1 位方向, 后 3 位帧号 |
| 渲染 | D3D11 多层 SourceOver 合成, Gamma/sRGB 字节空间对齐 AE 默认 |
| 效果链 | 7 效果 HLSL 串联, 每层独立 `EffectStack` |
| LUT | 256×16 HALD-CLUT 加载、运行时查表、GPU 烘焙、PNG 写出 |
| 方案 | 本体方案 + 多用户方案, 方案画廊, 缩略图, 锁定 |
| 随机 | 旧随机 + LayerSlot/SchemePalette 智能随机 + 智能随机混合 |
| 细化 | 独立 `SchemeRefineDialog`, 层级微调 HSL / 亮度对比度 |
| 转移 | 按 LayerSlot 分组转移配色参数 |
| 工程 | `.hplut.json` 保存/加载、最近工程、自动恢复 |
| GIF | 离屏 D3D11 渲染 + 256 色量化 + GIF LZW 输出 |
| 打包 | 动态部署、静态单 EXE、7z SFX 安装包 |

### 2.2 版本标记注意

代码中 `CMakeLists.txt` 和 `main.cpp` 当前仍显示 `1.0.0`。本文档按用户指定生成 **v1.0.1**。若要程序内版本一致, 需同步修改:

```cpp
// src/main.cpp
QApplication::setApplicationVersion("1.0.1");
```

```cmake
# CMakeLists.txt
project(HighPro_LUT VERSION 1.0.1 ...)
```

---

## 3. 目录结构

```text
HighPro_LUT/
├── CMakeLists.txt                 构建入口
├── CMakePresets.json              CMake preset
├── build.bat                      动态构建 + windeployqt 部署
├── _build_static.bat              静态 Qt 构建脚本
├── pack.bat                       7z SFX 打包脚本
├── app.rc                         Windows 图标资源
├── assets/
│   ├── assets.qrc                 Qt 资源清单
│   ├── 颜色图.png                  默认 256×16 HALD-CLUT
│   ├── icons/app.ico              应用图标
│   └── shaders/
│       ├── fullscreen_quad.hlsl    透传/纯色/三角 shader
│       ├── recolor.hlsl            HALD-CLUT 运行时查表 shader
│       └── effect_chain.hlsl       7 效果链 + 烘焙 shader
├── extern/
│   ├── stb/                       stb_image / stb_image_write
│   ├── gif/                       gif.h
│   └── nlohmann/json.hpp          JSON 头文件
├── src/
│   ├── main.cpp                   Qt 应用入口
│   ├── app/                       应用层: 设置、控制器、工程 IO、缩略图线程
│   ├── core/                      数据结构与算法: 层、效果、方案、路径、扫描
│   ├── render/                    D3D11、纹理、shader、渲染器、导出器
│   └── ui/                        Qt Widgets UI 与 Dock 面板
├── tools/
│   ├── 7zsd.sfx                   SFX stub
│   └── sfx_config.txt             SFX 配置
└── *.md                           开发记录与技术文档
```

---

## 4. 架构总览

### 4.1 分层架构

```text
UI 层
  MainWindow / PreviewPanel / LayerTreePanel / EffectPanel / SchemePanel / SchemeRefineDialog
    ↓ 信号/槽
应用控制层
  ProjectController / AppSettings / ProjectIO / ThumbnailWorker
    ↓ 读写 Project / 触发 dirty / 发信号刷新
核心数据层
  Project / LayerData / EffectStack / SchemePalette / ResourceScanner
    ↓ 渲染请求
渲染层
  D3D11Context / D3DWidget / FrameLoader / FrameRenderer / LutBaker / PngExporter / GifExporter
    ↓ GPU / 文件
资源层
  TGA / PNG HALD-CLUT / HLSL / JSON 工程文件 / GIF
```

### 4.2 核心对象关系

```text
Project
├── sourceRoot / outputRoot
├── layers: QVector<LayerData>                 合成顺序 body → 00..NN → addon
├── hiddenLayerKeys                            显隐状态
├── currentAddonKey                            addon 单选
├── skinSafeLayerKeys                          肤色保护层
├── layerSlots                                 LayerSlot 手工语义
├── currentAction / currentDirection / frame   播放状态
├── layerLutPath                               旧 add_lut 快捷应用路径
├── schemes: QVector<Scheme>
│   ├── Scheme 0 = 默认本体, 不可编辑
│   └── Scheme 1..N = 用户方案
└── currentLayerKey / currentSchemeIndex       当前编辑上下文
```

```text
Scheme
├── name
├── isBuiltin                                  本体方案
├── isBaked                                    来自 add_lut PNG 的已烘焙方案
├── locked                                     不参与批量随机, 推荐导出目标
├── layerEffects: layerKey → EffectStack       可编辑方案效果参数
├── layerLutPath: layerKey → add_lut PNG       已烘焙方案 LUT 路径
└── palette: optional<SchemePalette>           智能随机的方案级调色盘
```

---

## 5. 主要模块说明

### 5.1 `src/main.cpp`

职责:

- 设置 Qt 高 DPI 缩放策略
- 创建 `QApplication`
- 设置应用名、组织名、版本号
- 设置 Fusion 风格与窗口图标
- 加载 `AppSettings`
- 创建并显示 `MainWindow`
- 启动后异步自动加载上次工程
- 退出时保存 `AppSettings`

关键代码:

```cpp
QApplication::setApplicationName("HighPro_LUT");
QApplication::setOrganizationName("HighPro");
QApplication::setApplicationVersion("1.0.0");
HighPro::AppSettings::instance().load();
HighPro::MainWindow win;
win.show();
```

### 5.2 `MainWindow`

文件:

- `src/ui/MainWindow.h`
- `src/ui/MainWindow.cpp`

职责:

- 构建主窗口、菜单栏、状态栏
- 创建 dock: 资源树、颜色效果、方案画廊
- 中央区挂载 `PreviewPanel`
- 处理文件/工程/导出/视图/调试入口
- 保存恢复窗口几何与 dock 布局

菜单入口:

| 菜单 | 关键动作 |
|------|----------|
| 文件 | 打开源目录、设置输出目录、新建/打开/保存工程、最近工程 |
| 扫描 | 重新扫描当前源目录 |
| 方案 | 撤销/重做、新增/删除、切换方案、应用 add_lut/01..06 |
| 导出 | 导出当前、全部、锁定方案 LUT PNG |
| 视图 | dock toggle、适合窗口、100%、全屏画布 |
| 调试 | F12 抓当前帧 + CPU reference 对比 |

### 5.3 `ProjectController`

文件:

- `src/app/ProjectController.h`
- `src/app/ProjectController.cpp`

职责:

- 应用级单例, 持有唯一 `Project`
- 资源扫描与工程加载/保存
- 当前动作/方向/帧切换
- 显隐、addon 单选、肤色保护、LayerSlot 设置
- 方案增删改、锁定、切换
- 效果变化通知
- 随机/智能随机/重置/复制
- 细化方案保存与配色转移
- undo/redo 快照

关键接口:

```cpp
bool loadSource(const QString& sourceRoot, QString* errorOut = nullptr);
bool saveProject(const QString& path, const QJsonObject& uiState = {}, QString* errorOut = nullptr);
bool loadProject(const QString& path, QJsonObject* outUiState = nullptr, QString* errorOut = nullptr);

void setCurrentAction(const QString& a);
void setCurrentDirection(int d);
void setCurrentFrame(int f);
void advanceFrame();

void setLayerVisible(const QString& layerKey, bool visible);
void setCurrentAddon(const QString& layerKey);
void setLayerSkinSafe(const QString& layerKey, bool skinSafe);
void setLayerSlot(const QString& layerKey, LayerSlot slot);

int  addScheme(const QString& name = {});
bool removeScheme(int idx);
void renameScheme(int idx, const QString& name);
void setSchemeLocked(int idx, bool locked);
void setCurrentSchemeIndex(int i);

void randomizeCurrentLayer();
void randomizeAllLayers(bool sameSeedAllLayers = true);
void randomizeAllSchemes(bool includeBaked);
void smartRandomizeCurrentLayer();
void smartRandomizeAllLayers();
void smartRandomizeAllSchemes(bool includeBaked);
void mixRandomizeAllSchemes(bool includeBaked);
```

刷新信号:

```cpp
void projectLoaded();
void actionChanged();
void directionChanged();
void frameChanged();
void visibilityChanged();
void lutChanged();
void effectsChanged();
void currentLayerKeyChanged();
void schemesChanged();
void currentSchemeChanged();
```

刷新原则:

- Project 结构变化 → `projectLoaded`
- 当前动作/方向/帧变化 → 对应信号 + `frameChanged`
- 显隐/slot/skin 变化 → `visibilityChanged` + 需要时 `effectsChanged`
- EffectStack 变化 → `effectsChanged`
- 方案列表变化 → `schemesChanged`
- 当前方案变化 → `currentSchemeChanged` + `effectsChanged`

### 5.4 `ProjectIO`

文件:

- `src/app/ProjectIO.h`
- `src/app/ProjectIO.cpp`

职责:

- `Project` ↔ JSON
- `Scheme` ↔ JSON
- `EffectStack` ↔ JSON
- `SchemePalette` ↔ JSON
- Curve 点序列 JSON 化

读写目标: `.hplut.json`

特性:

- 使用 Qt `QJsonDocument` / `QJsonObject`
- 文件路径走 `QFile`, 支持中文路径
- 兼容缺字段默认值
- `layerEffects` 按 `layerKey` 保存

### 5.5 `AppSettings`

文件:

- `src/app/AppSettings.h`
- `src/app/AppSettings.cpp`

职责:

- 注册表持久化全局设置
- 使用 `QSettings`, 路径: `HKCU\SOFTWARE\HighPro\HighPro_LUT`

保存内容:

| 字段 | 默认值 | 说明 |
|------|--------|------|
| windowSize | 1440×900 | 窗口尺寸 |
| windowPos | 200,100 | 窗口位置 |
| windowMaximized | false | 最大化状态 |
| dockState | 空 | dock 布局二进制 |
| lastSourceDir | 空 | 上次源目录 |
| lastOutputDir | 空 | 上次输出目录 |
| lastProjectPath | 空 | 上次工程 |
| autoLoadLastProject | true | 启动自动恢复 |
| recentProjects | 空 | 最近工程, 上限 10 |
| bgColor | R60G60B60 | 预览背景色 |
| bgImage | 空 | 背景图片 |
| fps | 10 | 默认帧率 |

---

## 6. 数据模型

### 6.1 `LayerData`

文件:

- `src/core/LayerData.h`
- `src/core/LayerData.cpp`

```cpp
enum class LayerKind { Body, Numbered, Addon };

struct Action {
    QString name;
    QMap<int, QVector<QString>> framesByDir;
};

struct LayerData {
    QString displayName;
    QString rootDir;
    LayerKind kind;
    int numberedIdx;
    int addonSubIdx;
    QString addLutDir;
    QMap<QString, Action> actions;
    QString key() const;
};
```

`LayerData::key()` 是稳定标识, 用于:

- 当前层选择
- 显隐集合
- 肤色保护集合
- `layerEffects` map
- `layerLutPath` map
- 缩略图缓存与转移逻辑

### 6.2 `LayerSlot`

半语义层角色, 智能随机核心输入。

```cpp
enum class LayerSlot : int {
    Unknown = 0,
    Skin,
    Hair,
    Clothing,
    Skirt,
    Decor01,
    Decor02,
    WeaponMetal,
    WeaponNonMetal,
};
```

默认启发式 (`Project::defaultSlotFor`):

| 层 | 默认 slot |
|----|-----------|
| body | Skin |
| numbered 00 | Clothing |
| numbered 01 | Skirt |
| numbered 02 | Hair |
| numbered 03 | Decor01 |
| numbered 04 | Decor02 |
| numbered 05+ | Decor01 |
| addon | Decor02 |

实际 slot (`Project::slotFor`):

```text
skinSafe 命中 → Skin
else layerSlots 命中且非 Unknown → 用户指定
else → defaultSlotFor()
```

### 6.3 `EffectStack`

文件:

- `src/core/ColorEffect.h`
- `src/core/ColorEffect.cpp`

7 效果固定顺序:

```cpp
enum {
    EHsl = 0,
    EBrtCtr,
    ECurves,
    EChMix,
    EColorBal,
    EPhotoFilter,
    EVibrance,
    kCount = 7,
};
```

结构:

```cpp
struct EffectStack {
    std::array<bool, kCount> enabled;
    HslParams hsl;
    BrtCtrParams brtCtr;
    CurveParams curves;
    ChMixParams chMix;
    ColorBalParams colorBal;
    PhotoFilterParams photoFilter;
    VibranceParams vibrance;
    int shadowProtectThreshold = 8;
};
```

设计原则:

- 全 disabled 或 enabled 但参数 identity → `isIdentity() == true`
- 每个 Scheme 每个 Layer 一个 EffectStack
- 影子保护阈值默认 8, 0 表示关闭
- UI 改任意参数立刻调用 `ProjectController::notifyEffectsChanged()`

### 6.4 `SchemePalette`

文件:

- `src/core/SchemePalette.h`
- `src/core/SchemePalette.cpp`

用途: 智能随机的方案级调色盘。

```cpp
struct SchemePalette {
    int primaryHue;
    int secondaryHue;
    int accentHue;
    int accent2Hue;
    int glowHue;
    MetalTone metal;
    StyleMood mood;
    ClothingTone clothing;
    int saturationBias;
    int lightnessBias;
};
```

生成逻辑:

```cpp
SchemePalette generatePalette(int schemeIdx, quint32 seed = 0);
```

- `schemeIdx <= 0` 兜底取第 1 套风格
- `schemeIdx 1..N` 对应 27 套 `kSchemeStyles[(idx-1) % 27]`
- seed = 0 使用全局随机源
- 生成后按 ±10° 做色相微抖动

---

## 7. 资源扫描流程

文件:

- `src/core/ResourceScanner.h`
- `src/core/ResourceScanner.cpp`

入口:

```cpp
ResourceScanner::Result ResourceScanner::scan(const QString& sourceRoot);
```

扫描规则:

```text
sourceRoot/
├── body/              → LayerKind::Body
├── 00/                → LayerKind::Numbered, numberedIdx=0
├── 01/                → numberedIdx=1
├── ...
└── addon/
    ├── 01/            → LayerKind::Addon, addonSubIdx=0
    └── 02/            → addonSubIdx=1
```

动作目录规则:

```text
<layerRoot>/<actionName>/<direction><frame3>.tga
```

示例:

```text
body/stand/0000.tga     dir=0, frame=000
body/stand/0001.tga     dir=0, frame=001
body/stand/1000.tga     dir=1, frame=000
```

扫描输出:

```cpp
struct Result {
    QVector<LayerData> layers;
    QStringList warnings;
    QString error;
    bool isOk() const;
};
```

合成顺序:

```text
body → numbered ascending → addon
```

UI 资源树反向显示为 AE 风格:

```text
addon → numbered descending/ascending UI 规则 → body
```

---

## 8. 渲染管线

### 8.1 总览

```text
TGA 文件 (Straight RGBA, sRGB 字节)
  ↓ stbi_load_from_memory(req=4)
RGBA CPU bytes
  ↓ D3D11Texture::createFromRGBA
DXGI_FORMAT_R8G8B8A8_UNORM SRV
  ↓ FrameRenderer::render / renderCells
HLSL shader:
  - fullscreen_quad.hlsl
  - recolor.hlsl
  - effect_chain.hlsl
  ↓ SourceOver BlendState
B8G8R8A8_UNORM RTV / SwapChain
  ↓ Present
屏幕
```

关键约束:

- 输入 TGA = Straight Alpha
- 不预乘, 不反预乘
- SRV/RTV 均使用 `*_UNORM`, 非 `*_SRGB`
- 合成在 Gamma/sRGB 字节空间做, 对齐 AE 默认
- BlendState: `SRC_ALPHA / INV_SRC_ALPHA`

### 8.2 D3D11 初始化

文件:

- `src/render/D3D11Context.h`
- `src/render/D3D11Context.cpp`

职责:

- 创建 D3D11 Device / ImmediateContext
- 获取 DXGI Factory
- 创建 SwapChain
- 统一暴露 device/context/factory/featureLevel

```cpp
bool initialize(QString* errorOut = nullptr);
bool createSwapChainForHwnd(HWND hwnd, UINT width, UINT height, ...);
```

### 8.3 画布控件 `D3DWidget`

文件:

- `src/ui/D3DWidget.h`
- `src/ui/D3DWidget.cpp`

职责:

- Qt QWidget 内嵌 D3D11 swapchain
- `paintEngine() == nullptr`, 禁 Qt 绘制
- 处理 show/resize/paint/event
- 管理 RTV 与 render callback
- 处理 zoom/pan/鼠标滚轮/双击复位

关键接口:

```cpp
void setRenderCallback(RenderFn fn);
void requestRender();
void setAutoRender(bool on);
ID3D11RenderTargetView* currentRtv() const;
void setContentZoom(double z, QPointF anchorWidgetPx = {});
void resetView();
void fitView(int contentW, int contentH);
```

### 8.4 纹理加载 `D3D11Texture`

文件:

- `src/render/D3D11Texture.h`
- `src/render/D3D11Texture.cpp`

职责:

- CPU RGBA → GPU Texture2D + SRV
- 图片 bytes → stb 解码 → GPU
- 路径加载, 支持中文路径

```cpp
bool createFromRGBA(const uint8_t* pixels, int width, int height, ...);
bool loadFromMemory(const uint8_t* data, int size, QString* errorOut = nullptr, bool premultiply = false);
bool loadFromPath(const QString& path, QString* errorOut = nullptr, bool premultiply = false);
```

纹理格式:

```cpp
DXGI_FORMAT_R8G8B8A8_UNORM
MipLevels = 1
BindFlags = D3D11_BIND_SHADER_RESOURCE
```

### 8.5 帧缓存 `FrameLoader`

文件:

- `src/render/FrameLoader.h`
- `src/render/FrameLoader.cpp`

职责:

- TGA 加载缓存
- 后台预解码 + 主线程上传 GPU
- LRU 池控制内存

关键接口:

```cpp
std::shared_ptr<D3D11Texture> get(const QString& path);
void prefetch(const QStringList& paths);
void pump(int maxUploads = 4);
void clear();
void setMaxEntries(int n);
```

线程模型:

```text
UI/渲染线程:
  get(path) 命中 → 返回纹理
  get(path) miss → 同步加载兜底, 入 cache
  pump() → 上传后台 decode 队列

DecodeThread:
  readAll(path)
  stbi_load_from_memory(req=4)
  push Decoded {path, rgba, w, h}
```

### 8.6 `FrameRenderer`

文件:

- `src/render/FrameRenderer.h`
- `src/render/FrameRenderer.cpp`

职责:

- 编译 shader
- 创建 sampler / constant buffer / blend state
- 单方案渲染
- 多方案 cells 渲染
- 上传 EffectCB 与 Curve LUT
- 缓存 label 纹理 (方案编号)

关键数据:

```cpp
struct DrawItem {
    D3D11Texture* frame;
    D3D11Texture* lut;
    const EffectStack* effects;
    QRectF dst;
    bool bypassRecolor;
};

struct Cell {
    QVector<DrawItem> items;
    QString label;
    QRectF rect;
    bool selected;
    bool locked;
};
```

渲染决策:

```text
层不可见 → skip
肤色保护 / 本体 → fullscreen_quad 透传
已烘焙方案 layerLutPath 有值 → recolor.hlsl HALD-CLUT 查表
可编辑方案 EffectStack 非 identity → effect_chain.hlsl 实时效果链
否则 → fullscreen_quad 透传
```

---

## 9. HLSL Shader 设计

### 9.1 `fullscreen_quad.hlsl`

用途:

- 普通 TGA 透传
- 背景纯色填充
- 选中三角标识

特点:

- `VSMain` 用 `SV_VertexID` 生成 fullscreen quad 6 顶点
- `PSMain` 采样 t0, 输出 `(rgb, alpha)`
- `PSMainSolid` 输出纯色
- `PSMainTriangleUp` 画 ▲

### 9.2 `recolor.hlsl`

用途:

- 已烘焙 `add_lut/0N.png` 运行时变色

核心:

```text
src.rgb → HALD-CLUT 16³ 查表
r4 = R * 15
g4 = G * 15
b4 = B * 15
b0/b1 两个 B slice
RG 双线性 + B 线性插值
输出 mapped.rgb + src.a
```

### 9.3 `effect_chain.hlsl`

用途:

- 可编辑方案实时预览
- `LutBaker` 烘焙 add_lut PNG

固定顺序:

1. 色相/饱和度 (`applyHsl`, HSV/HSB 风格)
2. 亮度/对比度 (`applyBrtCtr`)
3. 曲线 (`applyCurves`, 256×1 curve LUT)
4. 通道混合 (`applyChMix`)
5. 颜色平衡 (`applyColorBalance`, smoothstep 三区)
6. 照片滤镜 (`applyPhotoFilter`)
7. 自然饱和度 (`applyVibrance`)
8. 影子保护 (`shadowProtectThreshold`)

Constant Buffer 对齐:

```hlsl
cbuffer EffectCB : register(b1)
{
    int4 enableMask;       // x=bitmask, y=shadow threshold
    float4 hsl;
    float4 brtCtr;
    float4 mixR;
    float4 mixG;
    float4 mixB;
    int4 mixFlag;
    float4 balShadow;
    float4 balMid;
    float4 balHigh;
    int4 balFlag;
    float4 photoColor;
    int4 photoFlag;
    float4 vibrance;
};
```

入口:

| Entry | 用途 |
|-------|------|
| `VSMain` | quad vertex shader |
| `PSMain` | 预览: 采样 TGA → 效果链 → 输出 |
| `PSMainBake` | 烘焙: 采样默认 HALD → 效果链 → 输出 LUT |

---

## 10. LUT 与烘焙管线

### 10.1 HALD-CLUT 格式

默认图: `assets/颜色图.png`

尺寸: `256×16`

编码:

```text
x ∈ [0,255], y ∈ [0,15]
R = (x mod 16) * 17
G = y * 17
B = floor(x / 16) * 17
```

总色数: `16 × 16 × 16 = 4096`

### 10.2 `HaldClut`

文件:

- `src/core/HaldClut.h`
- `src/core/HaldClut.cpp`

职责:

- 从路径加载 LUT 为 `D3D11Texture`
- 从内置默认资源加载 LUT
- 校验默认 HALD 编码
- 强制影子保护角点

关键接口:

```cpp
static bool loadAsTexture(const QString& path, D3D11Texture& outTex, QString* errorOut);
static bool loadDefaultAsTexture(D3D11Texture& outTex, QString* errorOut);
static void enforceShadowLock(uint8_t* rgba256x16);
static bool isDefaultHaldEncoded(const uint8_t* rgba256x16, int* mismatchOut = nullptr);
```

### 10.3 `LutBaker`

文件:

- `src/render/LutBaker.h`
- `src/render/LutBaker.cpp`

职责:

- 创建 256×16 离屏 RT
- 绑定默认 HALD 图作为输入
- 使用 `effect_chain.hlsl::PSMainBake` 输出新 LUT
- Readback 到 CPU bytes
- 强制 shadow lock

关键接口:

```cpp
bool init(QString* errorOut = nullptr);
bool bake(const EffectStack& stack, QString* errorOut = nullptr);
const std::array<uint8_t, 256*16*4>& bytes() const;
ID3D11ShaderResourceView* lutSrv() const;
```

烘焙流程:

```text
EffectStack
  ↓ uploadEffectCB
CurveParams → CurveSolver → 256×1 curve LUT
  ↓ render fullscreen quad 256×16
GPU RT 256×16 RGBA
  ↓ staging readback
CPU uint8_t[256*16*4]
  ↓ enforceShadowLockOnCpu()
bytes()
```

### 10.4 `PngExporter`

文件:

- `src/render/PngExporter.h`
- `src/render/PngExporter.cpp`

职责:

- 写 256×16 RGBA PNG
- 导出当前方案 / 全部方案 / 锁定方案
- 按层写入对应 `add_lut/0N.png`

接口:

```cpp
static bool writeLutPng(const uint8_t* rgba256x16, const QString& outPath, QString* errorOut = nullptr);
static Result exportCurrentScheme(const Project& project, LutBaker& baker, const QString& outputRoot = QString());
static Result exportAllSchemes(const Project& project, LutBaker& baker, const QString& outputRoot = QString());
static Result exportLockedSchemes(const Project& project, LutBaker& baker, const QString& outputRoot = QString());
```

导出命名:

```text
<outputRoot>/<layer relative path>/add_lut/0N.png
```

其中 `N = schemeIdx` 两位补零。

---

## 11. GIF 输出管线

文件:

- `src/render/GifExporter.h`
- `src/render/GifExporter.cpp`
- `src/ui/PreviewPanel.cpp`

入口:

```cpp
void PreviewPanel::onExportGif();
```

总流程:

```text
用户点击 输出 GIF 图...
  ↓
PreviewPanel 收集参数
  - 输出路径
  - 帧数
  - 网格尺寸
  - 是否显示方案 ID
  - 循环次数
  ↓
GifExporter::exportGif
  1. 创建离屏 D3D11 BGRA RTV + staging
  2. 逐帧 setFrame(f) → renderOnce(rtv)
  3. Readback RGBA frames
  4. 每帧 Wu Quantization / HQ palette
  5. 可选 Floyd-Steinberg / Sierra / Ordered / NoDither
  6. 写 GIF header + LCT frame + LZW data
  7. 写 trailer 0x3B
  8. 中文路径 TEMP 中转复制
```

`GifExporter::Options` 关键字段:

- 输出路径
- gifW / gifH
- delay
- loopCount
- dither mode
- show label

注意:

- GIF 输出期间临时停播放, 锁定 zoom=1.0, 避免 UI 装饰进入图像
- Alpha 已经在预览阶段合成到背景, GIF 内通常不保留半透明
- `m_renderingForGif` 控制选中三角等 UI 装饰隐藏

---

## 12. UI 细节模块

### 12.1 `PreviewPanel`

文件:

- `src/ui/PreviewPanel.h`
- `src/ui/PreviewPanel.cpp`

职责:

- 创建 D3D 画布
- 动作/方向/帧率/播放控制
- 背景色与背景图
- 方案网格渲染参数 (gapX/gapY/label)
- GIF 参数与导出入口
- 帧预取
- 调用 `FrameRenderer`

关键状态:

```cpp
int m_charGapXPx = -300;
int m_charGapYPx = -260;
bool m_showLabel = true;
int m_labelGapY = 200;
bool m_hideUnlocked = false;
int m_gifLoop = 0;
bool m_gifShowId = true;
```

### 12.2 `LayerTreePanel`

文件:

- `src/ui/LayerTreePanel.h`
- `src/ui/LayerTreePanel.cpp`

职责:

- 显示资源层树
- checkbox 显隐
- addon 单选
- 右键设置 LayerSlot / 肤色保护
- 发出 `currentLayerChanged(layerKey)`

设计:

- UI 显示 AE 风格上层在上
- 实际 Project 保持合成顺序
- `refresh()` 完整重建
- `syncCheckStates()` 仅同步状态, 不重建结构

### 12.3 `EffectPanel`

文件:

- `src/ui/EffectPanel.h`
- `src/ui/EffectPanel.cpp`

职责:

- 当前层 `EffectStack` 编辑 UI
- 7 效果分组
- 智能/随机/重置按钮区
- 响应当前层/当前方案/工程变化重建 UI

关键函数:

```cpp
void rebuildForCurrentLayer();
QWidget* buildSection(const QString& title, int idx, QWidget* body);
```

当前栈获取规则:

```cpp
EffectStack* currentStack()
{
    auto& proj = ProjectController::instance().projectMut();
    if (proj.currentLayerKey.isEmpty()) return nullptr;
    auto* sc = proj.currentScheme();
    if (!sc || sc->isBuiltin) return nullptr;
    if (!sc->layerEffects.contains(proj.currentLayerKey))
        sc->layerEffects.insert(proj.currentLayerKey, EffectStack{});
    return &sc->layerEffects[proj.currentLayerKey];
}
```

### 12.4 `SchemePanel`

文件:

- `src/ui/SchemePanel.h`
- `src/ui/SchemePanel.cpp`

职责:

- 方案画廊列表
- 缩略图显示与异步刷新
- 当前方案切换
- 方案重命名、删除、锁定
- 右键入口: 细化方案、配色方案转移

依赖:

- `ThumbnailWorker` 后台缩略图处理
- `LutBaker` 主线程烘焙每个方案的 LUT bytes

### 12.5 `SchemeRefineDialog`

文件:

- `src/ui/SchemeRefineDialog.h`
- `src/ui/SchemeRefineDialog.cpp`

职责:

- 方案细化弹窗
- 左侧层列表
- 右侧简化效果控件
- 只修改 HSL / BrtCtr, 保留其他 5 效果
- debounce 应用到 Project, 实时刷新预览
- 本地 undo/redo
- 保存 / 取消恢复

核心原则:

```text
已有 EffectStack → 拷贝到工作缓存
修改 HSL → enabled[EHsl] = true
修改 BrtCtr → enabled[EBrtCtr] = true
其他字段不动
取消 → 恢复原快照
保存 → 写入 Scheme.layerEffects[layerKey]
```

---

## 13. 智能随机系统

### 13.1 旧随机 `randomizeStack`

接口:

```cpp
void randomizeStack(EffectStack& s, quint32 seed = 0);
```

特点:

- 每层独立随机
- 各效果按概率开启
- 参数在美观安全范围内随机
- 不关心层语义

适合:

- 彩蛋风格
- 快速探索
- 不需要整体统一的素材

### 13.2 智能随机 `randomizeStackBySlot`

接口:

```cpp
void randomizeStackBySlot(
    EffectStack& s,
    LayerSlot slot,
    const SchemePalette& palette,
    quint32 seed = 0);
```

按 LayerSlot 策略:

| Slot | 策略 |
|------|------|
| Skin | reset, 不变色 |
| Hair | secondaryHue ±15°, 低饱和, 保明度 |
| Clothing | primaryHue ±8°, 主色承载 |
| Skirt | primary / secondary ±15°, 大面积低饱 |
| Decor01 | accentHue ±10°, 中高饱 |
| Decor02 | accent2 / glowHue ±10°, 高饱 + 提亮 |
| WeaponMetal | 固定金属色池 + 高对比保高光 |
| WeaponNonMetal | 70% 随 Clothing, 30% 随 Decor01 |

### 13.3 批量智能逻辑

`ProjectController` 中对应方法:

| 方法 | 范围 | include baked |
|------|------|---------------|
| `smartRandomizeCurrentLayer()` | 当前方案当前层 | 否 |
| `smartRandomizeAllLayers()` | 当前方案所有可见非肤色层 | 否 |
| `smartRandomizeAllSchemes(false)` | 所有可编辑方案 | 排除 baked |
| `smartRandomizeAllSchemes(true)` | 所有非本体方案 | baked 降级为可编辑 |
| `mixRandomizeAllSchemes(true)` | 智能与旧随机 50/50 混合 | baked 降级 |

锁定规则:

- locked 方案不参与批量随机/重置 (按当前实现策略)
- 导出锁定方案时只处理 locked=true 的非本体方案

---

## 14. 工程文件与兼容性

### 14.1 `.hplut.json` 内容

包含:

- 源目录 / 输出目录
- 层扫描结果
- 显隐集合
- addon 当前选中项
- 肤色保护集合
- layerSlots
- 当前动作/方向/帧
- 旧 `layerLutPath`
- schemes[]
- currentSchemeIndex
- currentLayerKey
- UI 状态

### 14.2 已烘焙方案 `isBaked`

`isBaked=true` 时:

```text
渲染走 layerLutPath → recolor.hlsl
EffectPanel 禁编辑
SchemeRefineDialog / 随机全部 / 重置全部 会提示降级
降级后:
  isBaked=false
  layerLutPath 清空
  layerEffects 写入新参数
```

### 14.3 老 `add_lut/01..06.png` 兼容

菜单 `方案 → 使用 add_lut/01..06.png`:

- 遍历所有层, 找各自 `add_lut/<filename>`
- 命中则写 `Project::layerLutPath[layerKey]`
- 渲染时直接使用老 LUT
- `Ctrl+Shift+0` 清除所有 LUT 引用

---

## 15. 构建与打包

### 15.1 依赖

| 工具 | 版本 / 路径 |
|------|-------------|
| Visual Studio | `C:\Program Files\Microsoft Visual Studio\18\Community` |
| Qt | `C:\Qt\6.11.1\msvc2022_64` |
| CMake | `C:\Qt\Tools\CMake_64\bin` |
| Ninja | `C:\Qt\Tools\Ninja` |
| Windows SDK | 10.0.22621+ |
| 7-Zip | `C:\Program Files\7-Zip\7z.exe` |

### 15.2 动态构建

脚本: `build.bat`

```cmd
cd /d x:\Lut变色\HighPro_LUT
build.bat          :: Release
build.bat debug    :: Debug
```

流程:

1. 设置 UTF-8 codepage
2. 调 `vcvars64.bat`
3. 设置 Qt/CMake/Ninja PATH
4. `cmake --preset msvc-release` 或 `msvc-debug`
5. `cmake --build build\release`
6. 用 `%TEMP%\HighPro_LUT_deploy` 中转执行 `windeployqt`
7. 拷回 Qt dll 与插件目录到项目根

输出:

```text
HighPro_LUT.exe
Qt6Core.dll / Qt6Gui.dll / Qt6Widgets.dll / ...
platforms/qwindows.dll
imageformats/*.dll
...
```

### 15.3 静态构建

脚本: `_build_static.bat`

```cmd
_build_static.bat
```

关键参数:

```cmd
cmake -S . -B build-static -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/msvc2022_64_static ^
  -DHIGHPRO_STATIC_RUNTIME=ON
```

输出名逻辑:

```cmake
if(HIGHPRO_STATIC_RUNTIME)
    OUTPUT_NAME "LUT_Pro"
else()
    OUTPUT_NAME "HighPro_LUT"
endif()
```

### 15.4 SFX 打包

脚本: `pack.bat`

流程:

```text
1. 清理 dist/
2. 复制 HighPro_LUT.exe + *.dll + Qt 插件目录
3. 7z a -t7z -mx=9 -mfb=64 -ms=on -mmt=on dist.7z dist\*
4. copy /b 7zsd.sfx + sfx_config.txt + dist.7z = HighPro_LUT_setup.exe
```

SFX 配置:

```text
Title="HighPro_LUT 高性能序列帧角色换色工具"
BeginPrompt="是否安装 HighPro_LUT? 程序会解压到 %TEMP% 后启动"
RunProgram="HighPro_LUT.exe"
GUIMode="2"
```

---

## 16. 调试与验证

### 16.1 构建验证

推荐每次改代码后:

```cmd
build.bat
HighPro_LUT.exe
```

验证项:

- 程序能启动
- 打开源目录不崩
- 资源树层数正确
- 播放帧数正确
- 方案切换刷新正确
- 拖 HSL 滑块实时生效
- 导出当前方案生成 PNG

### 16.2 渲染差异验证

快捷键: `F12`

输出到桌面:

```text
our.png         GPU 当前 RTV dump
reference.png   CPU LUT reference
```

用于排查:

- RGB/BGRA 通道错位
- sRGB/linear 差异
- premultiply 问题
- alpha 光晕
- LUT 查表偏移

### 16.3 常见问题定位

| 现象 | 优先检查 |
|------|----------|
| 程序打不开 | Qt dll / platforms/qwindows.dll 是否同目录部署 |
| 中文路径构建失败 | windeployqt 不支持中文, `build.bat` 中转逻辑是否执行 |
| 画面黑 | D3D11 init / swapchain / shader 编译 errorOut |
| 图层少 | `ResourceScanner` 正则、动作目录、帧名 |
| 透明边光晕 | straight alpha + gamma blend 是否被改动 |
| 颜色浅/深 | 是否误用 `_SRGB` 格式或 linear 合成 |
| 导出 PNG 为空 | `LutBaker::init`, `createRt`, `readbackToCpu` |
| GIF 卡顿 | 帧数 × 方案数太大, readback 与量化耗时正常 |

---

## 17. 性能设计

### 17.1 GPU 路径

- 单帧多层合成走 GPU pixel shader
- LUT 烘焙仅 256×16 RT, 成本极低
- Curve LUT 256×1, 参数变更时更新
- Label 纹理缓存, 避免重复 QPainter 生成

### 17.2 CPU/内存路径

- `FrameLoader` LRU 控制纹理数量
- 后台线程只 decode, 主线程 pump 上传 GPU
- 缩略图使用 `ThumbnailWorker` 后台 CPU LUT, 不阻塞 UI
- GIF 必须 readback 全帧, 适合离线导出

### 17.3 预期量级

| 操作 | 预期 |
|------|------|
| 单 LUT 烘焙 | < 1 ms |
| 单层实时变色 | < 1 ms |
| 4000+ 帧首次预览 | 依赖磁盘 IO, 后续缓存顺滑 |
| 28 方案网格预览 | 依赖层数与显卡, 可隐藏未锁定方案减负 |
| GIF 导出 | 离线耗时, 与帧数 × 输出分辨率成正比 |

---

## 18. 代码约定

### 18.1 C++

- C++17
- Qt Widgets, signal/slot
- 命名空间 `HighPro`
- 文件路径统一使用 `QString`, Windows API 前转换 wide string
- 不在渲染热路径抛异常
- 失败通过 `bool + QString* errorOut` 返回
- UI 文案使用 `QStringLiteral`

### 18.2 D3D11

- 使用 `Microsoft::WRL::ComPtr`
- 纹理默认 `UNORM`, 不使用 `_SRGB`
- Alpha 保持 Straight
- BlendState 固定 SourceOver
- Shader 运行时从 Qt qrc 读取源码并编译

### 18.3 数据持久化

- JSON 使用 Qt `QJson*`
- 新字段必须有默认值
- enum 必须有 string helper, 未识别回退 Unknown/默认
- 不把二进制纹理/缓存写入工程文件

### 18.4 UI

- Dock 可嵌套、可浮动
- 变更 Project 后必须发信号刷新
- 避免在 UI 线程做长耗时, 缩略图/GIF 例外按当前设计处理
- 控件 rebuild 时使用 `QSignalBlocker` 避免递归触发

---

## 19. 外部依赖与许可证注意

| 依赖 | 用途 | 位置 |
|------|------|------|
| Qt 6 | UI / 文件 / JSON / 线程 / 图像 | 外部安装 |
| Direct3D 11 | GPU 渲染与烘焙 | Windows SDK |
| stb_image | TGA/PNG 解码 | `extern/stb` |
| stb_image_write | PNG 写出 | `extern/stb` |
| gif.h | GIF LZW 编码与调色板工具 | `extern/gif` |
| nlohmann/json | 预留/历史 JSON 头 | `extern/nlohmann` |

发布前需确认第三方头文件 license 与产品发布策略匹配。

---

## 20. 后续维护建议

### 20.1 版本同步

把以下位置统一为 `1.0.1`:

- `CMakeLists.txt`: `project(... VERSION 1.0.1)`
- `src/main.cpp`: `QApplication::setApplicationVersion("1.0.1")`
- 安装包文件名或 SFX 标题可选追加版本

### 20.2 代码质量改进

- 将 `EffectPanel` 中重复 SliderRow 抽到公共 widget
- 将 `SchemeRefineDialog` 与 `EffectPanel` 的 HSL/BrtCtr 构建逻辑复用
- 给 `ProjectIO` 加 schema version 字段
- 给 `ResourceScanner` 增加单元测试目录样例
- 给 HLSL 编译错误弹窗增加 qrc 路径与 entry 名

### 20.3 渲染改进方向

- 添加可选 Linear 合成模式用于引擎对齐
- LUT 采样精度可升级到 32³ 或 64³, 代价是 PNG 尺寸和兼容性
- GIF 输出支持全局调色板/局部调色板切换策略
- FrameLoader LRU 参数暴露到设置界面

### 20.4 产品改进方向

- 方案批量重命名模板
- 方案评分 / 收藏
- LayerSlot 自动提示
- 导出后自动打开文件夹
- CLI 批处理模式

---

## 21. 快速索引

| 想找 | 文件 |
|------|------|
| 应用入口 | `src/main.cpp` |
| 菜单 / 主窗口 | `src/ui/MainWindow.cpp` |
| 中央预览与 GIF 入口 | `src/ui/PreviewPanel.cpp` |
| 资源树 | `src/ui/LayerTreePanel.cpp` |
| 7 效果 UI | `src/ui/EffectPanel.cpp` |
| 方案画廊 | `src/ui/SchemePanel.cpp` |
| 细化弹窗 | `src/ui/SchemeRefineDialog.cpp` |
| 应用控制器 | `src/app/ProjectController.cpp` |
| 工程 JSON | `src/app/ProjectIO.cpp` |
| 设置注册表 | `src/app/AppSettings.cpp` |
| 缩略图线程 | `src/app/ThumbnailWorker.cpp` |
| 层/动作模型 | `src/core/LayerData.h` |
| 项目/方案模型 | `src/core/Project.h` |
| 效果参数模型 | `src/core/ColorEffect.h` |
| 智能调色盘 | `src/core/SchemePalette.h` |
| 资源扫描 | `src/core/ResourceScanner.cpp` |
| D3D 上下文 | `src/render/D3D11Context.cpp` |
| 纹理加载 | `src/render/D3D11Texture.cpp` |
| 帧缓存 | `src/render/FrameLoader.cpp` |
| 主渲染器 | `src/render/FrameRenderer.cpp` |
| LUT 烘焙 | `src/render/LutBaker.cpp` |
| PNG 导出 | `src/render/PngExporter.cpp` |
| GIF 导出 | `src/render/GifExporter.cpp` |
| 7 效果 Shader | `assets/shaders/effect_chain.hlsl` |
| LUT 查表 Shader | `assets/shaders/recolor.hlsl` |
| 透传 Shader | `assets/shaders/fullscreen_quad.hlsl` |

---

> **工具技术文档 v1.0.1** · 生成日期: 2026-06-04
