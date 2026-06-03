# Alpha 合成空间踩坑复盘 — 光晕外溢 / 亮度抬升

> 版本: v1.0  
> 日期: 2026-06-02  
> 项目: HighPro_LUT  
> 现象: 同一张带 Alpha 的 TGA, AE / 游戏引擎显示正常, EXE 渲染光晕外溢、扩散、灰雾感  
> 配套文档: `03_TGA加载与渲染管线.md`, `TGA图片渲染光晕溢出问题定位分析.md`

---

## 一、终极结论 (一句话)

**AE 默认在 Gamma (sRGB) 空间做 alpha 合成, 我们 EXE 在 Linear 空间合成 — 同样的 Straight Alpha 公式, 在两个空间里出来的视觉差 ~9/255 亮度, 半透明区被抬高 → 光晕看起来"溢出".**

修复 = 把 EXE 的合成空间从 Linear 切回 Gamma, 与 AE 默认行为对齐.

---

## 二、专业版剖析

### 2.1 先把概念分清

| 概念 | 含义 |
|-----|------|
| **Straight Alpha** | RGB 与 Alpha 独立存储 (rgb 是原色, a 单独存覆盖度) |
| **Premultiplied Alpha** | rgb 已乘 alpha (rgb_premul = rgb_straight × a) |
| **Gamma 空间合成** | 直接在 sRGB 字节空间做 alpha-over, 不做 sRGB→linear 转换 |
| **Linear 空间合成** | 先 sRGB→linear, 在 linear 空间做 alpha-over, 再 linear→sRGB encode |
| **AE 的 "直接"/"预乘" 选项** | 是 **alpha 类型** 选项, 不是空间选项, 跟本次问题无关 |
| **AE 的 "Blend Colors Using 1.0 Gamma"** | 才是 **空间** 选项, 默认**不勾** = Gamma 空间合成 |

> 关键: 之前我把 AE 的"直接 vs 预乘"误当成主要矛盾, 折腾了 alpha 类型, 走了远路.

### 2.2 物理原因 (为什么 Linear 比 Gamma 更亮)

sRGB 不是线性. 同一个**字节值** 0.5 在两个空间数值不同:

```
sRGB 数值 0.5  →  线性数值 ≈ 0.214   (能量约 1/5)
sRGB 数值 1.0  →  线性数值  = 1.0
```

`sRGB→linear` 是凸函数, 把中等亮度"压低".

#### 半透明 over 公式比较

设 src.rgb=200/255≈0.784, a=0.5, bg=0.235:

**Gamma 空间合成 (= AE 默认)**

```
out = 0.784 * 0.5 + 0.235 * (1 - 0.5) = 0.510   (字节 ≈ 130)
```

**Linear 空间合成 (= 之前 EXE)**

```
src_lin = srgb2lin(0.784) ≈ 0.578
bg_lin  = srgb2lin(0.235) ≈ 0.046
out_lin = 0.578 * 0.5 + 0.046 * 0.5 = 0.312
out     = lin2srgb(0.312) ≈ 0.601   (字节 ≈ 153)
```

**差**: 153 - 130 = **23/255**, 半透明区每个像素亮 23 个亮度级 → 光晕"外溢".

实测 `body/stand/0000.tga`: 半透明区均值 (R/G/B) Linear 比 Gamma 多 **8.9 / 9.2 / 2.3** (差异随像素特性变化, 取整体均值).

### 2.3 错误版 EXE 的渲染管线 (反例, 已抛弃)

```
TGA 文件 (sRGB 字节, Straight)
  ↓
R8G8B8A8_UNORM 纹理 (sRGB 字节透传, 无 GPU sRGB 转换)
  ↓
PS Sample → src.rgb (sRGB 字节)
  ↓
PS 内 srgbToLinear(src.rgb) → linear 数值       ★ 错误源 #1
  ↓
return float4(linear_rgb, src.a)                ★ PS 输出当 linear 看
  ↓
BlendState SRC_ALPHA / INV_SRC_ALPHA            ★ 在 linear 空间做 over
  ↓
B8G8R8A8_UNORM_SRGB RTV → GPU 自动 linear→sRGB encode  ★ 错误源 #2
  ↓
屏幕 (sRGB 显示器)
```

整个链路看起来 "物理正确" — sRGB 解码 → linear 计算 → sRGB 编码. 但**这个"物理正确"恰恰偏离了 AE 的默认行为**, 视觉就跟 AE 不一致.

### 2.4 修复版 EXE 的渲染管线 (现行)

