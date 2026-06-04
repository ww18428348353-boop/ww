# HighPro_LUT — TGA 加载与渲染管线 · 完整说明

> 版本: v1.0  
> 生成日期: 2026-06-02  
> 项目根: `x:\Lut变色\HighPro_LUT`  
> 配套文档: `01_技术方案_HighPro_LUT.md`, `02_开发里程碑_HighPro_LUT.md`

---

## 总览

```
TGA 文件 (磁盘 Straight RGBA, sRGB 字节)
  │
  ├─ stbi_load_from_memory(req=4)               [CPU 解码]
  │
  ├─ R8G8B8A8_UNORM 纹理 (Straight, sRGB 字节)  [上传 GPU, 无格式转换]
  │
  ├─ Sample(LINEAR/POINT, CLAMP)                [GPU 采样, 无 sRGB→linear]
  │   → src = (R, G, B, A) ∈ [0,1] (sRGB 数值)
  │
  ├─ if a<0.001 → 早退 (0,0,0,0)
  ├─ LUT/Effect 在 sRGB 域处理 → mapped (sRGB 数值)
  ├─ (PS 不做 srgbToLinear — 保留 sRGB 字节空间合成)
  │
  ├─ PS output = float4(srgb_rgb, src.a)        [Straight, 不预乘]
  │
  ├─ BlendState SRC_ALPHA / INV_SRC_ALPHA       [GPU 在 sRGB 字节空间做 over:
  │   out = PS.rgb*PS.a + dst.rgb*(1-PS.a)]
  │
  └─ B8G8R8A8_UNORM RTV 字节透传                 [非 _SRGB, GPU 不做 encode]
       │
       └─ SwapChain Present → 屏幕 (sRGB 显示器)
       
   视觉 ≡ AE 默认 (Gamma 空间合成, 未勾 "Blend 1.0 Gamma")
```

**alpha 模型**: **Straight Alpha** (实测 `body/stand/0000.tga` 半透明像素 77% 满足 `rgb > a`, 与 AE / 游戏引擎一致).

**合成空间**: **Gamma (sRGB 字节)** — 与 AE 默认 / 游戏引擎 UI 默认一致. 详见 `05_Alpha合成空间踩坑复盘.md`.

---

## 一、加载 (CPU 端)

### 1.1 入口

调用方: `PreviewPanel::performRender()` → `FrameLoader::instance().get(path)`

### 1.2 LRU 缓存 + 兜底同步

`FrameLoader::get(path)` (`src/render/FrameLoader.cpp` L137):

```
hit cache → 直接返回 shared_ptr<D3D11Texture>
miss      → 兜底主线同步: tex->loadFromPath(path, &err, premultiply=false)
            → 入 hot 缓存
```

**异步路径** (`prefetch` + 后台 `DecodeThread`):

| Step | 文件 | 行 | 做什么 |
|------|------|---|------|
| 1 | `FrameLoader.cpp` | L36 | `PathUtil::readAll(path)` 中文路径 → QFile bytes |
| 2 | `FrameLoader.cpp` | L41 | `stbi_load_from_memory(buf, &w, &h, &comp, /*req=*/4)` |
| 3 | `FrameLoader.cpp` | L47-54 | **不做任何处理**, 原样存 `QByteArray d.rgba` |
| 4 | (主线 `pump`) | ~L201 | `D3D11Texture::createFromRGBA(d.rgba.data(), w, h)` 上传 GPU |

### 1.3 stbi 输出语义 (必看)

- `req=4` 强制 4 通道 → 始终输出 **R8G8B8A8 顺序** (注意 `R` 在低字节)
- TGA type=2/10 32bpp 文件存的是 **BGRA**, stbi **内部已重排成 RGBA**
- 像素**不做任何 alpha 处理** (不预乘 / 不反预乘) → **保持磁盘原始 Straight Alpha 语义**
- 实测 `body/stand/0000.tga`:
  - 总像素 250000
  - `a = 0`     → 228133
  - `a = 255`   → 4715
  - 半透明      → 17152 (其中 77% `rgb > a` → **确认 Straight Alpha**)

