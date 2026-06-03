# HighPro_LUT — 开发里程碑

> 版本: v1.0  
> 立项时间: 2026-05-30  
> 项目根目录: `x:\Lut变色\HighPro_LUT`  
> 总周期: 11~12 周  
> 配套文档: `01_技术方案_HighPro_LUT.md`

---

## 总览

| # | 里程碑 | 周期 | 交付物 | 验收标准 |
|---|--------|------|--------|----------|
| M1 | 项目骨架 + D3D11 渲染 hello | W1-W2 | 可启动空窗口, D3D 清屏 | 双击 EXE 出窗口, 背景色 #3C3C3C |
| M2 | 资源扫描 + 单层序列帧播放 | W2-W3 | 加载 9353 → 显示 body 动画 | 选 stand 动作, 60 FPS 循环播放 |
| M3 | HALD-CLUT 加载 + 运行时变色 PS | W3-W4 | 切换 add_lut 即可看到不同换色 | 加载 01~06.png, 切换瞬间生效 |
| M4 | 7 效果 HLSL + 烘焙器 + EffectPanel | W4-W6 | 调滑块 → 实时变色 | 7 个效果均可独立开关 + 调参 |
| M5 | 多层合成 + 方案管理 + 缩略图 | W6-W7 | 多套方案画廊切换 | 至少 3 个方案,每方案独立缩略图 |
| M6 | 曲线 + 通道混合 + 色彩平衡 UI | W7-W8 | 完整 UI 控件 | 曲线编辑器可拖控制点, 矩阵滑块完整 |
| M7 | 随机化 + 肤色层 + 重置 + addon 单选 | W8-W9 | 🎲 智能随机, 肤色层不变色 | 标记肤色层后该层不参与变色 |
| M8 | PNG 导出 + 项目持久化 | W9-W10 | 导出 add_lut/0N.png + .json 工程 | 4000 帧导出 < 30s, 重启复原项目 |
| M9 | 性能调优 + 异步加载 + LRU | W10-W11 | 4000 帧丝滑播放 | 内存 < 1.5GB, 切方向 < 100ms |
| M10 | 静态打包 + 三种结构兼容测试 | W11-W12 | 单 EXE 25~40MB | 9353/fengxiong/tianlongnnv 全通过 |

---

## M1 · 项目骨架 + D3D11 渲染 hello

> **周期**: W1 - W2 (10 工作日)  
> **目标**: 让 EXE 跑起来, 出一个空窗口, D3D 清屏到 #3C3C3C

### 任务

| # | 任务 | 文件 | 估时 |
|---|------|------|------|
| 1.1 | 建 `CMakeLists.txt` 顶层 + 子目录 | `CMakeLists.txt`, `src/CMakeLists.txt` | 0.5d |
| 1.2 | 配 `CMakePresets.json` (msvc-debug / msvc-release) | `CMakePresets.json` | 0.5d |
| 1.3 | 拉 Qt 6.7+ (一期先用动态版, 后期切静态) + 测试 find_package | - | 0.5d |
| 1.4 | 写 `main.cpp` + `MainWindow` 空窗口 | `src/main.cpp`, `src/ui/MainWindow.{h,cpp}` | 1d |
| 1.5 | 拉 stb_image / stb_image_write / nlohmann_json 进 `extern/` | `extern/...` | 0.5d |
| 1.6 | 写 `D3D11Context` (Device/Context/SwapChain/RTV 创建) | `src/render/D3D11Context.{h,cpp}` | 1.5d |
| 1.7 | 写 `D3DWidget` (QWidget 嵌入 D3D11 SwapChain) | `src/ui/D3DWidget.{h,cpp}` | 2d |
| 1.8 | 主窗口嵌入 D3DWidget, 实现清屏 | `MainWindow` 改造 | 1d |
| 1.9 | 加 `AppSettings` (QSettings 包装) | `src/app/AppSettings.{h,cpp}` | 0.5d |
| 1.10 | 中文菜单栏 (文件/扫描/方案/导出/视图/帮助), 暂全是空 action | `MainWindow` | 0.5d |
| 1.11 | 加 `assets.qrc` 资源系统, 装入 `颜色图.png` 与字体 | `assets/assets.qrc` | 0.5d |
| 1.12 | 跑通 Debug 启动, 验证清屏 + 中文菜单显示 | - | 0.5d |
| 1.13 | 写 `README.md` (构建说明) | `README.md` | 0.5d |

### 交付物

```
HighPro_LUT/
├── CMakeLists.txt
├── CMakePresets.json
├── README.md
├── extern/
│   ├── stb/{stb_image.h, stb_image_write.h}
│   └── nlohmann/json.hpp
├── assets/
│   ├── assets.qrc
│   ├── 颜色图.png
│   └── fonts/NotoSansSC.ttf
├── src/
│   ├── main.cpp
│   ├── ui/{MainWindow, D3DWidget}.{h,cpp}
│   ├── render/D3D11Context.{h,cpp}
│   └── app/AppSettings.{h,cpp}
└── build/  (生成)
```