```
TGA 文件 (sRGB 字节, Straight)
  ↓
R8G8B8A8_UNORM 纹理 (sRGB 字节透传)
  ↓
PS Sample → src.rgb (sRGB 字节)
  ↓
PS 不做 srgbToLinear, 直接计算                   ✓ Gamma 空间
  ↓
return float4(srgb_rgb, src.a)                  ✓ PS 输出就是 sRGB 字节
  ↓
BlendState SRC_ALPHA / INV_SRC_ALPHA            ✓ 在 Gamma 空间做 over
  ↓
B8G8R8A8_UNORM RTV (非 _SRGB) → 字节透传        ✓ 不做 encode
  ↓
屏幕 (sRGB 显示器, 它自己当 sRGB 解释字节)
  ↓
视觉 = AE 默认 (Gamma 空间合成)
```

物理上"不严格", 但视觉上**与 AE 默认完全一致**. 这才是"对齐 AE/游戏引擎"的正确做法.

### 2.5 改动清单

| 文件 | 改动 |
|------|------|
| `src/ui/D3DWidget.cpp` | RTV format `B8G8R8A8_UNORM_SRGB` → **`B8G8R8A8_UNORM`** (取消 GPU 自动 sRGB encode) |
| `src/render/FrameRenderer.cpp` | 清屏背景值: 移除 `sRgbToLinear()` 调用, 直接用 sRGB 字节 |
| `assets/shaders/recolor.hlsl` `PSMain` | 删 `mapped = srgbToLinear(mapped)` |
| `assets/shaders/effect_chain.hlsl` `PSMain` | 删 `c = srgbToLinear(c)` |
| `assets/shaders/fullscreen_quad.hlsl` `PSMain` / `PSMainSolid` / `PSMainTriangleUp` | 全部删 `srgbToLinear` 调用 |

### 2.6 不变的关键决策 (没改的也要记下)

| 项 | 配置 | 为何不动 |
|---|------|---------|
| 素材 alpha 模型 | Straight | 实测素材是 Straight, 不强行预乘 |
| BlendState | `SRC_ALPHA / INV_SRC_ALPHA` | Straight Alpha over 标准, AE 默认行为也是这个公式 |
| Mipmap | 关 (`MipLevels=1`) | 防 mipmap 引发的边缘 RGB bleed |
| Sampler AddressMode | CLAMP | 防 wrap 边界异常 |
| 纹理格式 | `R8G8B8A8_UNORM` (非 _SRGB) | 一直就是非 _SRGB, 跟 RTV 对齐了之后, sRGB 字节空间一路透传, 是最稳的 |

---

## 三、大白话版 (人话 / 类比)

### 3.1 这件事到底是啥

想象你有一张彩色照片要往墙上贴, 这张照片有些地方是半透明的 (光晕、烟雾).

**问题**: 同一张照片, 我朋友 (AE) 贴出来很好看, 我自己贴出来光晕"糊一团又亮又散". 大家用的胶水、贴法都一样, 为啥视觉差这么多?

**真相**: 区别不在贴法, 在 "**我在哪个空间里调亮度**".

### 3.2 啥叫"空间"

人眼看亮度是**非线性的** — 你看一盏灯亮度感觉翻倍, 实际能量翻了 5 倍. 这叫 **gamma 校正**.

电脑显示器存的字节值 (0~255) 是按"人眼感觉"线性分的, 不是按"真实能量"线性分的. 这套是 **sRGB 空间** / **Gamma 空间**.

如果你想"真物理正确"地混合两束光的亮度, 必须先把字节翻译成"能量数字" (linear 空间), 算完再翻译回字节.

```
sRGB (人眼感觉) ←→ linear (物理能量)
   字节 0.5      ↔    能量 0.214
   字节 1.0      ↔    能量 1.0
```

### 3.3 为啥两个空间出来不一样

考虑半透明像素叠加背景:

**Gamma 空间** (直接在字节上算):
- 字节 200 (光晕) × 50% + 字节 60 (背景) × 50% = 字节 130

**Linear 空间** (翻译成能量再算):
- 字节 200 → 能量 0.578
- 字节 60  → 能量 0.046
- 0.578 × 50% + 0.046 × 50% = 0.312
- 翻译回字节 ≈ 153

**结果**: linear 空间算出来 **比 gamma 空间亮 23 个亮度级**. 你越是半透明, 这个差越大.

光晕本来就是大片半透明像素 → 整片光晕都被"抬亮" → **看起来溢出/扩散/糊一团**.

### 3.4 AE 是怎么做的

AE 默认**就在 Gamma 空间里直接算**, 它有个开关叫 "Blend Colors Using 1.0 Gamma" — 默认**不勾**, 就是 gamma 合成.

