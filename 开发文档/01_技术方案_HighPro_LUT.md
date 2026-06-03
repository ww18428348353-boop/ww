# HighPro_LUT — 高性能序列帧角色换色工具 · 技术方案

> 版本: v1.0  
> 立项时间: 2026-05-30  
> 项目根目录: `x:\Lut变色\HighPro_LUT`  
> 预计周期: 11~12 周

---

## 0. 一句话定义

**导入角色 TGA 序列帧 → 多层合成实时双画布预览 → 7 个 AE 风格颜色效果对每层独立调节 → 烘焙生成多套换色方案 (add_lut/0N.png) → 批量输出 PNG 颜色贴图。**

核心特点:

- **C++17 / Qt 6 / Direct3D 11 / CMake / 全静态链接 → 单 EXE 无 dll 依赖**
- **GPU 烘焙 + GPU 变色**: LUT 烘焙 < 1ms, 单帧变色 < 1ms, 滑块全程 60FPS 顺滑
- **HALD-CLUT 16³ 颜色管线**: 影子像素天然免疫变色
- **每层独立 7 效果**, 每个方案独立保存 7 效果参数, 多方案画廊
- **4000+ 帧** 异步加载 + GPU 纹理 LRU 池, 内存可控

---

## 1. 需求映射

| # | 用户需求 | 实现机制 |
|---|---------|---------|
| 1 | 阅读 `开发文档/` 与 `LUT变色_开发详情.md` 等规则 | 已完成, 本方案为综合产物 |
| 2 | C++ / Dear ImGui + OpenGL 推荐方案 | 调整为 **C++ / Qt6 / Direct3D 11**(用户最终决定): 性能稳, UI 控件丰富, 单 EXE |
| 3 | 测试资源 `测试资源文件/{9353, fengxiong, tianlongnnv}/` | `ResourceScanner` 自动适配三种结构 |
| 4 | 扫描 `addon / 00 / 01 / 02 / 03 / 04 / body`, 后续 05/06 | 数字层用正则 `^\d{2}$` 匹配,前向兼容 |
| 4 补 | 加载顺序: addon (顶) → 00..04 (中) → body (底) | `FrameRenderer` 自下而上 SourceOver 合成 |
| 4 补 | addon 单选(只显一层), 其他层独立显隐 | `LayerTreePanel` radio + checkbox |
| 4 补 | 数字层有"肤色层", 不参与变色, 用户可指定 | 该层 `bypass_recolor=true`, 渲染时直接走原色 |
| 5 | `颜色图.png` 为默认颜色图 | 启动加载 `assets/颜色图.png` (256×16, HALD-CLUT 16³) |
| 6 | `add_lut/` 存放方案颜色贴图, 没则用默认 | 优先扫层下 `add_lut/0N.png`, 缺失时用默认 LUT |
| 7 | 每层根目录子文件夹 = 动作 (guard/stand/walk...) | `LayerData::scanActions()` |
| 8 | 帧名 `^(\d)(\d{3})\.tga$` → 方向 + 帧号 | 实证 9353/stand 匹配 |
| 9 | 默认 10 fps + 背景 R60G60B60 | `AppSettings` 默认值 |
| 10 | 7 个 AE 效果: 色相饱和度 / 亮度对比度 / 曲线 / 通道混合器 / 颜色平衡 / 照片滤镜 / 自然饱和度 | `effect_chain.hlsl` 一个 PS 内串联 |
| 11 | 7 效果可独立开关, 对每层都生效 | `EffectStack` per-layer + `enabled[7]` 数组 |
| 12 | 🎲 合理范围随机 + 重置 | `RandomRange` 美观区间, 启用效果数限 [2, 4] |
| 13 | 影子部分不受变色影响 | HALD-CLUT (0,0,0) 角点 + 烘焙器强制锁定 |
| 14 | 默认本体方案 1, 新增 2~10+ 方案 | `Project::schemes` 动态数组 + 画廊 |
| 15 | 输出 PNG 颜色贴图 | `PngExporter` 烘焙 256×16 PNG 写到 `add_lut/0N.png` |

---

## 2. 变色管线核心机制 (实证结论)

### 2.1 颜色图.png 编码 — HALD-CLUT 16×16×16

通过逐像素采样 `颜色图.png` 验证:

```
像素位置 (x, y), x∈[0,255], y∈[0,15]:
    R_in = (x mod 16) * 17       (16 个 R 切片横向铺开)
    G_in = y * 17                  (Y 轴 16 级 G)
    B_in = (x / 16) * 17           (16 个 B 切片)

→ 总计 16 × 16 × 16 = 4096 个唯一颜色
```