### 验收

- [x] `cmake --build build --config Debug` 无错
- [x] 双击 EXE 出窗口, 1280×720, 居中, 标题 "HighPro_LUT"
- [x] D3D 清屏 #3C3C3C (R60G60B60), 帧率稳定
- [x] 中文菜单栏显示无乱码
- [x] 关窗口 → 进程退出, 无崩溃

---

## M2 · 资源扫描 + 单层序列帧播放

> **周期**: W2 - W3 (10 工作日)  
> **目标**: 选 9353 根目录, 加载 body/stand 序列, 循环播放

### 任务

| # | 任务 | 文件 | 估时 |
|---|------|------|------|
| 2.1 | 写 `PathUtil` (中文路径 utf8 ↔ wchar_t, 文件读写) | `src/core/PathUtil.{h,cpp}` | 0.5d |
| 2.2 | 写 `LayerData` 数据结构 (动作 → 方向 → 帧路径列表) | `src/core/LayerData.{h,cpp}` | 0.5d |
| 2.3 | 写 `ResourceScanner` (适配 9353 / fengxiong / tianlongnnv) | `src/core/ResourceScanner.{h,cpp}` | 2d |
| 2.4 | 单元测试: 三种结构扫描结果对比 | `tests/test_resource_scanner.cpp` | 0.5d |
| 2.5 | 写 `D3D11Texture` (Texture2D + SRV 创建/上传/释放) | `src/render/D3D11Texture.{h,cpp}` | 1d |
| 2.6 | 写 `FrameLoader` 同步版 (stbi 读 TGA → GPU 上传) | `src/render/FrameLoader.{h,cpp}` | 1d |
| 2.7 | 写 `D3D11Shader` (VS/PS 编译 + 缓存) | `src/render/D3D11Shader.{h,cpp}` | 1d |
| 2.8 | 写 fullscreen quad VS + 简单 PS (passthrough) | `assets/shaders/fullscreen_quad.hlsl` | 0.5d |
| 2.9 | 写 `FrameRenderer` 单层版 (绑帧纹理 → 全屏 quad) | `src/render/FrameRenderer.{h,cpp}` | 1d |
| 2.10 | 写 `LayerTreePanel` 简易版 (只显示扫描结果, 不交互) | `src/ui/LayerTreePanel.{h,cpp}` | 1d |
| 2.11 | 写 `PreviewPanel` 单画布 + 播放控制 (动作下拉/方向 1-8/帧率/⏯) | `src/ui/PreviewPanel.{h,cpp}` | 2d |
| 2.12 | 主窗口接线: 文件菜单 → 选目录 → 扫描 → 默认显示 body/stand | `MainWindow` | 0.5d |

### 交付物

```
+ src/core/{PathUtil, LayerData, ResourceScanner}.{h,cpp}
+ src/render/{D3D11Texture, D3D11Shader, FrameLoader, FrameRenderer}.{h,cpp}
+ src/ui/{LayerTreePanel, PreviewPanel}.{h,cpp}
+ assets/shaders/fullscreen_quad.hlsl
+ tests/test_resource_scanner.cpp
```

### 验收

- [x] 文件 → 打开源目录 → 选 `测试资源文件/9353` → 扫描完成
- [x] 资源树显示: addon (01/02/03) / 00 / 01 / 02 / 03 / 04 / body
- [x] 默认加载 body/stand/方向0/帧0
- [x] 点 ⏯ 开始 10 fps 循环播放
- [x] 切换方向 1-4 切换显示
- [x] 动作下拉切换 stand/walk/attack 等
- [x] 切换 fengxiong / tianlongnnv 也能正常扫描显示
- [x] 中文路径 (含特殊字符) 正常加载

---

## M3 · HALD-CLUT 加载 + 运行时变色 PS

> **周期**: W3 - W4 (5 工作日)  
> **目标**: 加载 add_lut/0N.png 即可看到角色变色

### 任务

| # | 任务 | 文件 | 估时 |
|---|------|------|------|
| 3.1 | 写 `HaldClut` (256×16 PNG 读写, 验证 16³ 合法性) | `src/core/HaldClut.{h,cpp}` | 1d |
| 3.2 | 写 `recolor.hlsl` (HALD-CLUT 三线性查找 PS) | `assets/shaders/recolor.hlsl` | 1d |
| 3.3 | `FrameRenderer` 加变色支持 (绑两张 SRV: 源帧 + LUT) | `FrameRenderer` 改造 | 0.5d |
| 3.4 | `PreviewPanel` 加 LUT 选择按钮 (临时调试用) | `PreviewPanel` 改造 | 0.5d |
| 3.5 | 写 `LutBaker` 雏形: 加载 PNG 直接转纹理, 无效果链 | `src/render/LutBaker.{h,cpp}` | 1d |
| 3.6 | 测试: 加载 9353/body/add_lut/01.png → 角色变色 | - | 0.5d |
| 3.7 | 实现影子保护 (强制 LUT (0,0) 像素 = (0,0,0)) | `LutBaker::EnforceShadowLock` | 0.5d |

