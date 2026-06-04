# TGA图像光晕问题

## 来自你的消息:
同一张TGA的图，带通道
在AE和游戏里效果是一样的，左边，中间
在开发的EXE里面导入同样的一张图，最右边的效果光晕溢出了。
这个问题是什么原因？
如果开打开的EXE，需要怎么给AI说 ，导入 TGA的图/序列，带通道。保持跟AE的渲染模式？
用程序员开发的专业角度的语言说明。


![image](https://brainmaker.s3v2.nie.netease.com/brainmaker/prod/1780376852797-image.png)


## 来自 BrainMaker 的消息:
根因大概率就是一句话：

**你的 EXE 在处理这张带 Alpha 的 TGA 时，Alpha 解释方式或混合状态和 AE / 游戏引擎不一致。**  
最常见的是：

- **素材是预乘 Alpha（Premultiplied Alpha）**
- 但你的 EXE 按 **直通/直 Alpha（Straight Alpha）** 去混合  
  或者反过来

这就会导致你说的这种现象：

- 在 AE、游戏里看起来正常
- 在你们自研 EXE 里出现 **边缘发白、光晕外溢、亮部炸开、黑边/白边、透明区颜色异常**

---

# 一、从程序员角度，这是什么问题

这是一个典型的：

**Alpha 语义不一致 + Blend State 配置错误 + 可能伴随色彩空间处理错误**

重点先看前两个，因为它们最常见。

---

## 1）Straight Alpha 和 Premultiplied Alpha 混用了

带通道图片常见有两种表示方式：

### A. Straight Alpha
RGB 保持原始颜色，Alpha 单独存透明度。

公式：

```text
stored.rgb = original.rgb
stored.a   = alpha
```

渲染时常用混合公式：

```text
out.rgb = src.rgb * src.a + dst.rgb * (1 - src.a)
```

对应常见 Blend State：

```text
SrcBlend = SRC_ALPHA
DstBlend = ONE_MINUS_SRC_ALPHA
```

---

### B. Premultiplied Alpha
RGB 已经乘过 Alpha。

公式：

```text
stored.rgb = original.rgb * alpha
stored.a   = alpha
```

渲染时应该用：

```text
out.rgb = src.rgb + dst.rgb * (1 - src.a)
```

对应 Blend State：

```text
SrcBlend = ONE
DstBlend = ONE_MINUS_SRC_ALPHA
```

---

## 2）为什么会“光晕溢出”

如果素材本身是 **预乘 Alpha**，但你的 EXE 用了 **Straight Alpha 的混合方式**：

```text
out.rgb = src.rgb * src.a + dst.rgb * (1 - src.a)
```

由于 `src.rgb` 本来就已经乘过一次 alpha，结果你又乘了一次，  
就会出现边缘亮度、颜色过渡不对，导致：

- 发光边缘异常
- 光晕区域过亮或过脏
- 半透明区域颜色失真
- 边缘出现不自然扩散

反过来，如果素材是 Straight Alpha，但渲染时按 Premultiplied 去混，也会出问题。

---

# 二、为什么 AE 和游戏里正常，EXE 里不正常

说明两件事：

## AE 正常
AE 在解释素材 Alpha 时，通常允许你指定：

- **Straight**
- **Premultiplied**
- Premultiplied with color

AE 的素材解释和合成链路是完整的，所以效果正常。

---

## 游戏里正常
说明游戏引擎这边很可能：

- 正确识别了贴图 Alpha 类型
- 或导入时自动转换了 Alpha
- 或材质 / Shader / Blend State 已经按正确方式处理

---

## 你们自研 EXE 不正常
这通常表示 EXE 的某一层没有对齐：

### 可能错误点包括：

1. **TGA 读取器只读到了 RGBA，但没有定义 Alpha 是 Straight 还是 Premultiplied**
2. **渲染管线默认用了普通透明混合**
3. **Shader 中又手动乘了一次 Alpha**
4. **导入序列帧时做了错误预处理**
5. **sRGB / Linear 色彩空间不一致，导致亮边更明显**
6. **纹理过滤读取到了透明区域里的高亮 RGB，造成 fringe / halo**

---

# 三、最专业、最准确的程序员表述

你可以把这个问题描述为：

> 当前 EXE 在渲染带 Alpha 的 TGA/TGA 序列时，素材 Alpha 语义与渲染端混合模式不一致。AE 和游戏内表现一致，说明源素材本身没有问题；EXE 端大概率在 Alpha 解释、预乘处理、Blend State 或色彩空间处理上存在偏差。最终表现为半透明发光边缘出现 halo overflow / bright fringe / additive bloom-like spill。

更工程化一点可以说：

> 该问题本质上是 Premultiplied Alpha 与 Straight Alpha 管线不匹配。若源 TGA 为 PMA（预乘 Alpha），则 EXE 端必须使用 PMA 对应的混合状态（Src=ONE, Dst=ONE_MINUS_SRC_ALPHA），并确保 shader 不再重复执行 `rgb *= alpha`。若源 TGA 为 Straight Alpha，则应使用标准透明混合（Src=SRC_ALPHA, Dst=ONE_MINUS_SRC_ALPHA）。另外需要确认纹理采样色彩空间、mipmap 生成方式、透明边缘 RGB bleed 处理是否与 AE/游戏保持一致。

---

# 四、如果“告诉 AI / 告诉开发人员”，应该怎么说

如果你是要给 AI 下指令，或者给开发同事提需求，建议直接说成这种规范语言：

---

## 方案一：简洁版需求描述

```text
导入 TGA/TGA 序列（含 Alpha）时，请保持与 AE 一致的 Alpha 解释和合成结果。
要求：
1. 明确支持 Straight Alpha 与 Premultiplied Alpha 两种模式；
2. 若素材为 Premultiplied Alpha，渲染混合状态使用：
   SrcBlend = ONE
   DstBlend = ONE_MINUS_SRC_ALPHA
   并禁止 shader 再次执行 rgb *= alpha；
3. 若素材为 Straight Alpha，渲染混合状态使用：
   SrcBlend = SRC_ALPHA
   DstBlend = ONE_MINUS_SRC_ALPHA；
4. 纹理采样、色彩空间、mipmap、透明边缘插值方式需与 AE / 游戏引擎对齐，避免 halo、bright fringe、黑边或白边；
5. 目标是 EXE 中导入后的视觉效果与 AE 预览保持一致。
```

---

## 方案二：更专业的开发说明

```text
请检查 EXE 的 TGA/TGA 序列导入与渲染链路，确保 Alpha pipeline 与 AE 保持一致。

重点检查项：
1. TGA 解码后 Alpha 类型是否被正确标记为 Straight Alpha 或 Premultiplied Alpha；
2. Blend State 是否与素材 Alpha 语义匹配：
   - Straight Alpha: Src=SRC_ALPHA, Dst=ONE_MINUS_SRC_ALPHA
   - Premultiplied Alpha: Src=ONE, Dst=ONE_MINUS_SRC_ALPHA
3. Pixel Shader / Material 阶段是否重复进行了 alpha premultiply（例如 rgb *= a）；
4. 纹理是否按正确的色彩空间采样（sRGB / Linear）；
5. 是否存在透明边缘 RGB 污染、mipmap 生成错误、双线性过滤导致的 fringe/halo；
6. 若为特效序列（火焰、爆炸、辉光），优先按 Premultiplied Alpha 管线处理，并保证与 AE 合成结果一致。
```

---

# 五、开发里具体要检查什么

如果按程序员排查，建议按这个顺序：

---

## 1）先确认素材到底是 Straight 还是 Premultiplied

这是第一步，不然所有排查都可能方向错。

你要确认：

- AE 导出时 Alpha 选项是什么
- TGA 序列是否做过中转
- 游戏引擎导入时是否自动转换过

如果 AE 导出的是带辉光、烟雾、火焰这类特效，很多时候更适合走 **Premultiplied Alpha** 管线。

---

## 2）检查 EXE 的 Blend State

如果是 PMA 素材：

```text
SrcBlend = ONE
DstBlend = ONE_MINUS_SRC_ALPHA
```

如果是 Straight 素材：

```text
SrcBlend = SRC_ALPHA
DstBlend = ONE_MINUS_SRC_ALPHA
```

这是最核心的地方。

---

## 3）检查 Shader 是否重复乘 Alpha

很多项目会在 shader 里写：

```glsl
color.rgb *= color.a;
```

如果贴图本身已经是 PMA，这句就会造成二次预乘。

结果就是：

- 边缘变脏
- 光晕异常
- 透明过渡不对

所以必须确认：

- 图片是否已预乘
- shader 是否再次预乘
- CPU 导入阶段是否也做过预乘

只能乘一次。

---

## 4）检查色彩空间

这个经常被忽略，但对“发光溢出”观感影响很大。

要确认：

- TGA 作为纹理读入时是否当作 sRGB
- 混合是在 Linear 还是 Gamma 空间做
- AE 的预览和 EXE 是否在同一色彩管理策略下

如果 AE / 游戏在一个更正确的线性工作流里，而 EXE 在 Gamma 空间直接混合，  
亮边、过曝、发白会更明显。

专业点说就是：

> 需要确保 alpha compositing 和 color blending 在一致的 color space 下完成，优先在线性空间进行混合。

---

## 5）检查透明边缘的 RGB bleed

即使 Alpha=0，RGB 也可能不是黑色，而是残留了亮色信息。  
双线性采样、缩放、mipmap 时会把这些颜色采到边缘，形成 halo。

尤其是：

- 火焰
- 爆炸
- 光效
-贴图

最容易中招。

常见解决方式：

- 导出前做 alpha bleed / edge padding
- 生成 mipmap 时考虑 alpha coverage
- 对透明区做扩边填色
- 使用 PMA 管线减少 fringe

---

# 六、如果你要一句话让 AI 或开发照着改

你可以直接发这段：

```text
请把 TGA/TGA 序列的导入与渲染改为与 AE 一致的 Alpha 合成管线。
先确认素材是 Straight Alpha 还是 Premultiplied Alpha：
- 若为 Premultiplied Alpha，使用 Blend State: Src=ONE, Dst=ONE_MINUS_SRC_ALPHA，且 shader 不要再次执行 rgb *= alpha；
- 若为 Straight Alpha，使用 Blend State: Src=SRC_ALPHA, Dst=ONE_MINUS_SRC_ALPHA。
同时检查 sRGB/Linear 色彩空间、mipmap、双线性采样及透明边缘 RGB bleed，避免光晕外溢、白边和亮边异常。
目标是 EXE 中的显示结果与 AE 和游戏引擎一致。
```

---

# 七、如果你要“程序员开发的专业角度”的最终结论

可以直接这么写：

```text
该问题本质上不是 TGA 素材本身损坏，而是 EXE 渲染链路中的 Alpha compositing pipeline 与 AE/游戏引擎不一致。最可能的原因是源素材采用了 Premultiplied Alpha，而 EXE 端使用了 Straight Alpha 的 blend state，或 shader 中对 alpha 做了重复预乘；其次需要排查 sRGB/Linear 色彩空间不一致、mipmap/过滤导致的透明边缘颜色泄漏。修复方式是统一素材 Alpha 语义、纹理导入策略、shader 处理与 GPU blend state，使其与 AE 的合成模式完全对齐。
```

如果你愿意，我还可以继续帮你整理成下面任一种格式：

1. **给开发同事的 BUG 单**
2. **给 AI 的精确提示词**
3. **OpenGL / DirectX / Unity / UE 的具体配置方式**
4. **一段伪代码，说明导入 TGA 序列时该怎么处理 Alpha**