这是经典的 **HALD-CLUT (Hardware-Accelerated Look-up Display Color LUT)** 烘焙图格式 (FFmpeg/GIMP/PS 兼容)。

### 2.2 add_lut/0N.png 性质

通过逐像素采样 9353/body/add_lut/01~06.png:

| 方案 | (0,0) 角点 | 含义 |
|------|-----------|------|
| 01 | (0,0,0) | 影子保留 |
| 02 | (0,0,0) | 影子保留 |
| 03 | (0,0,0) | 影子保留 |
| 04 | (0,0,0) | 影子保留 |
| 05 | (15,15,13) | 微变 (亮度提升) |
| 06 | (0,0,0) | 影子保留 (全去饱和方案) |

**结论**:
- `add_lut/0N.png` = 默认颜色图经过 AE 效果链跑一遍后的输出
- (0,0,0) 像素几乎不变 → **影子免疫变色** 的根本原因
- 06.png 全部输出灰阶 → 自然饱和度 = -100 或 HSL/S = 0 的方案

### 2.3 运行时变色流程

```
源 TGA 像素 (R, G, B, A)
    ↓
α < 0.001 → 直接输出 (透明跳过)
    ↓
对 add_lut 做 HALD-CLUT 三线性查找:
    r4 = R * 15,  g4 = G * 15,  b4 = B * 15
    取 b 切片 b0 = floor(b4),  b1 = b0 + 1,  bf = frac(b4)
    UV0 = ( (b0 * 16 + r4) / 256 ,  g4 / 16 )
    UV1 = ( (b1 * 16 + r4) / 256 ,  g4 / 16 )
    c0 = sample(AddLut, UV0)  // 双线性, 自动 R/G 插值
    c1 = sample(AddLut, UV1)
    out = lerp(c0, c1, bf)
    ↓
输出 (out.rgb, src.a)
```

注: 实际查找还可对 R 维度也做手动插值,4096 色 → 16M 色平滑过渡。

### 2.4 烘焙流程 (滑块 → 新 add_lut)

```
启动:  加载 颜色图.png → 创建 256×16 RTV (DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
    ↓
用户改任一参数:
    ↓ 30 ms debounce 合并连续操作
    ↓
绑定 effect_chain PS, 设置 7 效果参数 cbuffer
全屏四边形 Draw → 输出新 LUT 纹理 (在 GPU 上)
    ↓
当前帧渲染时, 该 LUT 直接当 add_lut 使用 → 立即可见效果
    ↓
导出方案时: CopyResource 到 staging, Map 读回, stb_image_write 出 PNG
```

---

## 3. 7 个 AE 效果算法

### 3.1 效果总览