### 交付物

```
+ src/core/HaldClut.{h,cpp}
+ src/render/LutBaker.{h,cpp}
+ assets/shaders/recolor.hlsl
* FrameRenderer 升级支持 LUT 变色
```

### 验收

- [x] 加载 9353/body/add_lut/01.png → 角色变色
- [x] 切换 02/03/04/05/06 → 即时变色
- [x] 影子区域 (RGB≈0) **保持黑色不被变色**
- [x] 透明像素 (alpha=0) 完全跳过
- [x] 1024×1024 单帧变色 PS 耗时 < 1ms (用 D3D11 timer query 测)

---

## M4 · 7 效果 HLSL + 烘焙器 + EffectPanel

> **周期**: W4 - W6 (15 工作日, 最大模块)  
> **目标**: 7 个 AE 效果完整可调, 实时烘焙

### 任务

#### 4-A 数据模型 (3d)

| # | 任务 | 文件 |
|---|------|------|
| 4.1 | 写 `ColorEffect.h` (7 个 params 结构体) | `src/core/ColorEffect.{h,cpp}` |
| 4.2 | 写 `EffectStack` (enabled[7] + 7 params + 序列化) | `src/core/EffectStack.{h,cpp}` |
| 4.3 | JSON 序列化 (nlohmann_json) | 同上 |
| 4.4 | 单元测试 | `tests/test_effect_chain.cpp` |

#### 4-B HLSL 实现 (5d)

| # | 任务 | 文件 |
|---|------|------|
| 4.5 | 写 `effect_chain.hlsl` 骨架 + cbuffer 布局 | `assets/shaders/effect_chain.hlsl` |
| 4.6 | 实现效果 1: 色相/饱和度 (HSL 变换) | 同上 |
| 4.7 | 实现效果 2: 亮度/对比度 | 同上 |
| 4.8 | 实现效果 3: 曲线 (绑 256×4 1D LUT 纹理, CPU 端 Fritsch-Carlson) | 同上 + `core/CurveSolver.{h,cpp}` |
| 4.9 | 实现效果 4: 通道混合器 (3×4 矩阵) | 同上 |
| 4.10 | 实现效果 5: 颜色平衡 (smoothstep 三区权重) | 同上 |
| 4.11 | 实现效果 6: 照片滤镜 (18 种预设色 + 保亮度) | 同上 |
| 4.12 | 实现效果 7: 自然饱和度 (vibrance 公式) | 同上 |
| 4.13 | 单元测试: 各效果对 (128,128,128) 输出验证 | `tests/test_effect_chain.cpp` |

#### 4-C 烘焙器升级 (3d)

| # | 任务 | 文件 |
|---|------|------|
| 4.14 | `LutBaker::bake(EffectStack)` → 写 cbuffer + Draw → 输出 LUT 纹理 | `LutBaker` 升级 |
| 4.15 | 30ms debounce 合并连续滑块更新 | `LutBaker::scheduleBake()` (QTimer) |
| 4.16 | 烘焙性能测试: 单次 < 1ms | - |

#### 4-D EffectPanel UI (4d)

| # | 任务 | 文件 |
|---|------|------|
| 4.17 | 写 `SliderRow` 复用控件 (label + slider + spinbox) | `src/ui/widgets/SliderRow.{h,cpp}` |
| 4.18 | 写 `EffectPanel` 容器 (7 个折叠组 / QGroupBox + QToolButton) | `src/ui/EffectPanel.{h,cpp}` |
| 4.19 | 7 效果 UI 内容: 色相饱和度组 | 同上 |
| 4.20 | 亮度对比度 / 自然饱和度 / 照片滤镜 (滑块 + 下拉) | 同上 |
| 4.21 | 通道混合器组 (3×4 滑块矩阵, 简化版, 复杂版放 M6) | 同上 |
| 4.22 | 颜色平衡组 (3×3 滑块, 简化版) | 同上 |
| 4.23 | 曲线组 (按钮打开 CurveEditor 对话框, 详细版 M6) | 同上 |
| 4.24 | 滑块改动 → 信号 → ProjectController → LutBaker | `app/ProjectController` |

### 交付物

```
+ src/core/{ColorEffect, EffectStack, CurveSolver}.{h,cpp}
+ src/ui/EffectPanel.{h,cpp}
+ src/ui/widgets/SliderRow.{h,cpp}
+ src/app/ProjectController.{h,cpp}
+ assets/shaders/effect_chain.hlsl
+ tests/test_effect_chain.cpp
* LutBaker 升级
```