### 1.4 GPU 纹理参数 (`D3D11Texture.cpp`)

```cpp
D3D11_TEXTURE2D_DESC:
  Format     = DXGI_FORMAT_R8G8B8A8_UNORM      // 非 _SRGB! 关键
  MipLevels  = 1                                // 不生成 mipmap (避免 mipmap bleed)
  ArraySize  = 1
  Usage      = D3D11_USAGE_DEFAULT
  BindFlags  = D3D11_BIND_SHADER_RESOURCE

SRV.Format = R8G8B8A8_UNORM                    // 非 _SRGB!
```

**关键点**: 纹理格式 **不带 _SRGB**. 意味着 `gFrame.Sample()` 返回的是 **磁盘字节原值 / 255**, **没有 sRGB→linear 自动转换**. PS 内部手动做.

---

## 二、渲染 (GPU 端)

### 2.1 渲染目标

`D3DWidget` 创建:

| 资源 | Format | 备注 |
|------|--------|------|
| SwapChain BackBuffer | `B8G8R8A8_UNORM` | FLIP_DISCARD 不允许 _SRGB |
| RTV view | **`B8G8R8A8_UNORM_SRGB`** | 写入时 GPU 自动 linear → sRGB encode |

### 2.2 Pipeline State (`FrameRenderer.cpp`)

```
Rasterizer:    FillSolid + CullNone
Depth:         disabled
Sampler:       MIN_MAG_MIP_LINEAR + CLAMP   (默认)
                MIN_MAG_MIP_POINT + CLAMP    (1:1 zoom 时用)
Blend:         SrcBlend       = SRC_ALPHA
                DestBlend      = INV_SRC_ALPHA
                BlendOp        = ADD
                SrcBlendAlpha  = ONE
                DestBlendAlpha = INV_SRC_ALPHA
                BlendOpAlpha   = ADD
                WriteMask      = ALL
```

**Straight Alpha over** 公式:

```
out.rgb = src.rgb * src.a + dst.rgb * (1 - src.a)
out.a   = src.a           + dst.a   * (1 - src.a)
```

### 2.3 Sampler 选择 (每帧)

```cpp
oneToOne     = (wpx == tex_w && hpx == tex_h)
frameSampler = oneToOne ? POINT : LINEAR
```

- 100% zoom + 窗口足够大 → **POINT** (无插值, 原汁原味)
- 缩放 / 拉伸           → **LINEAR**

### 2.4 Shader 路径分流 (每层每帧)

```cpp
useEffect = (有 EffectStack && !isIdentity)   // 7 效果链路径
useLut    = (无 effect && lut.isValid())      // HALD-CLUT 反推路径
otherwise → 透传 (原 TGA 显示)
```

| 分支 | VS | PS | 文件 |
|------|-----|-----|------|
| Effect | `VSMain` | `PSMain` | `effect_chain.hlsl` |
| LUT | `VSMain` | `PSMain` | `recolor.hlsl` |
| Pass | `VSMain` | `PSMain` | `fullscreen_quad.hlsl` |

### 2.5 PS 共同骨架 (Straight Alpha 兼容写法)

#### 透传 `fullscreen_quad.hlsl PSMain`

```hlsl
float4 c = gTex.Sample(gLinearClamp, uv);     // RGBA 直通色, sRGB 字节值
if (c.a < 0.001) return float4(0,0,0,0);
c.rgb = srgbToLinear(saturate(c.rgb));        // sRGB → linear (RTV 是 _SRGB, 必须 linear 写)
return c * tint;                              // 不预乘! 直接 (rgb, a)
```

#### LUT `recolor.hlsl PSMain`