你在 AE 里勾上这个开关, 半透明光晕也会变亮 (你自己可以试).

游戏引擎大部分场景也是 gamma 合成 (除非美术专门要 PBR 物理正确照明).

### 3.5 我们 EXE 一开始为啥跟 AE 不一样

写代码的时候图书 / 教程都说"现代渲染要在 linear 空间做". 这话**对 3D PBR 渲染**对的, 但对**对齐 AE 视觉的 2D 合成工具是错的**. 我前面就是踩了这个坑.

具体我做了三件让 EXE 走 linear 路线:

1. **PS 着色器**里加了 `srgbToLinear` — 把采样到的 sRGB 字节翻译成能量
2. **RTV** 用 `_SRGB` 格式 — 写入时 GPU 自动把能量翻译回字节
3. **背景清屏色**也做 sRGB→linear 转换

三件事配起来, 整个 alpha over 在 linear 空间做了 — "物理正确"但**视觉上半透明区被抬亮 9~23 个级别**, 光晕外溢.

### 3.6 修复就一句

**把上面三件事全删掉**:

1. PS 着色器删 `srgbToLinear()` 调用
2. RTV 改成 `_UNORM` (非 _SRGB), GPU 不再做 encode
3. 背景清屏色直接用 sRGB 字节

EXE 不再"翻译来翻译去", 全程 sRGB 字节透传, blend 在 sRGB 空间做. **跟 AE 默认完全一致**.

### 3.7 为啥早没发现

因为这条 bug 表现是"**亮度差** 9~23 级 / 光晕外溢", 不是"颜色错了 / 黑屏了 / 崩了" — 数值范围还在 0~255 内, 没有越界, 算法没报错, 只是和 AE 比偏一点.

我之前一直在 alpha 类型 (Straight vs Premul) 上找原因, **方向错了**. 直到看了你给的"光晕溢出问题定位分析"文档, 第 217 行 "AE 默认 Gamma 空间合成" 一句话点醒, 才意识到根因在合成空间, 不在 alpha 类型.

---

## 四、踩坑时间线 (复盘自学用)

| 时间 | 我以为 | 实际 | 修法 (有效?) |
|------|--------|------|-------------|
| 12:08 | 显示精度不够 | 不是 | — |
| 12:20 | PyQt 预览的 alpha straight + 双线性插值 | 部分对 | PreviewCanvas 加 SmoothPixmapTransform + Premultiplied 转换 ✓ (修了 PyQt 版色块) |
| 12:33 | EXE 也是 alpha 处理错 | 错方向 | 改成 CPU 预乘 + ONE blend → 仍光晕大, 还引入反预乘除法误差 ✗ |
| 13:16 | 素材是 Straight (实测确认 77%) | 对 | 回滚到 SRC_ALPHA blend, 不预乘 ✓ (光晕色块解决, 但还溢出) |
| 13:26 | 视觉差是 zoom > 100% | 部分对 | 复位 100% 后差异减小, 但还有 ~9 级亮度差 |
| 13:55 | AE 直接 vs 预乘 | 误判 (那是 alpha 类型选项, 不是合成空间) | — |
| 14:00 | **AE 默认 Gamma 空间合成, 我们 Linear** | **正确根因** | 删 srgbToLinear + RTV 改 _UNORM ✓ |

---

## 五、核心知识点 (深度学习用)

### 5.1 sRGB / Linear 速记

```
sRGB → Linear:  s ≤ 0.04045 ?  s/12.92  :  ((s+0.055)/1.055)^2.4
Linear → sRGB:  L ≤ 0.0031308 ? L*12.92 :  1.055*L^(1/2.4) - 0.055
```

非线性段近似 gamma=2.2, 头部线性段是工程修正.

### 5.2 Alpha Over 公式速记

```
Straight (rgb 与 a 独立):
    out.rgb = src.rgb * src.a + dst.rgb * (1 - src.a)
    out.a   = src.a           + dst.a   * (1 - src.a)
    BlendState: SRC_ALPHA / INV_SRC_ALPHA

Premultiplied (rgb 已乘 a):
    out.rgb = src.rgb         + dst.rgb * (1 - src.a)
    out.a   = src.a           + dst.a   * (1 - src.a)
    BlendState: ONE / INV_SRC_ALPHA
```

两套公式数学上等价 (输入正确时输出相同), 但**预乘对采样/缩放更稳定** (双线性插值不会让边缘出色块).

### 5.3 何时用 Linear 何时用 Gamma