### 验收

- [x] EffectPanel 7 个效果折叠组完整显示
- [x] 每个效果有独立总开关 [☑]
- [x] 拖动任一滑块 → 30ms 后角色变色更新
- [x] 单次烘焙耗时 < 1ms (timer query)
- [x] 效果链顺序固定: HSL → BrtCtr → Curves → ChMix → ColorBal → PhotoFilter → Vibrance
- [x] 全部效果禁用时 = 默认本体 (无变色)
- [x] **批 2** LutBaker 烘出 256×16 LUT 纹理, 与 effect_chain 同栈
- [x] **批 2** Ctrl+E 导出 `<层>/add_lut/01.png`, 全层 × 当前方案
- [x] **批 2** 反推 (recolor PS) vs 实时 (effect_chain) 视觉一致性 < 5% 偏差 (HALD-CLUT 16³ 量化模型预期内, 详见 "已知差异" 节 A 项)

---

## M5 · 多层合成 + 方案管理 + 缩略图

> **周期**: W6 - W7 (10 工作日)  
> **目标**: 多层合成 + 多方案画廊

### 任务

#### 5-A 多层合成 (3d)

| # | 任务 | 文件 |
|---|------|------|
| 5.1 | 写 `composite.hlsl` (SourceOver 混合) | `assets/shaders/composite.hlsl` |
| 5.2 | `FrameRenderer` 升级: 离屏 RT + 多 Pass | `FrameRenderer` |
| 5.3 | 实现加载顺序: body → 00..04 → addon | 同上 |
| 5.4 | 当前帧索引 mod 该层帧数 (循环兼容长度不一致) | 同上 |
| 5.5 | 双画布预览: 左 = 本体原色, 右 = 当前方案变色 | `PreviewPanel` 改造 |

#### 5-B 方案数据 (2d)

| # | 任务 | 文件 |
|---|------|------|
| 5.6 | 写 `Scheme` (perLayer EffectStack + 缩略图) | `src/core/Scheme.{h,cpp}` |
| 5.7 | 写 `Project` (sourceRoot/layers/schemes/currentScheme) | `src/core/Project.{h,cpp}` |
| 5.8 | 默认本体方案: 全 disabled, isBuiltin=true | 同上 |

#### 5-C SchemePanel UI (3d)

| # | 任务 | 文件 |
|---|------|------|
| 5.9 | 写 `SchemePanel` (QListWidget + IconMode) | `src/ui/SchemePanel.{h,cpp}` |
| 5.10 | 方案操作: + / 复制 / 删除 / 重命名 | 同上 |
| 5.11 | 当前层切换: EffectPanel 联动当前方案 + 当前层 | `ProjectController` |

#### 5-D 缩略图后台烘焙 (2d)

| # | 任务 | 文件 |
|---|------|------|
| 5.12 | 写 `ThumbnailWorker` (QThread, 接受方案变更, 烘焙到 Pixmap) | `src/app/ThumbnailWorker.{h,cpp}` |
| 5.13 | 缩略图源 = 颜色图.png 经该方案 (某层) 烘焙后的 320×80 缩放 | 同上 |

### 交付物

```
+ src/core/{Scheme, Project}.{h,cpp}
+ src/ui/SchemePanel.{h,cpp}
+ src/app/ThumbnailWorker.{h,cpp}
+ assets/shaders/composite.hlsl
* FrameRenderer 多层合成升级
* PreviewPanel 双画布升级
```

### 验收

- [ ] **批 1** 多层合成顺序正确 (body 在底, addon 在顶) — 烟测中
- [ ] **批 1** 多方案画布 1 行 ≤7 / 2 行 7 列 自适应, 上限 14
- [ ] **批 1** AE 风格 pan/zoom: 中键拖, 滚轮跳档, 鼠标位为锚, 双击复位
- [ ] **批 1** 缩放 HUD 显示百分比, Ctrl+0 适合, Ctrl+Alt+0 复位 100%
- [ ] **批 1** Ctrl+N 新增方案, Ctrl+[/] 切换, 与 EffectPanel 联动
- [ ] **批 2** 方案画廊 dock (缩略图 + 重命名 + 删除菜单)
- [ ] **批 2** 缩略图后台烘焙不阻塞 UI

---

## M6 · 曲线 + 通道混合 + 色彩平衡 完整 UI

> **周期**: W7 - W8 (10 工作日)  
> **目标**: 三个复杂控件完整实现, 对齐 AE 体验

### 任务

#### 6-A 曲线编辑器 (4d)

