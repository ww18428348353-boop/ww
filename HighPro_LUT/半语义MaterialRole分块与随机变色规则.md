# 半语义 MaterialRole 分块与随机变色规则

> 项目: `HighPro_LUT`  
> 主题: 当前人工分块到随机变色规则的映射方案  
> 目标: 不做复杂图像识别起步，先用已有分块语义稳定提升随机变色质量。

---

## 一、核心判断

当前分块很适合做 **半语义 MaterialRole**，不需要复杂图像识别起步。

现在的重点不是“自动猜层语义”，而是：

```text
已有人工分块语义
→ 把这些块映射成随机变色规则
→ 角色少时自动降级
→ 角色多时精细控制
```

你当前分块：

1. 肤色（保护层，功能已有）
2. 头发
3. 装饰01
4. 装饰02
5. 裙摆
6. 服装
7. 武器（金属 / 非金属）

备注：

```text
角色少的分类只有 3 块
角色分类多的有 7 块
```

这套结构已经足够支撑第一版高质量随机变色。

---

## 二、推荐 MaterialRole 定义

按当前 7 块，建议这样定义：

```cpp
enum class MaterialRole {
    Unknown,

    Skin,        // 肤色，保护层，已有
    Hair,        // 头发

    Decor01,     // 装饰01，主装饰 / 大装饰
    Decor02,     // 装饰02，次装饰 / 小装饰 / 宝石 / 发光点

    Skirt,       // 裙摆
    Clothing,    // 服装主体

    WeaponMetal, // 武器金属部分
    WeaponOther  // 武器非金属部分
};
```

更推荐使用抽象命名，避免和 `装饰01/02` 字面强绑定：

```cpp
enum class MaterialRole {
    Unknown,
    Skin,
    Hair,
    DecorPrimary,
    DecorSecondary,
    Skirt,
    Clothing,
    WeaponMetal,
    WeaponNonMetal
};
```

推荐第二种。名字更稳定，也更适合后续扩展。

---

## 三、你的 7 块对应随机职责

| 当前分块 | 推荐 Role | 随机职责 |
|---|---|---|
| 肤色 | `Skin` | 保护，默认不变 |
| 头发 | `Hair` | 可轻微变色，保明度 |
| 装饰01 | `DecorPrimary` | 主点缀色 |
| 装饰02 | `DecorSecondary` | 次点缀色 / 宝石 / 发光点 |
| 裙摆 | `Skirt` | 大面积主色延展或辅色 |
| 服装 | `Clothing` | 角色主色承载 |
| 武器金属 | `WeaponMetal` | 金 / 银 / 铜 / 黑铁色池 |
| 武器非金属 | `WeaponNonMetal` | 跟随服装或点缀色 |

推荐主次关系：

```text
服装 = 主色
裙摆 = 主色延展 / 辅色
头发 = 低强度协调色
装饰01 = 点缀色
装饰02 = 高亮点缀 / 发光色
武器金属 = 固定金属色池
武器非金属 = 跟服装或装饰统一
肤色 = 保护
```

也就是：

```text
Clothing 决定整体风格
Skirt 扩展整体面积
Decor01/02 负责漂亮
Weapon 负责质感
Hair 负责协调
Skin 不动
```

不要让每块都抢主色。

---

## 四、推荐 SchemePalette

每个方案先生成一套调色盘，而不是每层独立随机。

```cpp
struct SchemePalette {
    float primaryHue;       // 主色：服装
    float secondaryHue;     // 辅色：裙摆 / 头发
    float accentHue;        // 点缀：装饰01
    float accent2Hue;       // 高亮点缀：装饰02
    float glowHue;          // 如果装饰02偏发光，可用
    MetalTone metalTone;    // 金属色：金 / 银 / 铜 / 黑铁
    StyleMood mood;         // 写实 / 暗黑 / 冷色 / 暖色 / 幻彩
};
```

推荐随机流程：

```text
生成 scheme 风格
→ 生成 SchemePalette
→ 根据 MaterialRole 给每块分配目标色
→ 每块套不同 hue/sat/lightness 范围
→ 生成当前方案的全部层效果
```

---

## 五、每块具体随机范围