| # | 效果 | 主要参数 | 默认值 | 随机区间 |
|---|------|---------|--------|---------|
| 1 | 色相/饱和度 | hue (-180~180), sat (-100~100), light (-100~100) | 0,0,0 | hue ∈ [-180,180], sat/light ∈ [-50,50] |
| 2 | 亮度/对比度 | brightness (-100~100), contrast (-100~100) | 0,0 | brt ∈ [-30,30], ctr ∈ [-40,40] |
| 3 | 曲线 | RGB 主曲线 + R/G/B 三独立曲线, 每条 2~6 控制点 | 直线 | 1~3 控制点小幅扰动 |
| 4 | 通道混合器 | 3×4 矩阵 (R'/G'/B' × R/G/B/Const), 每个 -200~200% | 单位阵 | 主对角 [70,130]%, 副对角 [-30,30]%, 恒量 0 |
| 5 | 颜色平衡 | 阴影/中间调/高光 各 R/G/B 三轴 (-100~100) | 全 0 | 每分量 [-50, 50], 保亮度 ON |
| 6 | 照片滤镜 | 滤镜色 (18 种预设) + 浓度 (0~100%) + 保亮度 | 暖色 85, 25%, 保亮度 | 预设随机抽 + 浓度 [20,60]% |
| 7 | 自然饱和度 | vibrance (-100~100), saturation (-100~100) | 0,0 | vib [-50,80], sat [-30,50] |

### 3.2 算法细节 (对应 HLSL 实现)

#### 1) 色相/饱和度 (HSL 模型, 对齐 AE)

```
RGB → HSL
H = (H + hue/360) mod 1
S = clamp(S * (1 + sat/100), 0, 1)
L = clamp(L * (1 + light/100), 0, 1)
HSL → RGB
```

#### 2) 亮度/对比度 (传统模式, 对齐 AE 经典)

```
c = (c - 0.5) * (1 + contrast/100) + 0.5 + brightness/255
```

#### 3) 曲线 (Fritsch-Carlson 单调三次 Hermite, 沿用 py)

CPU 端预计算 256-级 LUT, 上传为 1D 纹理 (R8 / 256×4 if RGB+composite)。

#### 4) 通道混合器 (3×4 仿射矩阵)

```
R' = mr.r*R + mr.g*G + mr.b*B + mr.const
G' = mg.r*R + mg.g*G + mg.b*B + mg.const
B' = mb.r*R + mb.g*G + mb.b*B + mb.const
```

UI 滑块单位 = 百分比 (0~100% 对应 0~1.0, 滑块范围 [-200%, +200%])。

#### 5) 颜色平衡 (三区间 smoothstep 权重)

```
luma = 0.299*R + 0.587*G + 0.114*B
w_shadow = max(0, 1 - 2*luma)
w_high   = max(0, 2*luma - 1)
w_mid    = 1 - w_shadow - w_high

c += (bal_shadow * w_shadow + bal_mid * w_mid + bal_high * w_high) * 0.5
保亮度模式: c = c - (luma_after - luma_before)
```

#### 6) 照片滤镜

```
out = lerp(c, filter_color, density)
保亮度: out *= luma_in / luma_out
```

预设滤镜色表 (RGB 0~255):
```
暖色 85   (255, 187, 122)
暖色 LBA  (255, 218, 144)
暖色 81   (235, 180, 134)
冷色 80   (107, 175, 224)
冷色 LBB  (140, 198, 245)
冷色 82   (162, 214, 230)
红 (234, 26, 35), 橙红 (235, 89, 49), 黄 (240, 232, 84),
绿 (54, 199, 128), 青 (56, 195, 209), 蓝 (37, 94, 217),
紫 (126, 56, 187), 品红 (224, 79, 173), 棕褐 (175, 117, 79),
深蓝 (30, 60, 145), 翡翠绿 (15, 130, 70), 深黄 (243, 180, 31), 水下 (40, 130, 195)
```

#### 7) 自然饱和度

```
sat_now = max(R,G,B) - min(R,G,B)         // 当前饱和度
delta_v = vibrance/100 * (1 - sat_now)    // 越不饱和越多加
delta_s = saturation/100                   // 全局饱和度直接加成
final_factor = 1 + delta_v + delta_s

luma = 0.299*R + 0.587*G + 0.114*B
c = luma + (c - luma) * final_factor
```

### 3.3 效果链顺序

按 AE 默认从上到下应用:

```
HSL → BrtCtr → Curves → ChannelMix → ColorBalance → PhotoFilter → Vibrance
```

UI 上**支持折叠**, 一期不开放重排序 (一期所有效果固定顺序, 减少风险)。

---

## 4. 资源结构适配

### 4.1 三种实测结构

```
9353 (最新, 完整):
  9353/
    addon/01/{stand,walk,...}/{方向}{帧}.tga
    addon/02/{...}
    addon/03/{...}
    00/{动作}/{方向}{帧}.tga
    01/{...}
    02/{...}
    03/{...}
    04/{...}
    body/{动作}/{方向}{帧}.tga
    body/add_lut/{01..06}.png

tianlongnnv:
  tianlongnnv/
    addon/01/{...}     (只有 1 个 addon 子层)
    00/{...}
    body/{...}

fengxiong (旧版, 无 addon):
  fengxiong/
    00/{...}
    01/{...}
    body/{...}
```

### 4.2 扫描算法

```cpp
扫描 root, 遍历所有一级子目录:
1. name == "addon"
   → 进入, 扫所有子目录 (按字母序),
     每个作为一个 AddonLayer { subIdx = 0,1,2,... }
2. regex_match(name, ^\d{2}$)
   → NumberedLayer { idx = 整数值 }   [00..99 都接受, 未来 05/06 自动支持]
3. name == "body"
   → BodyLayer
4. 否则跳过

每层下:
- 扫所有子目录作为动作名 (stand/walk/attack/...)
- 动作目录下找 *.tga, 用 ^(\d)(\d{3})\.tga$ 解析
- 找 add_lut/ 目录, 扫 *.png 作为该层方案缓存
```

### 4.3 加载顺序 (从底到顶)

```
合成顺序 (paint 顺序):
    body  (最底)
      → 00
      → 01
      → 02
      → 03
      → 04
      → addon (最顶, 单选)
```

### 4.4 层显隐 / 肤色层

| 操作 | 实现 |
|------|------|
| 显隐 toggle | 树节点 [👁] checkbox, 隐藏时跳过该层合成 |
| addon 单选 | RadioButton group,  只能选 1 个 addon 子层显示 |
| 肤色层标记 | 数字层右键 → "标记为肤色层", 该层右侧显示 [👤], 渲染时 bypass 颜色效果 |

---

## 5. 多方案系统

### 5.1 数据模型

```cpp
struct EffectStack {
    bool enabled[7];           // 7 个效果开关
    HslParams      hsl;
    BrtCtrParams   brtCtr;
    CurveParams    curves;     // RGB + 三通道 控制点列表
    ChMixParams    chMix;      // 3×4 矩阵
    ColorBalParams colorBal;
    PhotoFilParams photoFil;
    VibranceParams vibrance;
};

struct Scheme {
    QString    name;                        // "默认本体" / "方案 02" / 用户改名
    QHash<LayerKey, EffectStack> perLayer;  // 每层独立参数 (按 layer key)
    QPixmap    thumbnail;                   // 320×80 后台烘焙
    bool       isBuiltin = false;           // 默认方案 1 不可删
};

struct Project {
    QString          sourceRoot;            // 资源根目录
    QString          outputRoot;            // 导出根目录
    QVector<LayerEntry> layers;             // 扫描到的层
    int              skinLayerIdx = -1;     // 肤色层索引 (-1 = 无)
    int              currentAddonIdx = 0;   // addon 当前显示索引
    QVector<Scheme>  schemes;               // 至少 1 个 (默认本体)
    int              currentScheme = 0;     // 当前激活方案
};
```

### 5.2 方案 1 = 默认本体

- 7 效果全部 disabled
- LUT = `颜色图.png` 原图 (即 add_lut = 颜色图)
- 渲染结果 = 源 TGA 原色
- **不可删除, 不可改名**

### 5.3 方案操作

| 按钮 | 动作 |
|------|------|
| `[+]` 新建 | 新方案 (复制当前 stack) |
| `[📋]` 复制 | 复制选中方案为新方案 |
| `[🎲]` 随机 | 当前方案全层随机化 |
| `[↺]` 重置 | 当前方案全层置零 |
| `[✏]` 重命名 | 双击或按钮 |
| `[🗑]` 删除 | 默认本体不能删, 至少保留 1 个 |

---

## 6. UI 布局 (Qt6 Widgets + QDockWidget)

```
┌──────────────────────────────────────────────────────────────────┐
│ 文件  扫描  方案  导出  视图  帮助                  [─][□][×]   │
├──────────────────────────────────────────────────────────────────┤
│ ┌─────────┐ ┌────────────────────────────┐ ┌─────────────────┐  │
│ │ 资源树   │ │  双画布预览                 │ │ 方案画廊         │  │
│ │ (左 Dock)│ │  ┌──────────┐┌──────────┐ │ │ ━━━━━━━━━━━━━━━ │  │
│ │         │ │  │ 本体 (左) ││ 变色 (右) │ │ │ [缩略]方案 1 ●  │  │
│ │ ▼addon  │ │  │           ││           │ │ │ [缩略]方案 2    │  │
│ │  ⚪01    │ │  └──────────┘└──────────┘ │ │ [缩略]方案 3    │  │
│ │  ⚪02    │ │                            │ │ ...             │  │
│ │  ⚫03    │ │  动作:[stand ▾]            │ │ [+] [📋] [🎲]   │  │
│ │ ─00 [👁]│ │  方向:1 2 3 4 5 6 7 8       │ │ [↺] [✏] [🗑]  │  │
│ │ ─01 [👁]│ │  帧率:[ 10] 当前:[ 5/12]    │ └─────────────────┘  │
│ │ ─02 [👁👤│ │  背景:[#3C3C3C] [图...] [×]│                      │
│ │ ─03 [👁]│ │  ⏪ ⏯ ⏩                   │                      │
│ │ ─04 [👁]│ │                            │                      │
│ │ ─body[👁│ │                            │                      │
│ └─────────┘ └────────────────────────────┘                      │
│                                                                  │
│ ┌──────────────────────────────────────────────────────────────┐ │
│ │ 颜色效果 (当前层: 00)  (下 Dock)                              │ │
│ │  ▼ [☑] 色相/饱和度       H[━━●━━ 0]  S[━●━━ -10] L[━━●━ +5] │ │
│ │  ▶ [☑] 亮度/对比度                                           │ │
│ │  ▶ [☐] 曲线  [打开曲线编辑器]                                │ │
│ │  ▼ [☑] 通道混合器        [3×4 矩阵滑块组]                    │ │
│ │  ▶ [☐] 颜色平衡                                              │ │
│ │  ▶ [☑] 照片滤镜                                              │ │
│ │  ▶ [☐] 自然饱和度                                            │ │
│ │  [🎲 当前层随机] [↺ 当前层重置] [📋 复制到所有层]              │ │
│ └──────────────────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────────────┘
```

---

## 7. 项目目录结构

```
HighPro_LUT/
├── CMakeLists.txt
├── CMakePresets.json
├── README.md
│
├── extern/                          (vendored, 静态嵌入)
│   ├── stb/
│   │   ├── stb_image.h
│   │   └── stb_image_write.h
│   └── nlohmann/
│       └── json.hpp
│
├── assets/                          (Qt 资源系统 .qrc 编入 EXE)
│   ├── 颜色图.png                    默认 HALD-CLUT
│   ├── shaders/
│   │   ├── effect_chain.hlsl        7 效果链 (烘焙 LUT)
│   │   ├── recolor.hlsl             运行时变色
│   │   ├── composite.hlsl           多层合成
│   │   └── fullscreen_quad.hlsl     全屏顶点着色器
│   ├── fonts/
│   │   └── NotoSansSC.ttf
│   └── icons/
│
├── src/
│   ├── main.cpp
│   │
│   ├── core/
│   │   ├── HaldClut.h/.cpp          16³ HALD-CLUT 编解码
│   │   ├── ColorEffect.h/.cpp       7 效果数据结构
│   │   ├── EffectStack.h/.cpp       每层效果栈 (enabled[7] + 7 个 params)
│   │   ├── LayerData.h/.cpp         单层资源 (动作/方向/帧)
│   │   ├── Project.h/.cpp           整工程序列化
│   │   ├── Scheme.h/.cpp            方案 (多套 EffectStack 组合)
│   │   ├── ResourceScanner.h/.cpp   扫描 + 三种结构适配
│   │   ├── RandomGenerator.h/.cpp   合理范围随机
│   │   └── PathUtil.h/.cpp          中文路径 utf8↔wchar
│   │
│   ├── render/
│   │   ├── D3D11Context.h/.cpp      Device/Context/SwapChain 单例
│   │   ├── D3D11Texture.h/.cpp      Texture2D + SRV/RTV 包装
│   │   ├── D3D11Shader.h/.cpp       VS/PS 编译缓存
│   │   ├── LutBaker.h/.cpp          7 效果链烘焙到 256×16 LUT
│   │   ├── FrameRenderer.h/.cpp     单帧多层合成 + LUT 变色
│   │   ├── FrameLoader.h/.cpp       异步 TGA 解码 (3 个 IO 线程)
│   │   ├── FrameCache.h/.cpp        LRU GPU 纹理池 (默认 2GB 上限)
│   │   └── PngExporter.h/.cpp       LUT/帧导出 PNG
│   │
│   ├── ui/
│   │   ├── MainWindow.h/.cpp        主窗口 + Dock + 菜单
│   │   ├── D3DWidget.h/.cpp         嵌入 D3D11 SwapChain 的 QWidget
│   │   ├── PreviewPanel.h/.cpp      双画布 + 播放控制
│   │   ├── LayerTreePanel.h/.cpp    左侧资源树
│   │   ├── EffectPanel.h/.cpp       右侧 7 效果手风琴
│   │   ├── SchemePanel.h/.cpp       方案画廊
│   │   ├── widgets/
│   │   │   ├── CurveEditor.h/.cpp   PS 风格曲线
│   │   │   ├── ColorMatrix.h/.cpp   3×4 通道混合器
│   │   │   ├── BalanceWheel.h/.cpp  色彩平衡
│   │   │   ├── PhotoFilterCombo.h/.cpp  滤镜下拉
│   │   │   └── SliderRow.h/.cpp     标签+滑块+SpinBox 复用件
│   │   └── ExportDialog.h/.cpp      最终输出
│   │
│   └── app/
│       ├── ProjectController.h/.cpp 项目读写指挥
│       ├── ThumbnailWorker.h/.cpp   方案缩略图后台烘焙
│       └── AppSettings.h/.cpp       QSettings 持久化
│
└── tests/                           (单元测试)
    ├── test_hald_clut.cpp
    ├── test_effect_chain.cpp
    └── test_resource_scanner.cpp
```

---

## 8. 性能目标 (D3D11 实测可达)

| 指标 | 目标 | 备注 |
|------|------|------|
| 滑块拖动 → 画面更新延迟 | < 16 ms (60 FPS) | 30 ms debounce 后 |
| 序列帧播放 | 稳定 60 FPS (最多 4000 帧) | 异步加载 + 预取 |
| 方向/动作切换 | < 100 ms | LRU 缓存命中 |
| 单帧 LUT 变色 | < 1 ms | 1024×1024, GPU PS |
| 烘焙 LUT (单方案 7 效果) | < 1 ms | 4096 像素, 一个 Pass |
| 4000 帧批量导出 | < 30 s | 1024×1024 PNG, 多线程 |
| 内存占用 | < 1.5 GB | 4000 帧 RGBA8 1024² LRU 池 |
| EXE 启动 | < 2 s | Qt 静态版冷启动 |
| EXE 体积 | 25~40 MB | Qt 静态 + MSVC /MT |

---

## 9. 风险与对策

| 风险 | 等级 | 对策 |
|------|------|------|
| Qt 静态版编译漫长 (~2h) | 中 | 一期可先用动态版 + windeployqt 出绿色文件夹, 后期再切静态 |
| Qt + D3D11 集成踩坑 (HWND/DPI/resize) | 中 | 走 `setAttribute(WA_NativeWindow + WA_PaintOnScreen)` 标准方案, paintEngine() 返回 nullptr |
| 中文路径下 stb_image fopen 失败 | 低 | 统一用 `QFile::readAll` + `stbi_load_from_memory` |
| HLSL 编译失败 (旧驱动) | 低 | 目标 Feature Level 11_0, SM 5.0 即可, 兼容 Win7 SP1+ |
| 4000 帧内存爆炸 | 中 | LRU 池 + 上限可配, 超出释放最久未用 |
| 滑块连续拖动卡顿 | 中 | 30 ms debounce 合并烘焙请求 |
| 影子被效果链推离 (0,0,0) | 低 | 烘焙后强制锁 (0,0) 像素 = (0,0,0) |
| 不同显卡颜色精度差异 | 低 | LUT 用 `R8G8B8A8_UNORM_SRGB`, 全程 sRGB 工作流 |

---

## 10. 编码与构建规范

| 项 | 规范 |
|---|------|
| 编码 | UTF-8 with BOM (Windows + MSVC 兼容) |
| 命名 | 类 PascalCase, 函数 camelCase, 成员 m_xxx, 常量 kXxx |
| 头文件 | `#pragma once`, 不用 include guard |
| 智能指针 | `std::unique_ptr` (CPU), `Microsoft::WRL::ComPtr` (D3D 资源) |
| 异常 | 关闭 (`/EHsc` 仅必要), 用错误码 + Qt 信号 |
| 日志 | qDebug + 文件 sink (`QFileLogger`), Release 关 debug |
| C++ 标准 | C++17 |
| 编译器 | MSVC 2022 v143 (Qt 静态版要求) |
| 平台目标 | Windows 10 1809+ x64 (D3D11 FL 11.0) |
| 打包 | CMake `install()` + cpack zip |

---

## 11. 文档地图

| 文件 | 用途 |
|------|------|
| `LUT变色_开发详情.md` | py 旧版规则 |
| `PP官方变色_案例整理.md` | PP 管线案例 |
| `PP官方管线_Tab2_TGA变色输出_开发详情.md` | PP Tab2 实现 |
| `PP官方管线_技术文档.md` | PP 算法文档 |
| `颜色图处理.md` | 颜色图原理 + 效果建议 |
| `开发文档/角色序列图处理.md` | 需求初稿 |
| `开发文档/颜色图处理.md` | 颜色图原理 |
| `开发文档/高性能序列帧角色换色工具EXE开发技术方案研究.md` | 框架调研 |
| **`开发文档/01_技术方案_HighPro_LUT.md`** | **本文档** |
| **`开发文档/02_开发里程碑_HighPro_LUT.md`** | **里程碑与排期** |

---

*文档生成时间: 2026-05-30*