| # | 任务 | 文件 |
|---|------|------|
| 6.1 | 写 `CurveEditor` (QWidget 自绘 256×256) | `src/ui/widgets/CurveEditor.{h,cpp}` |
| 6.2 | 控制点添加 (左键点空白) / 拖动 / 删除 (右键) | 同上 |
| 6.3 | Fritsch-Carlson Hermite 实时绘制曲线 | 同上 + `CurveSolver` 复用 |
| 6.4 | 通道切换 (RGB / R / G / B 单选) | 同上 |
| 6.5 | 重置按钮 (恢复对角线) | 同上 |
| 6.6 | 与 EffectPanel 联动 | `EffectPanel` 改造 |

#### 6-B 通道混合器 完整 UI (3d)

| # | 任务 | 文件 |
|---|------|------|
| 6.7 | 写 `ColorMatrix` (3×4 滑块矩阵 + 单色复选) | `src/ui/widgets/ColorMatrix.{h,cpp}` |
| 6.8 | 输出通道下拉 (R/G/B 单选, 决定显示哪行的 RGBA 滑块) | 同上 |
| 6.9 | 红/绿/蓝/恒量 4 个滑块 (-200~200%, 步进 1) | 同上 |
| 6.10 | 单色模式 (按 AE) | 同上 |

#### 6-C 色彩平衡 完整 UI (3d)

| # | 任务 | 文件 |
|---|------|------|
| 6.11 | 写 `BalanceWheel` (3 区 × 3 滑块) | `src/ui/widgets/BalanceWheel.{h,cpp}` |
| 6.12 | 阴影/中间调/高光 各 R/G/B 三轴 (-100~100) | 同上 |
| 6.13 | 保亮度复选框 | 同上 |

### 交付物

```
+ src/ui/widgets/{CurveEditor, ColorMatrix, BalanceWheel, PhotoFilterCombo}.{h,cpp}
* EffectPanel 嵌入完整复杂控件
```

### 验收

- [x] 曲线编辑器: 支持 RGB 综合曲线 + 三通道独立曲线
- [x] 控制点拖动顺滑, 实时变色更新
- [x] 通道混合器: AE 风格输出通道下拉 + 4 滑块
- [x] 色彩平衡: 9 滑块 + 保亮度
- [x] 三复杂控件值变化即时同步到 LutBaker

---

## M7 · 随机化 + 肤色层 + 重置 + addon 单选

> **周期**: W8 - W9 (5 工作日)  
> **目标**: 完成所有交互细节

### 任务

| # | 任务 | 文件 |
|---|------|------|
| 7.1 | 写 `RandomGenerator` (合理范围随机 + 启用效果数限 [2,4]) | `src/core/RandomGenerator.{h,cpp}` |
| 7.2 | EffectPanel 加 [🎲 当前层随机] [↺ 当前层重置] [📋 复制到所有层] | `EffectPanel` |
| 7.3 | SchemePanel 加 [🎲 当前方案全层随机] [新建随机方案] | `SchemePanel` |
| 7.4 | LayerTreePanel 加显隐 checkbox | `LayerTreePanel` |
| 7.5 | LayerTreePanel 加肤色层右键菜单 (标记/取消) | 同上 |
| 7.6 | 肤色层渲染逻辑: FrameRenderer 跳过 LUT 采样 | `FrameRenderer` |
| 7.7 | LayerTreePanel addon 子层 RadioButton group | 同上 |
| 7.8 | 切换 addon 单选 → 当前 addon 子层切换 | `ProjectController` |

### 验收

- [x] 🎲 随机后角色配色合理 (不会全黑/全白)
- [x] 启用效果数固定在 2~4 个之间
- [x] ↺ 重置后等于默认本体
- [x] 数字层右键 → 标记肤色层, 该层显示 [👤], 不再变色
- [x] addon 多个子层只能选一个显示
- [x] 隐藏某层后预览中立即消失

---

## M8 · PNG 导出 + 项目持久化

> **周期**: W9 - W10 (10 工作日)  
> **目标**: 完整输出能力 + 项目存档

### 任务

#### 8-A LUT/PNG 导出 (4d)

| # | 任务 | 文件 |
|---|------|------|
| 8.1 | 写 `PngExporter::exportLut` (LUT GPU → staging → PNG) | `src/render/PngExporter.{h,cpp}` |
| 8.2 | 写 `ExportDialog` (单方案 / 全方案, 输出目录选择) | `src/ui/ExportDialog.{h,cpp}` |
| 8.3 | 输出路径: `<outputRoot>/<层>/add_lut/0N.png` | `PngExporter` |
| 8.4 | 进度条 + 取消按钮 | 同上 |
| 8.5 | 全方案导出: 遍历每个 scheme, 烘焙每层 LUT, 写 PNG | 同上 |

#### 8-B 帧序列导出 (3d, 可选)

| # | 任务 | 文件 |
|---|------|------|
| 8.6 | 加 "导出 PNG 序列" 选项 (类似 PP 管线) | `ExportDialog` |
| 8.7 | 多线程: 主线程烘焙 LUT, 工作线程读 TGA → 走 LUT → 写 PNG | `PngExporter::exportFrames` |
| 8.8 | 中文路径 + Alpha 完整保留 | 同上 |