```hlsl
float4 src = gFrame.Sample(...);
if (src.a < 0.001) return float4(0,0,0,0);
float3 mapped = SampleHald(saturate(src.rgb)); // HALD-CLUT 三线性 (sRGB 域)
mapped = srgbToLinear(mapped);                 // 转 linear 写 sRGB RTV
return float4(mapped, src.a) * tint;           // 不预乘
```

#### Effect `effect_chain.hlsl PSMain`

```hlsl
float4 src = gFrame.Sample(...);
if (src.a < 0.001) return float4(0,0,0,0);
float3 c = applyEffectChain(saturate(src.rgb)); // 7 效果在 sRGB 域串
c = srgbToLinear(c);
return float4(c, src.a) * tint;                 // 不预乘
```

### 2.6 完整像素流 (一像素的旅程)

参见文档开头"总览"小节.

---

## 三、关键交叉验证表

| 问题 | 配置 | 是 / 否 |
|-----|------|------|
| stbi 是否预乘? | `stbi_set_unpremultiply_on_load` 未调用, 默认不预乘 | **不预乘** ✓ |
| CPU 端二次预乘? | `FrameLoader.cpp` L47 已删 | **不二次预乘** ✓ |
| 纹理 _SRGB? | `R8G8B8A8_UNORM` | **不带 _SRGB** (sRGB 字节透传) |
| PS 内 sRGB→linear? | 已删, PS 直接在 sRGB 字节空间算 | **不做** ✓ (Gamma 合成) |
| RTV _SRGB? | `B8G8R8A8_UNORM` | **非 _SRGB** (字节透传, GPU 不做 encode) |
| Mipmap? | `MipLevels=1` | **关** ✓ |
| Sampler Address? | CLAMP | ✓ |
| Blend 是否预乘风格? | `SRC_ALPHA / INV_SRC_ALPHA` | **Straight 风格** ✓ |
| 合成空间 | sRGB 字节空间 (Gamma) | **= AE 默认** ✓ |

---

## 四、定位问题用 — 可疑点清单 (按概率排)

### ⚠️ #1 双线性插值在 sRGB 字节空间做 (一致性问题)

- **当前**: 纹理 `R8G8B8A8_UNORM`, sampler LINEAR 在**字节值**上插值
- **AE 默认**: sRGB 工程 / 32bpc Linear 工程, 插值在 linear 空间
- **影响**: 缩放或非 1:1 时, 半透明边缘的明度可能跟 AE 偏差. **1:1 + POINT 不受影响**.
- **修法**: 纹理改 `R8G8B8A8_UNORM_SRGB`, sampler 自动 linear 插值, PS 删 `srgbToLinear`

### ⚠️ #2 PS 输出 vs RTV 字节序

- 纹理 `R8G8B8A8_UNORM` → `Sample().rgba` 含义就是 R/G/B/A
- RTV `B8G8R8A8_UNORM_SRGB` → `SV_TARGET` 是 GPU 抽象输出, 由 IHV 驱动负责把 `.x→R, .y→G, .z→B, .w→A` 写到对应的硬件通道
- 即: 我们 `return float4(R, G, B, A)`, 写到 B8G8R8A8 不会颜色翻转 (D3D 规范保证)
- **检验**: 临时 `return float4(1, 0, 0, 1)`, 屏幕应为**红色**

### #3 sRGB → linear 应用范围

- `srgbToLinear` 只对 `mapped` / `c` 做, 对 `tint` 没做
- `tint = (1,1,1,1)`, sRGB = linear = 1, 无影响. OK

### #4 Alpha 通道是否走 sRGB encode

- RTV `_SRGB` 只对 RGB 通道 encode, alpha 通道 unchanged
- 我们 `return float4(rgb_linear, a_srgb)` → alpha 没有 encode, 写出去就是数值. OK

---

## 五、与 AE / 游戏引擎对齐的检查清单