### 5.1 Skin 肤色

肤色已有保护层，继续跳过。

```text
Hue: 不变
Saturation: 不变
Lightness: 不变
```

如果以后需要轻微肤色冷暖：

```text
Hue: ±2°
Saturation: -3 到 +3
Lightness: -2 到 +2
```

但默认不要动。

---

### 5.2 Hair 头发

头发不能像装饰那样乱跳。头发是角色识别点，建议低幅协调。

```text
目标色：secondaryHue 或 primaryHue 附近
Hue: base ± 10° 到 ±18°
Saturation: -15 到 +8
Lightness: -6 到 +8
```

推荐策略：

| 风格 | 头发策略 |
|---|---|
| 写实 | 保原色，只微调冷暖 |
| 暗黑 | 降饱和，偏深蓝 / 紫 / 灰 |
| 高贵 | 白发 / 银发 / 金发 |
| 幻彩 | 可偏粉 / 紫 / 青，但饱和不要太满 |

建议参数：

```text
HSL saturation: -12 到 +5
HSL lightness: -5 到 +6
Vibrance saturation: -10 到 +3
```

---

### 5.3 Clothing 服装

服装是主色核心。先定服装，其他跟它走。

```text
目标色：palette.primaryHue
Hue: primary ± 8°
Saturation: -18 到 +8
Lightness: -6 到 +6
```

服装面积通常大，不适合高饱和。高饱和会显得廉价。

推荐范围：

| 风格 | 服装参数 |
|---|---|
| Realistic | sat -18 到 -5 |
| Natural | sat -10 到 +5 |
| Dark | sat -30 到 -12，light -18 到 -6 |
| Vivid | sat 0 到 +12，但必须限制面积 |

---

### 5.4 Skirt 裙摆

裙摆也是大面积层。不要独立随机，应和服装形成统一。

两种好看模式：

#### 模式 A：同色系渐变

```text
Skirt = Clothing hue ± 10°
Skirt 比 Clothing 稍亮或稍暗
```

适合仙侠、白裙、轻盈角色。

#### 模式 B：类似色辅色

```text
Skirt = primaryHue ± 25° 到 45°
```

适合彩色皮肤。

推荐范围：

```text
Hue: secondaryHue ± 10°
Saturation: -15 到 +8
Lightness: -8 到 +10
```

注意：裙摆面积大，不能使用 `DecorSecondary` 那种高饱和点缀策略。

---

### 5.5 DecorPrimary 装饰01

装饰01做主点缀。可以比服装更鲜艳，但面积通常更小。

```text
目标色：palette.accentHue
Hue: accent ± 8° 到 ±15°
Saturation: -5 到 +18
Lightness: -4 到 +12
```

典型搭配：

```text
服装深蓝 → 装饰金色
服装黑紫 → 装饰银白
服装白粉 → 装饰玫瑰金
服装墨绿 → 装饰黄绿 / 金
```

---

### 5.6 DecorSecondary 装饰02

装饰02建议做“高亮点缀 / 宝石 / 发光点”。如果这层包含发光，使用 `glowHue`。

```text
目标色：palette.accent2Hue 或 palette.glowHue
Hue: ±5° 到 ±10°
Saturation: 0 到 +25
Lightness: +2 到 +18
```

但要限制面积。

```text
装饰02面积小 → 可高饱和 / 高亮
装饰02面积大 → 降级为 DecorPrimary
```

简单规则：

```cpp
if (decor02.alphaCoverage < 0.08f) {
    role = DecorSecondaryGlowLike;
} else {
    role = DecorPrimary;
}
```

---

### 5.7 WeaponMetal 武器金属

金属不要全 hue 随机。使用固定色池。

```cpp
enum class MetalTone {
    Gold,
    Silver,
    Copper,
    BlackIron,
    BlueSteel,
    DarkGold
};
```

推荐金属色池：

| 金属 | Hue | Sat | Light |
|---|---:|---:|---:|
| Gold | 45° | 中 | 高 |
| Silver | 任意 | 低 | 高 |
| Copper | 25° | 中 | 中 |
| BlackIron | 220° / 0° | 低 | 低 |
| BlueSteel | 210° | 低 | 中 |
| DarkGold | 42° | 中低 | 中低 |