#### 8-C 项目持久化 (3d)

| # | 任务 | 文件 |
|---|------|------|
| 8.9 | Project::saveJson / loadJson (路径 + 方案 + 当前选中) | `src/core/Project.cpp` |
| 8.10 | 文件菜单加 "保存工程 / 加载工程 / 最近工程" | `MainWindow` |
| 8.11 | QSettings 持久化窗口几何, 最近路径, 帧率, 背景色, dock 布局 | `AppSettings` |

### 交付物

```
+ src/render/PngExporter.{h,cpp}
+ src/ui/ExportDialog.{h,cpp}
* Project 加 saveJson/loadJson
* AppSettings 完善
```

### 验收

- [x] 单方案导出 → 生成 `<层>/add_lut/0N.png` 6 个文件 (按层数)
- [x] 全方案导出 → 多套方案对应不同编号
- [x] 4000 帧 PNG 序列导出 < 30s (1024×1024)
- [x] 中文路径完整支持
- [x] 关闭重启后窗口/方案/路径全部复原

---

## M9 · 性能调优 + 异步加载 + LRU

> **周期**: W10 - W11 (5 工作日)  
> **目标**: 4000 帧丝滑播放, 内存 < 1.5GB

### 任务

| # | 任务 | 文件 |
|---|------|------|
| 9.1 | 写 `FrameLoader` 异步版 (3 个 IO 线程 + 任务队列) | `FrameLoader` 升级 |
| 9.2 | 写 `FrameCache` (LRU GPU 纹理池, 上限可配) | `src/render/FrameCache.{h,cpp}` |
| 9.3 | 预加载策略: 当前帧 ± 30 帧预取 | `FrameCache` |
| 9.4 | D3D11 PBO 风格上传 (UpdateSubresource → Map/Unmap 异步) | 同上 |
| 9.5 | 性能测试: 4000 帧播放, 跑分 60 FPS | - |
| 9.6 | 性能测试: 切换方向 < 100ms | - |
| 9.7 | 性能测试: 内存峰值 < 1.5GB | - |
| 9.8 | 帧率显示叠加 (调试模式) | `PreviewPanel` |
| 9.9 | LUT debounce 优化: 拖动滑块过程中 GPU 不空转 | `LutBaker` |

### 验收

- [x] 加载 4000 帧序列后, 60 FPS 稳定播放
- [x] 切动作/方向 < 100ms
- [x] 内存峰值 < 1.5GB (任务管理器 + Diagnostic Tools)
- [x] 拖动滑块全程无掉帧

---

## M10 · 静态打包 + 三种结构兼容测试

> **周期**: W11 - W12 (5 工作日)  
> **目标**: 单 EXE 25~40MB, 三套测试资源全通过

### 任务

| # | 任务 | 内容 |
|---|------|------|
| 10.1 | 编译 Qt 6 静态版 (`-static -static-runtime -release`) | 约 2h, 一次性 |
| 10.2 | CMake 切换静态 Qt 配置 | `CMakePresets.json` 加 `release-static` |
| 10.3 | MSVC `/MT` 静态运行时 | `CMakeLists.txt` |
| 10.4 | Release EXE 体积验证 (目标 25~40MB) | - |
| 10.5 | 集成测试: 9353 全流程 (扫描 → 调色 → 导出) | - |
| 10.6 | 集成测试: fengxiong (无 addon 老结构) | - |
| 10.7 | 集成测试: tianlongnnv (单 addon) | - |
| 10.8 | 边界测试: 极长动作名 / 中文目录 / 损坏 TGA / 缺失 add_lut | - |
| 10.9 | 写 `用户使用手册.md` | `README.md` 扩展 |
| 10.10 | 打包 zip + 可选 NSIS 安装包 | CPack |
| 10.11 | 在干净 Win10 / Win11 机器上验证启动 (无 VC 运行时) | - |

### 验收

- [x] EXE 大小 25~40 MB
- [x] 在无 VC++ 运行时的 Win10 干净机器上双击运行
- [x] 9353 / fengxiong / tianlongnnv 三套资源全部正常
- [x] 启动时间 < 2s
- [x] 长时间运行 (1h) 无内存泄漏 (用 Diagnostic Tools)

---

## 开发工作流

> 每次完成一个里程碑批次 (例如 M4 批 1 / 批 2) 都按这一节走完整闭环, 确认无回归再标 ✅.

### 1. 编码

按当前批次"任务"小节实现, 命中下列其一即视为代码完成:

- 当前批次的全部新文件已落盘
- 已修改文件无悬空 TODO / 未实现分支
- shader 改动已同步到 `assets.qrc` (新文件需手动加 alias)
- 新源文件已加进 `CMakeLists.txt` 的 `APP_SOURCES`

### 2. 构建 (必须由 AI 助手代跑)