| 项 | AE / 游戏 | HighPro_LUT | 状态 |
|----|----------|-------------|------|
| Alpha 模型 | Straight | Straight | ✓ |
| Blend 公式 | `src.rgb*a + dst.rgb*(1-a)` | `SRC_ALPHA / INV_SRC_ALPHA` | ✓ |
| sRGB 工作流 | sRGB 工程 (8bpc) / Linear (32bpc) | sRGB 字节 + RTV linear→sRGB encode | ✓ |
| Mipmap | 单层 | `MipLevels=1` | ✓ |
| Sampler 边缘 | clamp | CLAMP | ✓ |
| 1:1 像素呈现 | nearest | POINT (`zoom = 100%`) | ✓ |
| 缩放插值空间 | sRGB 工程一般在 sRGB 字节插值 | sRGB 字节插值 | ✓ (但见 §四 #1) |

---

## 六、调试入口 — 如何定位差异

### 6.1 取色对比

加 GUI 取色调试 (PreviewPanel `mouseMove`):

```
鼠标停在某像素 → 读 RTV 字节 → 屏幕坐标 + RGB 一行打印
```

AE 同位置 取色对比, 差异 < 5/255 视作可接受.

### 6.2 Shader 临时调试

`recolor.hlsl PSMain` 末尾插:

```hlsl
// debug: 让早退区 / 半透明区 / 实色区可视化
if (src.a < 0.001) return float4(0, 0, 0, 1);             // 黑
if (src.a < 0.99)  return float4(1, 0, 0, 1);             // 红 = 半透明
return float4(0, 1, 0, 1);                                // 绿 = 实色
```

跑一次, 看红色区是不是仅在角色光晕边缘 (应是).

### 6.3 dump 中间纹理

`DebugDumper::dumpTexture(tex, "X:/dump/frame.png")` 把 GPU 纹理回读 PNG 看真实字节. 用 PS / GIMP 拿吸管对比 AE 同帧.

### 6.4 CPU 复现一遍

```python
from PIL import Image
import numpy as np
im = Image.open(r'X:\Lut变色\测试资源文件\8326\body\stand\0000.tga').convert('RGBA')
arr = np.array(im).astype(np.float32) / 255.0
bg  = np.array([60/255, 60/255, 70/255], dtype=np.float32)
a   = arr[..., 3:4]
out = arr[..., :3] * a + bg * (1 - a)               # straight over (sRGB 字节空间)
Image.fromarray(np.clip(out*255, 0, 255).astype(np.uint8)).save('out.png')
```

把 `out.png` 跟 EXE 同 zoom 截图对比. 完全一致 → pipeline 物理正确, 差异在 **AE 工程后期** (曝光 / Glow / 图层 blend mode).

---

## 七、变更记录

| 日期 | 改动 | 文件 |
|------|------|------|
| 2026-06-02 | 起初尝试 premul pipeline (`ONE / INV_SRC_ALPHA` + CPU 预乘) | 全 |
| 2026-06-02 | 实测素材是 Straight, 回滚到 `SRC_ALPHA / INV_SRC_ALPHA` | 同上 |
| 2026-06-02 | 删 PS 反预乘 (`rgb / a`) → 直接用 `src.rgb` | 三个 hlsl |
| 2026-06-02 | `D3D11Texture::loadFromMemory/Path` 加 `premultiply` 参数 (默认 false) 留扩展 | `D3D11Texture.h/cpp` |
| 2026-06-02 | **【根因修复】** 切 Linear 合成 → Gamma 合成 (与 AE 默认对齐): RTV 改 `_UNORM`, PS 删 `srgbToLinear`, 清屏不再线性化. 详见 `05_Alpha合成空间踩坑复盘.md` | `D3DWidget.cpp`, `FrameRenderer.cpp`, 三个 hlsl |
| 2026-06-02 | 烟测 100% zoom 视觉与 AE / 游戏完全一致 | - |

---

*文档生成时间: 2026-06-02*