随机策略：

```text
Hue: 固定金属 hue ±5°
Saturation: -25 到 -5
Lightness: -5 到 +10
Contrast: +5 到 +18
保高光
```

金属关键不是颜色艳，而是高光质感。

---

### 5.8 WeaponNonMetal 武器非金属

非金属部分可以跟服装或装饰统一。

推荐：

```text
70% 跟 Clothing
30% 跟 DecorPrimary
```

范围：

```text
Hue: primary/accent ±10°
Saturation: -12 到 +10
Lightness: -6 到 +8
```

如果是布条、缠带、木柄，偏低饱和：

```text
Saturation: -20 到 0
Lightness: -10 到 +4
```

---

## 六、角色只有 3 块时怎么降级

角色少的分类只有 3 块，必须做“角色数量自适应”。

常见 3 块可能是：

```text
肤色 / 主体 / 武器
```

或：

```text
肤色 / 服装 / 装饰
```

建议统一降级为：

```text
Skin
Main
Accent
```

内部映射：

| 原始块数 | 推荐角色 |
|---:|---|
| 1 | Main |
| 2 | Main + Accent |
| 3 | Skin + Main + Accent |
| 4 | Skin + Hair + Main + Accent |
| 5+ | 使用完整 7 类 |

如果没有明确武器金属 / 非金属拆分，不要强行识别金属。让武器整体走 `Accent` 或 `WeaponNonMetal`，再靠 UI 手动调整。

---

## 七、推荐做 LayerSlot，而不是只用 MaterialRole

你的分块是人工规则，建议保存两层含义：

```cpp
enum class LayerSlot {
    Skin,
    Hair,
    Decor01,
    Decor02,
    Skirt,
    Clothing,
    WeaponMetal,
    WeaponNonMetal
};

 enum class MaterialRole {
    Protected,
    Hair,
    MainColor,
    SecondaryColor,
    Accent,
    GlowAccent,
    Metal,
    WeaponOther
};
```

原因：

```text
LayerSlot = 资源分块是什么
MaterialRole = 随机时怎么处理
```

示例：

```text
装饰02 面积小 + 高亮 → GlowAccent
装饰02 面积大 → Accent
裙摆 在某些风格里 → MainColor
裙摆 在另一些风格里 → SecondaryColor
```

这样更灵活。

---

## 八、推荐工程保存格式

在项目 JSON 里保存每层 slot 和 materialRole：

```json
{
  "layers": [
    {
      "key": "skin",
      "slot": "Skin",
      "materialRole": "Protected"
    },
    {
      "key": "hair",
      "slot": "Hair",
      "materialRole": "Hair"
    },
    {
      "key": "decor_01",
      "slot": "Decor01",
      "materialRole": "Accent"
    },
    {
      "key": "decor_02",
      "slot": "Decor02",
      "materialRole": "GlowAccent"
    },
    {
      "key": "skirt",
      "slot": "Skirt",
      "materialRole": "SecondaryColor"
    },
    {
      "key": "clothing",
      "slot": "Clothing",
      "materialRole": "MainColor"
    },
    {
      "key": "weapon_metal",
      "slot": "WeaponMetal",
      "materialRole": "Metal"
    },
    {
      "key": "weapon_other",
      "slot": "WeaponNonMetal",
      "materialRole": "WeaponOther"
    }
  ]
}
```

之后随机逻辑优先使用保存的 slot，不需要每次重新猜。

---

## 九、随机时的配色分配

推荐主公式：

```text
Clothing = primary
Skirt = secondary 或 primary variation
Hair = neutral / secondary low intensity
Decor01 = accent
Decor02 = glow / accent2
WeaponMetal = metal pool
WeaponNonMetal = primary / accent
Skin = protected
```

随机逻辑应该围绕 `Clothing` 服装主色展开：

```text
先定服装色
再定裙摆色
再定装饰色
最后定武器和头发
```

不要每块独立全随机。

---

## 十、配色模板示例

### 10.1 黑金王者