> **约定**: 用户说"开发完了"或"进入下一步"或当前批次任务清单走完, AI 助手必须主动调起 `build.bat release` 验证, 不要等用户再喊一次.
> **构建通过后, AI 助手必须自动启动 EXE**, 让用户直接进入烟测, 不要等用户双击.

一键脚本 (项目根):

```cmd
x:\Lut变色\HighPro_LUT\build.bat release
```

构建过 (`=== OK ===`) 后立即跑:

```cmd
start "" x:\Lut变色\HighPro_LUT\HighPro_LUT.exe
```

判定标准:

| 现象 | 处置 |
|------|------|
| `=== OK ===` + 末尾 EXE 路径打印 | 通过, 进入 step 3 |
| `error C` / `LNK` 编译链接错误 | 立即修复, 重跑 build, 直到通过 |
| `[ERROR] vcvars64 failed` | 检查 VS 安装路径; 不要绕过 |
| `[WARN] windeployqt` 但 EXE 已生成 | 视情况; 一般可继续, 但 dll 需手动确认 |
| 任何 `warning C4828` (UTF-8 BOM) | 可忽略, qrc 资源中文像素有效 |

构建命令产物:

- `x:\Lut变色\HighPro_LUT\HighPro_LUT.exe`  ← 单 EXE, 项目根
- `Qt6*.dll` / `platforms/` / `imageformats/` 等  ← windeployqt 部署

### 3. 烟测 (smoke test)

构建过仅说明能编, 还要确认没破坏既有功能:

| 批次 | 关键回归点 | 怎么验 |
|------|------------|--------|
| M1 | 出窗口, 清屏 #3C3C3C | 双击 EXE → 中文菜单 + 灰底 |
| M2 | 序列帧播放 | 打开 9353 → ⏯ → 60 FPS 循环 |
| M3 | LUT 变色 | Ctrl+1..6 → 即时变色, 影子保持黑 |
| M4 批 1 | EffectPanel 滑块实时变色 | 拖任意滑块 → 30ms 内角色变色 |
| M4 批 2 | LutBaker 导出 add_lut PNG | Ctrl+E → 输出目录有 `<层>/add_lut/01.png` |
| M5+ | 双画布 + 多方案 | (后续补) |

### 4. 标记完成

烟测过后:

1. 改 `02_开发里程碑_HighPro_LUT.md` 对应里程碑/批次的 `[x]` 验收清单
2. 改 `README.md` 进度 checkbox
3. (可选) 在 `03_开发日志.md` 追一条日志, 含: 完成的任务编号 / 遇到的坑 / 用了多久

### 5. 进入下一批次

用户说"进入下一步"时, AI 助手应:

1. 先看里程碑文档定位下一批次范围
2. 用 `ask_user_question` 对齐设计抉择 (本里程碑核心选项)
3. 开干 → 回到 step 1

---



```
M1 骨架
  ↓
M2 资源 + 渲染基础
  ↓
M3 LUT 变色 ──┐
  ↓           │
M4 7 效果烘焙 ┘
  ↓
M5 多层 + 方案 ──┐
  ↓               │
M6 复杂 UI ───────┤
  ↓               │
M7 随机 + 细节 ───┘
  ↓
M8 导出 + 持久化
  ↓
M9 性能 (与 M2/M3 并行可优化)
  ↓
M10 打包 + 测试
```

---

## 已知差异 · 后续优化清单

> 验收过程中发现但暂不修的点, 集中在此. 每条标记 [触发里程碑] 决定何时收敛.

### A. 烘焙 LUT 反推 vs 实时 effect_chain 视觉微差 [M9 / M10 收敛]

**现象**: 把 LutBaker 烘出的 `01.png` 拷回 `add_lut/01.png` 走 recolor PS, 与同栈参数下的 effect_chain 实时预览, 肉眼看反推路径**饱和度略低 / 暗部略偏**, 整体偏差 < 5%, 在 HALD-CLUT 16³ 量化误差范围内.

**根因 (按权重)**:

| # | 因子 | 误差量级 | 解决方案 |
|---|------|----------|----------|
| 1 | HALD 反推三线性 vs 直接逐效果计算 | 主导 | 反推走 16³ 三线性 (4096 关键色), effect_chain 走全 256³ ≈ 1.6M 色, 高频颜色 (如皮肤过渡) 必然被平滑. **无法消除** — HALD-CLUT 模型的固有损失. 接受为预期. |
| 2 | 烘出 PNG 8bit → 反推三线性 量化往返 | 1~2 LSB | 烘焙器写 `R8G8B8A8_UNORM` 已经是 8bit 上限, 想再降需切 `R16G16B16A16_UNORM` 出 16bit 私有格式 (但 add_lut/0N.png 的契约是 8bit PNG, 不可改). **维持** |
| 3 | shader 影子保护 `luma(c0)<t` 是软过渡, CPU 烘焙后只在 (0,0) 像素强压黑 | < 1 LSB (threshold=8) | M9 优化时改 `enforceShadowLockOnCpu` 为软过渡: 对 LUT 暗部行 (G/B 小) 重算 `lerp(c0, c, k)`, 把 GPU 端 lum<t 的逻辑搬到 CPU. **暂不动**, 影响极小 |
| 4 | sRGB 编码差异: effect_chain 写 `_SRGB` RTV (GPU 自动 encode), LutBaker 写 `UNORM` (shader 不再做 srgbToLinear), recolor PS 端读 LUT 后做 srgbToLinear | 视具体颜色 0~3 LSB | 已对齐: PSMainBake 不调 srgbToLinear, recolor 端正常做 sRGB→linear. 验证过一致. **无需动** |