| 场景 | 推荐 |
|------|-----|
| **2D 合成工具对齐 AE 视觉** | **Gamma** ★ 本项目 |
| 2D 引擎对齐游戏内观感 | 看游戏引擎走 gamma 还是 linear, **大部分游戏 UI 是 gamma** |
| 3D PBR 渲染, 精确光照 | Linear |
| 颜色采样、HSL 转换、LUT 查找 | Gamma (sRGB 空间) — 因为美术工具都按 sRGB 字节调 |
| HDR / Bloom / 后期 | Linear |

我们这个项目是 LUT 烘焙工具 — 用户在 PS / 颜色图.png 里都是按 sRGB 字节工作 → **整条管线在 sRGB 空间最自洽**.

### 5.4 一个判断题

> "正确的写法应该一直在 linear 空间算, 最后才 sRGB encode"

**❌ 错** — 这是 PBR 时代的口号, 不适合所有场景. 工具类应用对齐美术软件视觉时, 应该跟随美术软件的合成空间.

### 5.5 RTV 格式与 PS 输出契约

| RTV format | PS 输出语义 | GPU 行为 |
|-----------|------------|---------|
| `*_UNORM` | sRGB 字节 (0~1) | 直接写, 字节透传 |
| `*_UNORM_SRGB` | linear (0~1) | 写时 GPU 自动 linear → sRGB encode |
| `*_FLOAT` | linear (HDR, 任意值) | 直接写 (无 encode) |

**契约不一致就出问题**:
- PS 输出 sRGB 字节, RTV 是 `_SRGB` → 双重编码, 颜色变浅
- PS 输出 linear, RTV 是 `_UNORM` → 没编码, 颜色发黑/低对比

我们之前是 PS 输出 linear + RTV `_SRGB` (一致, "物理正确"), 但**视觉跟 AE 偏**.  
现在改成 PS 输出 sRGB 字节 + RTV `_UNORM` (一致, "AE 默认"), 视觉对齐.

### 5.6 排查这类 bug 的通用思路

按文档 P0~P3 优先级:

1. **P0 alpha 类型** — 实测 a=0 区 RGB 是否清零 (premul 必清零)
2. **P0 BlendState** — 跟 alpha 类型是否配套
3. **P1 颜色空间** — 纹理 / RTV / PS 三者契约是否一致
4. **P1 合成空间** — Gamma vs Linear, 跟参考软件 (AE) 是否一致 ★ 本次踩的就是这条
5. **P2 Bloom / HDR** — 不相关项目本无, 排除
6. **P3 输出 gamma 校正** — RTV 与显示器契约

最有效手段: **CPU 用 numpy 复现 Gamma vs Linear 两种合成**, 把对应的 png 跟 EXE 截图、AE 截图三张并排对比, 哪个像哪个一目了然.

---

## 六、给后来人的 checklist

做"对齐 AE 视觉"的工具/合成器, 默认配置:

- [ ] 纹理: `R8G8B8A8_UNORM` (非 _SRGB)
- [ ] RTV: `_UNORM` (非 _SRGB)
- [ ] PS 输出: sRGB 字节 (不做 `srgbToLinear`, 不做预乘)
- [ ] 清屏色: sRGB 字节直接用
- [ ] BlendState: `SRC_ALPHA / INV_SRC_ALPHA` (Straight) 或 `ONE / INV_SRC_ALPHA` (Premul, 需 CPU 端预乘)
- [ ] Mipmap: 关
- [ ] Sampler: `MIN_MAG_MIP_LINEAR` + `CLAMP`
- [ ] 半透明区视觉与 AE (默认未勾 "Blend 1.0 Gamma") 对照, 误差 ≤ 5/255 视作通过

如果未来要切到 Linear 工作流 (例如加 Bloom):
- 全链路同步切, **不能只切一段** (例如只 RTV 用 _SRGB 但 PS 不做 sRGB→linear, 直接错)
- 给一个全局开关 (UI 层) 让用户在 Gamma / Linear 间切换, 默认 Gamma

---

## 七、参考

- 项目内 `TGA图片渲染光晕溢出问题定位分析.md` — 217-238 行 "为什么 AE 正常而自定义程序溢出" (本次解决根因来源)
- LearnOpenGL Gamma Correction: <https://learnopengl.com/Advanced-Lighting/Gamma-Correction>
- Adobe AE Color Management: <https://helpx.adobe.com/after-effects/using/color-management.html>
- ProVideo Coalition "Gamma-Corrected Compositing": <https://www.provideocoalition.com/gamma_intro/>

---

*文档生成时间: 2026-06-02*