| 块 | 颜色 |
|---|---|
| Skin | 不变 |
| Hair | 银白 / 暗灰 |
| Clothing | 黑 / 深蓝黑 |
| Skirt | 深紫 / 深蓝 |
| Decor01 | 金 |
| Decor02 | 暖白金光 |
| WeaponMetal | 暗金 / 黑铁 |
| WeaponNonMetal | 深蓝黑 |

### 10.2 冰霜青白

| 块 | 颜色 |
|---|---|
| Skin | 不变 |
| Hair | 白 / 冷银 |
| Clothing | 冷蓝 |
| Skirt | 青白 |
| Decor01 | 银 |
| Decor02 | 青蓝发光 |
| WeaponMetal | 银 / 蓝钢 |
| WeaponNonMetal | 深蓝 |

### 10.3 樱粉白金

| 块 | 颜色 |
|---|---|
| Skin | 不变 |
| Hair | 白粉 / 浅金 |
| Clothing | 白粉 |
| Skirt | 浅粉 / 珍珠白 |
| Decor01 | 玫瑰金 |
| Decor02 | 粉白光 |
| WeaponMetal | 金 / 玫瑰金 |
| WeaponNonMetal | 白粉 |

### 10.4 暗紫魔纹

| 块 | 颜色 |
|---|---|
| Skin | 不变 |
| Hair | 白 / 紫灰 |
| Clothing | 深紫 |
| Skirt | 紫黑 |
| Decor01 | 银 / 紫金 |
| Decor02 | 紫粉发光 |
| WeaponMetal | 黑铁 / 银 |
| WeaponNonMetal | 深紫 |

---

## 十一、实现建议：先用分块 ID

既然资源已经分块明确，第一阶段不用做复杂像素识别。

直接在导入或 `LayerTreePanel` 里给每层设置 slot：

```text
肤色 → Skin
头发 → Hair
装饰01 → Decor01
装饰02 → Decor02
裙摆 → Skirt
服装 → Clothing
武器金属 → WeaponMetal
武器非金属 → WeaponNonMetal
```

角色只有 3 块时，映射为：

```text
肤色 → Skin
主体大块 → Clothing
剩余小块 → Decor01 或 WeaponNonMetal
```

这比自动识别更稳，开发量更小。

---

## 十二、最小落地版本

### 12.1 第一版 enum

```cpp
enum class LayerSlot {
    Unknown,
    Skin,
    Hair,
    Decor01,
    Decor02,
    Skirt,
    Clothing,
    WeaponMetal,
    WeaponNonMetal
};
```

### 12.2 随机函数入口

```cpp
void randomizeStackBySlot(
    EffectStack& stack,
    LayerSlot slot,
    const SchemePalette& palette,
    const LayerStats& stats,
    quint32 seed
);
```

### 12.3 根据 slot 分派

```cpp
switch (slot) {
case LayerSlot::Skin:
    stack.reset();
    break;

case LayerSlot::Hair:
    applyHairRandom(stack, palette, stats, seed);
    break;

case LayerSlot::Clothing:
    applyMainClothingRandom(stack, palette, stats, seed);
    break;

case LayerSlot::Skirt:
    applySkirtRandom(stack, palette, stats, seed);
    break;

case LayerSlot::Decor01:
    applyDecorPrimaryRandom(stack, palette, stats, seed);
    break;

case LayerSlot::Decor02:
    applyDecorSecondaryRandom(stack, palette, stats, seed);
    break;

case LayerSlot::WeaponMetal:
    applyWeaponMetalRandom(stack, palette, stats, seed);
    break;

case LayerSlot::WeaponNonMetal:
    applyWeaponNonMetalRandom(stack, palette, stats, seed);
    break;

 default:
    applyFallbackRandom(stack, palette, stats, seed);
    break;
}
```

---

## 十三、一句话建议

你当前这套分块已经够用了。不要优先做复杂自动识别。

第一阶段最稳路线：

```text
LayerSlot 人工语义分块
→ SchemePalette 方案级调色盘
→ 每个 slot 套不同随机范围
→ 少块角色自动降级为 Skin / Main / Accent
```

这条路开发最快、误判最少、效果提升最大。

最终目标：

```text
随机参数 → 变成随机配色方案
每块乱变 → 变成服装主导、裙摆协调、装饰点缀、武器保质感
```