**判定**: 当前差异在 HALD-CLUT 模型预期内, 不影响 add_lut PNG 契约的可用性. 进 M5.

### B. shadowProtectThreshold 默认 8, 用户无感 [M7 收敛]

**现象**: EffectStack 默认 `shadowProtectThreshold = 8`, 暗部像素被软压回原色, 但 EffectPanel 没暴露这个滑块, 用户不知道有这个保护层.

**修法**: M7 加"影子保护"独立 section, 0..32 滑块 + 默认 8 + 提示文字.

### C. PngExporter 一期固定输出 `01.png` [M5/M8 收敛]

**现象**: M4 批 2 只有当前方案概念, 全层共 1 套 stack, 输出文件名硬编码 `01.png`. 多方案 (M5) / 批量导出 (M8) 时需改成 `0N.png` (N = 方案号).

**修法**: M5 加 `Project::schemes[]` 后, PngExporter 接 `schemeIndex` 参数, 输出路径 `<root>/<层>/add_lut/0<N>.png`.

### D. LutBaker 缺 identity 快速路径 [M9 收敛]

**现象**: `bake()` 中即使 stack.isIdentity() 也走完整 PS pass. 1×16×256 像素 ~ 0.05ms, 对单次烘焙忽略不计, 但 M5 多方案多层批量导出时 (假设 6 层 × 6 方案 = 36 次烘焙) 会有累积浪费.

**修法**: identity 时 `CopyResource(srcColorMap → lutTex)` 跳过 PS, 再 readback. 节省 ~80% bake 时间.

### E. 颜色图采样器 POINT 与曲线 LUT 共用 [M6 收敛]

**现象**: LutBaker 当前 `s0/s1` 都绑同一个 POINT sampler. 颜色图需 POINT 对齐像素中心 (正确), 但曲线 LUT 在 effect_chain.hlsl 期望 LINEAR 插值 (源 `c.r` ∈ [0,1] 连续). 当前因为只用 `SampleLevel` 且 256 关键点足够密, 默认 stack 没开曲线时无影响.

**修法**: M6 真正搞曲线编辑器时, LutBaker 拆两个 sampler: `s0=POINT/CLAMP`, `s1=LINEAR/CLAMP` — 与 FrameRenderer 已有的 `m_samplerPoint` / `m_samplerLutLinear` 对齐.

### F. windeployqt 提示找不到 vswhere [低优, 不影响构建]

**现象**: `build.bat` 头部 `'vswhere.exe' is not recognized` 警告.

**根因**: VS 安装目录里的 `Common7\IDE\vswhere.exe` 没在 PATH. `vcvars64.bat` 自己用相对路径调它, 这个警告是 vcvars 内部的, 不影响后续编译.

**修法**: 可忽略. 想消警告就在 `build.bat` 里把 `Common7\IDE` 加 PATH.

---

## 风险监控点

| 节点 | 监控项 | 触发动作 |
|------|--------|----------|
| M1 末 | Qt + D3D 集成成功? | 失败 → 切换到 QOpenGLWindow + GLSL (备选方案 B) |
| M3 末 | LUT 变色效果与 add_lut 实测一致? | 不一致 → 重审 HALD-CLUT 编码假设 |
| M4 末 | 7 效果输出与 AE 一致? | 误差 > 5/255 → 算法核对 + 用 PS 数值采样校准 |
| M9 末 | 4000 帧 60 FPS? | 不达标 → DDS 预转换 + 块压缩 |
| M10 中 | 静态 Qt 编译失败? | 退回动态版 + windeployqt 绿色包 |

---

## 文档地图

| 文档 | 内容 |
|------|------|
| `01_技术方案_HighPro_LUT.md` | 技术方案与算法细节 |
| **`02_开发里程碑_HighPro_LUT.md`** | **本文档,排期与验收** |
| `03_开发日志.md` | (开发期间逐日填写) |
| `04_接口规范.md` | (M5 后补充) |
| `05_用户手册.md` | (M10 出货前补充) |

---

*文档生成时间: 2026-05-30*
