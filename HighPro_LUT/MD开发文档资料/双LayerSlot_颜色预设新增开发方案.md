# HighPro_LUT — 三分类 LayerSlot 颜色开发方案

> 项目: `HighPro_LUT`  
> 文档类型: 开发前方案设计  
> 目标: 保留“部件 LayerSlot（旧逻辑）”，新增“单层颜色 LayerSlot”和“全层颜色 LayerSlot”两个颜色操作分类；颜色包含红/橙/黄/绿/青/蓝/紫/粉/黑/白/银/灰；新增逻辑不破坏现有智能随机、旧随机、方案保存、已烘焙方案等逻辑。  
jin'x> 当前阶段: 只生成方案文档，确认后再开发。

---

## 1. 核心结论

当前项目已有一套 **部件 LayerSlot**：

```text
Skin / Hair / Clothing / Skirt / Decor01 / Decor02 / WeaponMetal / WeaponNonMetal
```

它解决的是：

```text
这一层是什么部件 / 什么材质职责？
```

现在要新增一套 **颜色 LayerSlot**：

```text
红 / 橙 / 黄 / 绿 / 青 / 蓝 / 紫 / 粉 / 黑 / 白 / 银 / 灰
```

它解决的是：

```text
这一层偏向什么目标色调？
```

推荐设计：

```text
部件 LayerSlot 继续负责“怎么变”
颜色 LayerSlot 新增负责“往什么颜色变”
```

也就是双维度模型：

```text
最终随机参数 = 部件规则 × 颜色规则 × 方案调色盘
```

这样不会破坏旧逻辑。

---

## 2. 为什么不能直接把颜色塞进旧 LayerSlot

如果直接把 `Red / Orange / Yellow / ...` 追加到现有 `LayerSlot`，会产生语义混乱。

现有 `LayerSlot` 是材质/部件职责：

| 当前 LayerSlot | 含义 |
|---|---|
| `Skin` | 肤色保护 |
| `Hair` | 头发 |
| `Clothing` | 服装主体 |
| `Skirt` | 裙摆 |
| `Decor01` | 主装饰 |
| `Decor02` | 次装饰 / 发光 / 宝石 |
| `WeaponMetal` | 武器金属 |
| `WeaponNonMetal` | 武器非金属 |

新增颜色是目标色调：

| 新颜色 Slot | 含义 |
|---|---|
| `Red` | 红色系 |
| `Orange` | 橙色系 |
| `Yellow` | 黄色系 |
| `Green` | 绿色系 |
| `Cyan` | 青色系 |
| `Blue` | 蓝色系 |
| `Purple` | 紫色系 |
| `Pink` | 粉色系 |
| `Black` | 黑色系 |
| `White` | 白色系 |
| `Silver` | 银色系 |
| `Gray` | 灰色系 |

二者不是同一类东西。

如果混在一个 enum 里，会出现问题：

```text
一个层到底是 Clothing，还是 Blue？
如果选择 Blue，那它还知道自己是服装还是装饰吗？
如果选择 Clothing，那它还能指定蓝色吗？
```

所以正确方式是新增第二套字段。

---

## 3. 总体设计

### 3.1 三个右键分类

新增后 UI 右键菜单分为三个分类：

```text
A. 部件 LayerSlot（旧逻辑）
   - 控制部件/材质随机策略
   - 保留现有逻辑
   - 只作用于当前选中层

B. 单层颜色 LayerSlot
   - 控制当前选中层的目标色调
   - 只给右键点击的这个层设置颜色预设
   - 写入 layerColorSlots[layerKey]

C. 全层颜色 LayerSlot
   - 控制所有层的目标色调
   - 点击任意一个层后，右键选择颜色，即所有可设置层都写入同一个颜色预设
   - 本质是批量写入 layerColorSlots[每个 layerKey]
```

三个分类对应用户操作：

| 分类 | 作用范围 | 数据字段 | 是否旧逻辑 | 用途 |
|---|---|---|---|---|
| 部件 LayerSlot | 当前层 | `layerSlots[layerKey]` | 是 | 设置这一层是服装/裙摆/头发/装饰/武器等。 |
| 单层颜色 LayerSlot | 当前层 | `layerColorSlots[layerKey]` | 否，新增 | 只把当前层设为红/蓝/黑/白等。 |
| 全层颜色 LayerSlot | 全部层 | 批量写入 `layerColorSlots` | 否，新增 | 所有层统一设为同一颜色预设。 |

### 3.2 数据关系

单层最终有两个持久化 slot：

```cpp
LayerSlot      partSlot;   // 旧：部件 Slot
LayerColorSlot colorSlot;  // 新：颜色 Slot
```

“全层颜色 LayerSlot”不是第三个持久化字段，而是一个 **批量操作入口**：

```text
用户选择 全层颜色 LayerSlot = Blue
→ 遍历所有层
→ layerColorSlots[layer.key()] = LayerColorSlot::Blue
```

这样数据模型简单，兼容旧工程，也不会引入全局状态冲突。

概念示例：

| 操作 | 结果 |
|---|---|
| 对 `00` 设置“部件 LayerSlot → Clothing” | `00` 按服装材质规则随机。 |
| 对 `00` 设置“单层颜色 LayerSlot → Blue” | 只有 `00` 偏蓝，其它层不变。 |
| 对任意层设置“全层颜色 LayerSlot → Red” | 所有层的颜色 Slot 都变成 Red。 |
| 对任意层设置“全层颜色 LayerSlot → Auto” | 所有层颜色 Slot 清空/回 Auto，恢复旧智能随机表现。 |

### 3.3 默认兼容

新增 `LayerColorSlot::Auto`。

旧工程打开时：

```text
没有 colorSlot → 默认为 Auto
```

`Auto` 含义：

```text
完全走现有 SchemePalette / generatePalette / randomizeStackBySlot 逻辑
```

这样：

1. 旧工程不变。
2. 旧智能随机不变。
3. 旧随机不变。
4. 只有用户显式选择颜色 Slot 时，才进入新逻辑。

---

## 4. 新增枚举设计

### 4.1 推荐 enum 名称

推荐新增：

```cpp
enum class LayerColorSlot : int {
    Auto = 0,
    Red,
    Orange,
    Yellow,
    Green,
    Cyan,
    Blue,
    Purple,
    Pink,
    Black,
    White,
    Silver,
    Gray,
};
```

### 4.2 中文显示名

| 枚举 | 中文名 | UI Emoji 建议 |
|---|---|---|
| `Auto` | 自动 | 🎲 |
| `Red` | 红调 | 🔴 |
| `Orange` | 橙调 | 🟠 |
| `Yellow` | 黄调 | 🟡 |
| `Green` | 绿调 | 🟢 |
| `Cyan` | 青调 | 🧊 |
| `Blue` | 蓝调 | 🔵 |
| `Purple` | 紫调 | 🟣 |
| `Pink` | 粉调 | 🌸 |
| `Black` | 黑调 | ⚫ |
| `White` | 白调 | ⚪ |
| `Silver` | 银调 | 🪙 |
| `Gray` | 灰调 | ◻ |

说明：emoji 仅 UI 可选，不写入核心逻辑。

### 4.3 字符串保存名

JSON 保存建议使用英文稳定名：

| 枚举 | 保存字符串 |
|---|---|
| `Auto` | `Auto` |
| `Red` | `Red` |
| `Orange` | `Orange` |
| `Yellow` | `Yellow` |
| `Green` | `Green` |
| `Cyan` | `Cyan` |
| `Blue` | `Blue` |
| `Purple` | `Purple` |
| `Pink` | `Pink` |
| `Black` | `Black` |
| `White` | `White` |
| `Silver` | `Silver` |
| `Gray` | `Gray` |

读取未知字符串：

```text
返回 Auto
```

---

## 5. 颜色 Slot 不是简单 hue

颜色 Slot 不能只等于 hue。

例如：

```text
蓝调 ≠ hue 240
黑调 ≠ hue 任意 + lightness -50
白调 ≠ lightness +50
银调 ≠ 灰色
```

每个颜色 Slot 应该包含完整颜色预设：

```text
Hue 范围
Saturation 范围
Lightness / Value 范围
Brightness / Contrast 倾向
Curve 模板
ColorBalance 倾向
PhotoFilter 倾向
Vibrance 倾向
材质修正规则
```

最终建议用 `ColorPreset` 描述。

---

## 6. ColorPreset 数据模型

### 6.1 推荐结构

```cpp
struct ColorPresetDef {
    LayerColorSlot slot;
    const char* name;

    int hueCenter;
    int hueJitter;

    int satMin;
    int satMax;
    int lgtMin;
    int lgtMax;

    int brtMin;
    int brtMax;
    int ctrMin;
    int ctrMax;

    int vibranceMin;
    int vibranceMax;
    int vibSatMin;
    int vibSatMax;

    int photoFilterPreset;
    int photoDensityMin;
    int photoDensityMax;

    ColorCurveProfile curve;
    ColorBalanceProfile balance;
};
```

### 6.2 Curve Profile

```cpp
enum class ColorCurveProfile {
    None,
    DeepSafe,
    SoftContrast,
    HighlightSafe,
    MetalSafe,
    MonoDeep,
    PearlWhite,
};
```

| Profile | 用途 |
|---|---|
| `None` | 不开曲线。 |
| `DeepSafe` | 暗色，压暗但不死黑。 |
| `SoftContrast` | 普通色，轻微对比。 |
| `HighlightSafe` | 亮色，亮但不曝白。 |
| `MetalSafe` | 金属，保高光与灰阶。 |
| `MonoDeep` | 黑/灰低饱和深色。 |
| `PearlWhite` | 白/银，压顶防曝。 |

### 6.3 ColorBalance Profile

```cpp
enum class ColorBalanceProfile {
    None,
    Red,
    Orange,
    Yellow,
    Green,
    Cyan,
    Blue,
    Purple,
    Pink,
    CoolGray,
    WarmGray,
    Silver,
};
```

用于给阴影/中间调/高光加方向感。

---

## 7. 12 个颜色 LayerSlot 详细预设

以下参数是第一版推荐范围。它们是“目标方向”，开发时可按当前 `EffectStack` 字段映射。

### 7.1 红调 `Red`

定位：黑红、暗红、血红、战斗感。

| 项 | 推荐值 |
|---|---|
| Hue | 350°~10° |
| Saturation | 中高，但大面积需克制 |
| Lightness | 偏暗 / 中暗 |
| 推荐部件 | Clothing、Skirt、Decor01、WeaponNonMetal |
| 谨慎部件 | Hair、Decor02 大面积发光 |

基础参数：

```text
hueCenter: 0
hueJitter: 10
sat: -8 到 +12
lgt: -18 到 -4
brightness: -8 到 -2
contrast: +8 到 +20
vibrance: -6 到 +4
vibSat: -6 到 +4
curve: DeepSafe
photoFilter: 红 / 橙红, density 3~10
```

ColorBalance：

```text
sR +10~+18
mR +8~+16
hR +2~+8
sB +0~+6
```

部件修正：

| 部件 | 修正 |
|---|---|
| Clothing | 压暗，避免大面积鲜红。 |
| Skirt | 比 Clothing 稍亮 3~6。 |
| Decor01 | 可提高饱和 4~8。 |
| Decor02 | 可走红光，但 density 不高。 |
| Hair | 降饱和，避免红发抢戏。 |
| WeaponMetal | 不建议强制红，金属仍优先金属规则。 |

---

### 7.2 橙调 `Orange`

定位：火焰、暖金、铜色、古风。

| 项 | 推荐值 |
|---|---|
| Hue | 18°~38° |
| Saturation | 中等 |
| Lightness | 中暗到中亮 |
| 推荐部件 | Decor01、Decor02、WeaponNonMetal、Skirt |
| 谨慎部件 | Clothing 大面积橙容易土黄 |

基础参数：

```text
hueCenter: 28
hueJitter: 10
sat: -12 到 +8
lgt: -10 到 +4
brightness: -4 到 +3
contrast: +6 到 +16
vibrance: -8 到 +2
vibSat: -8 到 +2
curve: SoftContrast
photoFilter: 橙红 / 暖色 85, density 4~12
```

ColorBalance：

```text
sR +8~+14
sG +2~+6
mR +6~+12
mG +2~+6
hR +2~+6
```

部件修正：

| 部件 | 修正 |
|---|---|
| Clothing | saturation 上限降低，lightness 不超过 +2。 |
| Decor01 | 可作为金色装饰替代。 |
| Decor02 | 可做火光。 |
| WeaponMetal | 可映射到 Copper 或 Gold。 |

---

### 7.3 黄调 `Yellow`

定位：金色、圣光、黄宝石。

| 项 | 推荐值 |
|---|---|
| Hue | 45°~62° |
| Saturation | 中低到中等 |
| Lightness | 中亮，但防曝白 |
| 推荐部件 | Decor01、WeaponMetal、Decor02 |
| 谨慎部件 | Clothing 大面积黄容易廉价/曝白 |

基础参数：

```text
hueCenter: 52
hueJitter: 8
sat: -18 到 +4
lgt: -4 到 +10
brightness: 0 到 +5
contrast: +2 到 +12
vibrance: -12 到 0
vibSat: -12 到 0
curve: HighlightSafe
photoFilter: 黄 / 深黄 / 暖色 81, density 3~10
```

ColorBalance：

```text
sR +4~+10
sG +4~+10
mR +4~+10
mG +4~+10
hR +2~+8
hG +2~+8
```

部件修正：

| 部件 | 修正 |
|---|---|
| Clothing | 强制低饱和，偏象牙/暗金，不走亮黄。 |
| Skirt | 可稍亮，但最高高光压顶。 |
| Decor01 | 推荐金色点缀。 |
| WeaponMetal | 映射 `Gold` 或 `DarkGold`。 |
| Decor02 | 可做黄光，但面积大时降亮。 |

---

### 7.4 绿调 `Green`

定位：墨绿、森林、毒雾、自然。

| 项 | 推荐值 |
|---|---|
| Hue | 115°~150° |
| Saturation | 中等 |
| Lightness | 暗到中暗 |
| 推荐部件 | Clothing、Skirt、Decor01 |
| 谨慎部件 | Skin、Hair |

基础参数：

```text
hueCenter: 135
hueJitter: 18
sat: -15 到 +8
lgt: -20 到 -4
brightness: -8 到 -2
contrast: +8 到 +18
vibrance: -8 到 +3
vibSat: -8 到 +3
curve: DeepSafe
photoFilter: 绿 / 翡翠绿, density 3~10
```

ColorBalance：

```text
sG +10~+18
sB +2~+8
mG +6~+14
hG +2~+6
```

部件修正：

| 部件 | 修正 |
|---|---|
| Clothing | 推荐墨绿黑，高级稳定。 |
| Skirt | 可比 Clothing 稍亮。 |
| Decor01 | 可用暗金/浅绿点缀。 |
| Decor02 | 可变毒雾绿，但饱和不能过高。 |
| Hair | 仅轻微绿，不建议亮绿。 |

---

### 7.5 青调 `Cyan`

定位：冰、幽光、青黑、科技感。

| 项 | 推荐值 |
|---|---|
| Hue | 175°~200° |
| Saturation | 中等到中高 |
| Lightness | 中暗到中亮 |
| 推荐部件 | Decor02、Skirt、Clothing、WeaponNonMetal |
| 谨慎部件 | 大面积高亮 Clothing |

基础参数：

```text
hueCenter: 188
hueJitter: 12
sat: -12 到 +10
lgt: -12 到 +8
brightness: -4 到 +5
contrast: +4 到 +16
vibrance: -6 到 +6
vibSat: -8 到 +4
curve: SoftContrast
photoFilter: 青 / 水下 / 冷色 82, density 3~12
```

ColorBalance：

```text
sG +4~+10
sB +8~+16
mG +4~+10
mB +6~+14
hB +2~+8
```

部件修正：

| 部件 | 修正 |
|---|---|
| Clothing | 青黑更好，避免亮青大面积。 |
| Decor02 | 最适合青光。 |
| WeaponMetal | 可映射 `BlueSteel`。 |
| White/Silver 组合 | 可形成冰白蓝。 |

---

### 7.6 蓝调 `Blue`

定位：暗蓝、深海、冰蓝、夜蓝。

| 项 | 推荐值 |
|---|---|
| Hue | 215°~245° |
| Saturation | 中等 |
| Lightness | 暗到中暗，亮蓝作为少量高光 |
| 推荐部件 | Clothing、Skirt、Decor02、WeaponMetal |
| 谨慎部件 | 大面积高饱亮蓝 |

基础参数：

```text
hueCenter: 228
hueJitter: 14
sat: -14 到 +8
lgt: -20 到 -4
brightness: -8 到 -2
contrast: +8 到 +20
vibrance: -8 到 +4
vibSat: -8 到 +4
curve: DeepSafe
photoFilter: 蓝 / 深蓝 / 冷色 80, density 3~10
```

ColorBalance：

```text
sB +12~+20
mB +8~+16
hB +2~+8
sR -4~0
```

部件修正：

| 部件 | 修正 |
|---|---|
| Clothing | 最稳主色之一，推荐暗蓝黑。 |
| Skirt | 可稍亮，形成蓝色层次。 |
| Decor02 | 可冰蓝发光。 |
| WeaponMetal | 可映射 Silver / BlueSteel。 |
| Hair | 降饱，避免蓝发抢戏。 |

---

### 7.7 紫调 `Purple`

定位：暗紫、魔法、贵族、神秘。

| 项 | 推荐值 |
|---|---|
| Hue | 265°~292° |
| Saturation | 中等 |
| Lightness | 暗到中暗 |
| 推荐部件 | Clothing、Skirt、Decor01、Decor02 |
| 谨慎部件 | 粉紫亮色大面积 |

基础参数：

```text
hueCenter: 278
hueJitter: 14
sat: -14 到 +8
lgt: -20 到 -4
brightness: -8 到 -2
contrast: +8 到 +20
vibrance: -8 到 +4
vibSat: -8 到 +4
curve: DeepSafe
photoFilter: 紫 / 品红, density 3~9
```

ColorBalance：

```text
sR +6~+12
sB +12~+20
mR +4~+10
mB +8~+14
hB +2~+8
```

部件修正：

| 部件 | 修正 |
|---|---|
| Clothing | 推荐暗紫黑，不推荐亮紫糖果色。 |
| Decor02 | 可稍亮变魔法光。 |
| Hair | 低饱紫发可以保留。 |
| WeaponMetal | 推荐 DarkGold / Silver 配紫。 |

---

### 7.8 粉调 `Pink`

定位：粉、玫红、桃色、稀有色。

| 项 | 推荐值 |
|---|---|
| Hue | 315°~340° |
| Saturation | 中低到中等 |
| Lightness | 中亮但必须降饱 |
| 推荐部件 | Decor02、Decor01、Hair 少量 |
| 谨慎部件 | Clothing 大面积粉容易糖果色 |

基础参数：

```text
hueCenter: 328
hueJitter: 12
sat: -20 到 +2
lgt: -4 到 +8
brightness: -2 到 +4
contrast: +2 到 +10
vibrance: -14 到 0
vibSat: -16 到 -2
curve: SoftContrast 或 HighlightSafe
photoFilter: 品红 / 暖色 LBA, density 2~8
```

ColorBalance：

```text
sR +6~+12
mR +6~+12
mB +2~+8
hR +2~+6
```

部件修正：

| 部件 | 修正 |
|---|---|
| Clothing | 默认强制降饱、压暗，除非用户明确指定“亮粉”。 |
| Decor01 | 可做玫红点缀。 |
| Decor02 | 可做粉色发光。 |
| Hair | 可轻粉，但 lightness 不宜过高。 |

---

### 7.9 黑调 `Black`

定位：黑金、黑银、黑红、暗黑。

黑调不是纯黑。目标是：

```text
低亮度 + 保纹理 + 保边缘高光
```

| 项 | 推荐值 |
|---|---|
| Hue | 可偏冷 220° 或跟随方案 |
| Saturation | 极低 |
| Lightness | 暗，但不死黑 |
| 推荐部件 | Clothing、Skirt、WeaponMetal、Decor01 |
| 谨慎部件 | Decor02 发光不能纯黑 |

基础参数：

```text
hueCenter: 220
hueJitter: 20
sat: -65 到 -35
lgt: -30 到 -12
brightness: -14 到 -5
contrast: +12 到 +26
vibrance: -25 到 -10
vibSat: -30 到 -12
curve: MonoDeep
photoFilter: 深蓝 / 冷色 80, density 0~5
```

推荐曲线：

```text
{0, 10}
{32, 20}
{80, 58}
{128, 105}
{190, 190}
{230, 228}
{255, 246}
```

部件修正：

| 部件 | 修正 |
|---|---|
| Clothing | 最适合黑色主体。 |
| Skirt | 比 Clothing 稍亮 3~8。 |
| Decor01 | 黑色装饰需保高光。 |
| Decor02 | 黑调不能用于发光，自动改为暗蓝/暗紫微光。 |
| WeaponMetal | 映射 BlackIron。 |

---

### 7.10 白调 `White`

定位：白金、冰白、象牙白、圣洁。

白调不是纯白。目标是：

```text
低饱和 + 高中间调 + 高光压顶
```

| 项 | 推荐值 |
|---|---|
| Hue | 暖白 45° / 冷白 210° / 绿白 120° |
| Saturation | 低 |
| Lightness | 中高，防曝 |
| 推荐部件 | Clothing、Skirt、Decor01、Hair 少量 |
| 谨慎部件 | 大面积高光曝白 |

基础参数：

```text
hueCenter: 210 或 45
hueJitter: 12
sat: -55 到 -25
lgt: +6 到 +18
brightness: +1 到 +6
contrast: -4 到 +8
vibrance: -20 到 -8
vibSat: -25 到 -10
curve: PearlWhite
photoFilter: 冷色 82 / 暖色 81, density 2~8
```

推荐曲线：

```text
{0, 12}
{48, 48}
{128, 140}
{200, 215}
{240, 238}
{255, 246}
```

部件修正：

| 部件 | 修正 |
|---|---|
| Clothing | 白色大面积必须强降饱和。 |
| Skirt | 可以比 Clothing 更亮一点。 |
| Decor02 | 白光需压 density，防糊。 |
| WeaponMetal | 不等于银，金属仍走 Silver。 |

---

### 7.11 银调 `Silver`

定位：银甲、冷金属、冰银、高级灰白。

银调和白调不同：

```text
银 = 冷灰 + 高光 + 金属对比
白 = 低饱高亮布料/衣物
```

| 项 | 推荐值 |
|---|---|
| Hue | 205°~225° 冷灰蓝 |
| Saturation | 极低到低 |
| Lightness | 中亮 |
| 推荐部件 | WeaponMetal、Decor01、Decor02、Clothing 局部 |
| 谨慎部件 | Skin |

基础参数：

```text
hueCenter: 215
hueJitter: 10
sat: -50 到 -25
lgt: -2 到 +10
brightness: 0 到 +5
contrast: +8 到 +20
vibrance: -18 到 -6
vibSat: -22 到 -8
curve: MetalSafe
photoFilter: 冷色 80 / 冷色 LBB, density 2~8
```

ColorBalance：

```text
sB +4~+10
mB +3~+8
hB +2~+8
整体 preserveLuma = true
```

部件修正：

| 部件 | 修正 |
|---|---|
| WeaponMetal | 首选映射 `MetalTone::Silver`。 |
| Decor01 | 银色边饰高级稳定。 |
| Clothing | 大面积银布应降对比，避免像金属皮肤。 |
| Decor02 | 可做冰银光。 |

---

### 7.12 灰调 `Gray`

定位：灰、冷灰、暖灰、黑白灰高级色。

灰调比黑调亮，比银调不那么金属。

| 项 | 推荐值 |
|---|---|
| Hue | 210° 冷灰或 35° 暖灰 |
| Saturation | 很低 |
| Lightness | 中暗到中亮 |
| 推荐部件 | Clothing、Skirt、Decor01、Hair |
| 谨慎部件 | Decor02 发光 |

基础参数：

```text
hueCenter: 210
hueJitter: 18
sat: -60 到 -35
lgt: -10 到 +8
brightness: -4 到 +4
contrast: +4 到 +14
vibrance: -22 到 -8
vibSat: -28 到 -10
curve: SoftContrast 或 MonoDeep
photoFilter: 冷色 80 / 棕褐, density 0~6
```

ColorBalance：

```text
冷灰: sB +4~+8, mB +2~+6
暖灰: sR +2~+6, mR +2~+6
```

部件修正：

| 部件 | 修正 |
|---|---|
| Clothing | 可做黑白灰高级方案。 |
| Skirt | 可稍亮。 |
| Hair | 灰发可保留。 |
| Decor02 | 不建议纯灰发光，自动转银/白微光。 |

---

## 8. 三个分类下面都可选不同颜色预设

用户最新要求：

```text
1. 部件 LayerSlot（旧逻辑）
2. 单层颜色 LayerSlot（点击层级后，右键添加，只给选择的层级添加）
3. 全层颜色 LayerSlot（点击任意层级，右键添加，所有层级都添加同一个颜色预设）
```

三类都放在 LayerTree 右键菜单中，且互不破坏。

### 8.1 第一类：部件 LayerSlot（旧逻辑）

部件 LayerSlot 只对当前右键点击的层生效，保留现有旧逻辑。

| 部件 LayerSlot | 推荐预设 |
|---|---|
| `Skin` | 默认肤色 / 浅肤 / 深肤 / 冷白肤 / 暖肤 / 不变色 |
| `Hair` | 黑发 / 白发 / 银发 / 蓝黑 / 紫黑 / 红棕 / 粉发低饱 |
| `Clothing` | 暗蓝黑 / 暗紫黑 / 黑红 / 墨绿黑 / 黑白灰 / 冰蓝白 / 白金 / 稀有彩色 |
| `Skirt` | 跟服装 / 稍亮主色 / 辅色 / 白边 / 暗边 / 渐变感 |
| `Decor01` | 金 / 银 / 铜 / 红宝石 / 蓝宝石 / 绿宝石 / 紫宝石 |
| `Decor02` | 青光 / 蓝光 / 紫光 / 金光 / 红光 / 白光 / 低亮发光 |
| `WeaponMetal` | 金 / 银 / 铜 / 黑铁 / 蓝钢 / 暗金 |
| `WeaponNonMetal` | 跟服装 / 跟装饰 / 皮革棕 / 黑红 / 暗蓝 |

### 8.2 第二类：单层颜色 LayerSlot

单层颜色 LayerSlot 只对当前右键点击的层生效。

操作流程：

```text
点击 / 右键某个层
→ 单层颜色 LayerSlot
→ 选择 红/橙/黄/绿/青/蓝/紫/粉/黑/白/银/灰/自动
→ 只写入这个 layerKey 的 layerColorSlots
```

示例：

```text
右键 00 → 单层颜色 LayerSlot → 蓝调
结果：只有 00 = Blue，其它层保持原颜色 Slot
```

颜色 LayerSlot 下选择的是全局色系预设。

| 颜色 LayerSlot | 推荐预设 |
|---|---|
| 红调 | 黑红 / 暗红 / 朱红 / 熔岩红 / 红宝石 |
| 橙调 | 暖橙 / 铜橙 / 火焰橙 / 暗橙 |
| 黄调 | 暗金 / 明金 / 圣光黄 / 砂金 |
| 绿调 | 墨绿 / 森绿 / 毒绿 / 翡翠绿 |
| 青调 | 青黑 / 冰青 / 幽光青 / 水下青 |
| 蓝调 | 暗蓝 / 深海蓝 / 冰蓝 / 宝石蓝 |
| 紫调 | 暗紫 / 蓝紫 / 魔纹紫 / 贵族紫 |
| 粉调 | 玫红 / 淡粉 / 桃粉 / 粉紫 |
| 黑调 | 黑银 / 黑金 / 黑红 / 黑紫 / 纯黑质感 |
| 白调 | 冰白 / 象牙白 / 白金 / 白绿 / 珍珠白 |
| 银调 | 冷银 / 蓝银 / 黑银 / 亮银 |
| 灰调 | 冷灰 / 暖灰 / 深灰 / 浅灰 / 黑白灰 |

### 8.3 第三类：全层颜色 LayerSlot

全层颜色 LayerSlot 是批量操作，不新增独立数据字段。

操作流程：

```text
点击 / 右键任意一个层
→ 全层颜色 LayerSlot
→ 选择 红/橙/黄/绿/青/蓝/紫/粉/黑/白/银/灰/自动
→ 遍历所有层
→ 所有 layerColorSlots[layerKey] 都写入这个颜色
```

示例：

```text
右键任意层 → 全层颜色 LayerSlot → 黑调
结果：所有层 colorSlot = Black
```

全层颜色适合快速测试：

1. 全角色统一蓝调。
2. 全角色统一黑调。
3. 全角色统一白调。
4. 全角色统一灰调。
5. 一键回 Auto，恢复旧智能随机。

建议全层操作默认包含所有层，但 `Skin` 最终仍由部件规则保护，不会被染色。

### 8.4 组合示例

| 部件 Slot | 颜色 Slot | 部件预设 | 颜色预设 | 输出倾向 |
|---|---|---|---|---|
| Clothing | Blue | 暗色主体 | 暗蓝 | 暗蓝黑服装。 |
| Clothing | White | 高级亮色 | 冰白 | 冰蓝白服装。 |
| Decor01 | Yellow | 金属装饰 | 暗金 | 暗金边饰。 |
| Decor02 | Cyan | 发光点 | 幽光青 | 青色发光。 |
| WeaponMetal | Silver | 金属 | 冷银 | 银色武器。 |
| Hair | Gray | 头发 | 冷灰 | 灰白头发。 |

---

## 9. 与现有 SchemePalette 的关系

当前智能随机流程：

```text
generatePalette()
→ SchemePalette
→ randomizeStackBySlot(partSlot, palette)
```

新增颜色 Slot 后推荐流程：

```text
generatePalette()
→ SchemePalette
→ resolveColorIntent(colorSlot, palette)
→ randomizeStackByPartAndColor(partSlot, colorIntent, palette)
```

### 9.1 Auto 模式

当 `colorSlot == Auto`：

```text
不生成 colorIntent
直接走旧 randomizeStackBySlot(partSlot, palette)
```

这保证 100% 兼容旧逻辑。

### 9.2 指定颜色模式

当 `colorSlot != Auto`：

```text
按 colorSlot 生成 ColorIntent
再让 partSlot 消费 ColorIntent
```

示例：

```text
partSlot = Clothing
colorSlot = Blue
→ 生成蓝色 ColorIntent
→ applyClothing 使用蓝色目标，但保留服装暗调/曲线/降饱策略
```

### 9.3 SchemePalette 仍有价值

即使指定颜色 Slot，`SchemePalette` 仍用于：

1. 控制整体亮度档。
2. 控制整体饱和档。
3. 提供金属倾向。
4. 提供 accent / glow 的备用方向。
5. 用于 `Auto` 层与指定颜色层之间保持一致。

---

## 10. 推荐新增 ColorIntent

### 10.1 结构

```cpp
struct ColorIntent {
    LayerColorSlot slot = LayerColorSlot::Auto;

    int hueAbs = 0;
    int hueJitter = 0;

    int satLo = 0;
    int satHi = 0;
    int lgtLo = 0;
    int lgtHi = 0;

    int brtLo = 0;
    int brtHi = 0;
    int ctrLo = 0;
    int ctrHi = 0;

    ColorCurveProfile curve = ColorCurveProfile::None;
    ColorBalanceProfile balance = ColorBalanceProfile::None;

    int photoPreset = -1;
    int photoDensityLo = 0;
    int photoDensityHi = 0;

    bool isMonoLike = false;
    bool isMetalLike = false;
    bool isLightLike = false;
    bool isDarkLike = false;
};
```

### 10.2 生成函数

```cpp
ColorIntent resolveColorIntent(LayerColorSlot colorSlot,
                               const SchemePalette& palette,
                               quint32 seed = 0);
```

规则：

| colorSlot | 行为 |
|---|---|
| `Auto` | 返回空 intent 或不调用。 |
| 彩色 Slot | 从固定 hueCenter + jitter 生成。 |
| 黑/白/银/灰 | 走低饱 / 亮度 / 曲线特殊逻辑。 |

---

## 11. 新随机函数设计

### 11.1 保留旧函数

旧函数不删：

```cpp
void randomizeStackBySlot(EffectStack& s,
                          LayerSlot slot,
                          const SchemePalette& palette,
                          quint32 seed = 0);
```

继续服务：

1. 旧工程。
2. `colorSlot == Auto`。
3. 没有颜色控制需求的层。
4. 当前智能随机默认逻辑。

### 11.2 新增重载

新增：

```cpp
void randomizeStackBySlot(EffectStack& s,
                          LayerSlot partSlot,
                          LayerColorSlot colorSlot,
                          const SchemePalette& palette,
                          quint32 seed = 0);
```

内部逻辑：

```cpp
if (colorSlot == LayerColorSlot::Auto) {
    randomizeStackBySlot(s, partSlot, palette, seed);
    return;
}

ColorIntent intent = resolveColorIntent(colorSlot, palette, seed);
randomizeStackByPartAndColor(s, partSlot, intent, palette, seed);
```

### 11.3 新增内部函数

```cpp
void applyClothingWithColor(EffectStack& s, const ColorIntent& c, const SchemePalette& p, Rng& rng);
void applySkirtWithColor(EffectStack& s, const ColorIntent& c, const SchemePalette& p, Rng& rng);
void applyHairWithColor(EffectStack& s, const ColorIntent& c, const SchemePalette& p, Rng& rng);
void applyDecor01WithColor(EffectStack& s, const ColorIntent& c, const SchemePalette& p, Rng& rng);
void applyDecor02WithColor(EffectStack& s, const ColorIntent& c, const SchemePalette& p, Rng& rng);
void applyWeaponMetalWithColor(EffectStack& s, const ColorIntent& c, const SchemePalette& p, Rng& rng);
void applyWeaponNonMetalWithColor(EffectStack& s, const ColorIntent& c, const SchemePalette& p, Rng& rng);
```

第一版也可以简化：

```cpp
先调用现有 applyXxx()
再用 applyColorIntentPatch() 覆盖 hue/sat/lgt/曲线/滤镜
```

但更推荐直接写 `WithColor`，参数更可控。

---

## 12. 部件 × 颜色的优先级规则

最终参数冲突时，按以下优先级：

```text
安全保护 > 部件材质规则 > 颜色目标 > 方案氛围 > 随机 jitter
```

### 12.1 安全保护最高

永远优先：

1. `Skin` 不变色。
2. `shadowProtectThreshold` 保留。
3. 黑位不死黑。
4. 白位不曝白。
5. 已锁方案不随机。
6. 已烘焙方案按当前 includeBaked 逻辑处理。

### 12.2 部件材质第二

颜色不能破坏材质。

示例：

| 部件 | 颜色 Slot | 处理 |
|---|---|---|
| `WeaponMetal` | `Pink` | 不直接变亮粉金属；转为低饱玫瑰金或提示不推荐。 |
| `Decor02` | `Black` | 发光不能黑；转暗蓝/暗紫微光。 |
| `Clothing` | `Yellow` | 不出大面积亮黄；转暗金/象牙黄。 |
| `Hair` | `Red` | 不强红；走低饱红棕。 |

### 12.3 颜色目标第三

颜色 Slot 决定目标色系。

示例：

```text
Clothing + Blue → 暗蓝/深蓝/冰蓝
Clothing + White → 白金/冰白/象牙白
Decor02 + Cyan → 青光
WeaponMetal + Silver → 银金属
```

### 12.4 SchemePalette 第四

`SchemePalette` 决定整体亮度/饱和氛围。

例如同样 `Blue`：

| Scheme 亮度档 | 输出 |
|---|---|
| UltraDark | 蓝黑 |
| Dark | 暗蓝 |
| Mid | 中蓝 |
| Bright | 冰蓝 |
| UltraBright | 冰白蓝 |

---

## 13. JSON 保存设计

### 13.1 Project 新字段

现有：

```cpp
QHash<QString, LayerSlot> layerSlots;
```

新增：

```cpp
QHash<QString, LayerColorSlot> layerColorSlots;
```

### 13.2 JSON 示例

```json
{
  "layerSlots": {
    "num_00": "Clothing",
    "num_01": "Skirt",
    "num_02": "Hair",
    "num_03": "Decor01",
    "addon_00": "Decor02"
  },
  "layerColorSlots": {
    "num_00": "Blue",
    "num_01": "White",
    "num_02": "Gray",
    "num_03": "Yellow",
    "addon_00": "Cyan"
  }
}
```

### 13.3 兼容策略

读取逻辑：

```text
没有 layerColorSlots → 全部 Auto
layerColorSlots 中没有某层 → 该层 Auto
未知字符串 → Auto
```

保存逻辑：

```text
可只保存非 Auto 项
也可保存全部项
```

推荐只保存非 Auto 项，工程 JSON 更干净。

---

## 14. UI 设计方案

### 14.1 LayerTree 右键菜单

右键菜单改为三个分类：

```text
部件 LayerSlot（旧逻辑）
- 自动 / Unknown
- Skin
- Hair
- Clothing
- Skirt
- Decor01
- Decor02
- WeaponMetal
- WeaponNonMetal

单层颜色 LayerSlot
- 自动
- 红调
- 橙调
- 黄调
- 绿调
- 青调
- 蓝调
- 紫调
- 粉调
- 黑调
- 白调
- 银调
- 灰调

全层颜色 LayerSlot
- 全部自动
- 全部红调
- 全部橙调
- 全部黄调
- 全部绿调
- 全部青调
- 全部蓝调
- 全部紫调
- 全部粉调
- 全部黑调
- 全部白调
- 全部银调
- 全部灰调
```

三个分类行为：

| 菜单 | 作用范围 | 写入 |
|---|---|---|
| 部件 LayerSlot（旧逻辑） | 当前右键层 | `layerSlots[currentLayerKey]` |
| 单层颜色 LayerSlot | 当前右键层 | `layerColorSlots[currentLayerKey]` |
| 全层颜色 LayerSlot | 所有层 | `layerColorSlots[eachLayerKey]` |

右键点任意层都可以使用“全层颜色 LayerSlot”。该操作不依赖当前层，只把当前层当作菜单触发位置。

建议“全层颜色 LayerSlot”执行前不弹窗，因为它可通过“全部自动”恢复，也可用项目撤销栈恢复。如果担心误操作，可在开发时接入现有 undo snapshot。

### 14.2 显示前缀

当前 LayerTree 可能显示部件 emoji。

新增后推荐显示格式：

```text
[部件Emoji][颜色Emoji] layerName
```

示例：

```text
👕 🔵 00
👗 ⚪ 01
💎 🟡 03
✨ 🧊 addon/00
```

如果颜色为 Auto，不显示颜色 emoji，避免 UI 过乱。

### 14.3 Tooltip

Tooltip 推荐：

```text
部件: 服装主体
颜色: 蓝调
智能随机: 按服装暗色规则生成蓝色系方案
```

### 14.4 EffectPanel 新增信息

当前方案/当前层区域可显示：

```text
部件 Slot: Clothing
单层颜色 Slot: Blue
```

也可加两个下拉框：

```text
部件类型: [Clothing ▼]
单层颜色: [Blue ▼]
```

第一版推荐只放 LayerTree 右键，减少 UI 改动。

### 14.5 单层/全层颜色写入规则

单层颜色：

```cpp
void ProjectController::setLayerColorSlot(const QString& layerKey,
                                          LayerColorSlot slot)
{
    if (slot == LayerColorSlot::Auto) {
        m_project.layerColorSlots.remove(layerKey);
    } else {
        m_project.layerColorSlots[layerKey] = slot;
    }
}
```

全层颜色：

```cpp
void ProjectController::setAllLayerColorSlots(LayerColorSlot slot)
{
    if (slot == LayerColorSlot::Auto) {
        m_project.layerColorSlots.clear();
        return;
    }

    for (const auto& layer : m_project.layers) {
        m_project.layerColorSlots[layer.key()] = slot;
    }
}
```

注意：

```text
全层颜色只写 layerColorSlots。
不改 layerSlots。
不改 skinSafeLayerKeys。
不改 EffectStack。
用户再次点击智能随机后，颜色 Slot 才影响生成结果。
```

---

## 15. 智能随机按钮行为调整

### 15.1 智能当前层

旧逻辑：

```text
partSlot = slotFor(layer)
randomizeStackBySlot(stack, partSlot, palette)
```

新逻辑：

```text
partSlot = slotFor(layer)
colorSlot = colorSlotFor(layer)
randomizeStackBySlot(stack, partSlot, colorSlot, palette)
```

若 `colorSlot == Auto`，结果等同旧逻辑。

### 15.2 智能所有层

每层使用自己的颜色 Slot：

```text
00 Clothing + Blue
01 Skirt + White
02 Hair + Gray
03 Decor01 + Yellow
addon Decor02 + Cyan
```

### 15.3 智能可编辑 / 智能全部

同上，循环方案时每层读取自己的 `layerColorSlots`。

### 15.4 旧随机按钮不改

旧随机 `randomizeStack()` 不读取 `LayerColorSlot`。

原因：

1. 旧随机就是参数随机。
2. 保持旧功能含义。
3. 避免“随机当前层”突然受颜色 Slot 影响。

若后续需要，可新增按钮：

```text
按颜色随机
```

但第一版不建议。

---

## 16. 颜色预设与 27 方案的关系

当前项目已有 `kSchemeStyles` 27 套参考方案，也有动态 `generatePalette()`。

新增颜色 Slot 后，有两种模式：

### 16.1 手动指定层颜色

用户给层设置颜色：

```text
00 = Clothing + Blue
01 = Skirt + White
03 = Decor01 + Yellow
addon = Decor02 + Cyan
```

此时每次智能随机仍有变化，但色系稳定。

### 16.2 方案预设批量设置层颜色

后续可新增“配色方案模板”，一次性设置多个层的颜色 Slot。

示例：暗蓝银模板：

| 层 | 部件 | 颜色 |
|---|---|---|
| 00 | Clothing | Blue |
| 01 | Skirt | Blue |
| 02 | Hair | Gray |
| 03 | Decor01 | Silver |
| 04 | Decor02 | Cyan |
| WeaponMetal | WeaponMetal | Silver |

示例：黑红金模板：

| 层 | 部件 | 颜色 |
|---|---|---|
| 00 | Clothing | Black |
| 01 | Skirt | Red |
| 02 | Hair | Black |
| 03 | Decor01 | Yellow |
| 04 | Decor02 | Red |
| WeaponMetal | WeaponMetal | Yellow |

---

## 17. 推荐新增配色方案模板

这些模板不替代 `kSchemeStyles`，而是作为 “部件 Slot + 颜色 Slot” 的组合预设。

### 17.1 暗蓝银

| 部件 | 颜色 |
|---|---|
| Clothing | Blue |
| Skirt | Blue |
| Hair | Gray |
| Decor01 | Silver |
| Decor02 | Cyan |
| WeaponMetal | Silver |
| WeaponNonMetal | Blue |

视觉：深海 / 夜蓝 / 银边 / 青光。

### 17.2 黑红金

| 部件 | 颜色 |
|---|---|
| Clothing | Black |
| Skirt | Red |
| Hair | Black |
| Decor01 | Yellow |
| Decor02 | Red |
| WeaponMetal | Yellow |
| WeaponNonMetal | Red |

视觉：战斗 / 魔化 / 火焰 / 黑金。

### 17.3 墨绿金

| 部件 | 颜色 |
|---|---|
| Clothing | Green |
| Skirt | Black |
| Hair | Gray |
| Decor01 | Yellow |
| Decor02 | Green |
| WeaponMetal | Yellow |
| WeaponNonMetal | Green |

视觉：森林 / 毒雾 / 古风。

### 17.4 暗紫银

| 部件 | 颜色 |
|---|---|
| Clothing | Purple |
| Skirt | Black |
| Hair | Gray |
| Decor01 | Silver |
| Decor02 | Purple |
| WeaponMetal | Silver |
| WeaponNonMetal | Purple |

视觉：魔法 / 贵族 / 夜色。

### 17.5 冰蓝白

| 部件 | 颜色 |
|---|---|
| Clothing | White |
| Skirt | Blue |
| Hair | White |
| Decor01 | Silver |
| Decor02 | Cyan |
| WeaponMetal | Silver |
| WeaponNonMetal | Blue |

视觉：冰霜 / 冷白 / 高级亮色。

### 17.6 黑白灰

| 部件 | 颜色 |
|---|---|
| Clothing | Black |
| Skirt | White |
| Hair | Gray |
| Decor01 | Silver |
| Decor02 | White |
| WeaponMetal | Silver |
| WeaponNonMetal | Gray |

视觉：高级灰阶 / 黑银 / 极简。

### 17.7 白绿金

| 部件 | 颜色 |
|---|---|
| Clothing | White |
| Skirt | Green |
| Hair | Gray |
| Decor01 | Yellow |
| Decor02 | Green |
| WeaponMetal | Yellow |
| WeaponNonMetal | Green |

视觉：清冷自然 / 圣洁 / 白绿。

### 17.8 粉紫银

| 部件 | 颜色 |
|---|---|
| Clothing | Purple |
| Skirt | Pink |
| Hair | Pink |
| Decor01 | Silver |
| Decor02 | Pink |
| WeaponMetal | Silver |
| WeaponNonMetal | Purple |

视觉：稀有色 / 梦幻 / 女性向。大面积必须降饱和。

---

## 18. 开发落地文件清单

### 18.1 新增/修改核心文件

| 文件 | 改动 |
|---|---|
| `src/core/LayerData.h` | 新增 `LayerColorSlot` enum 与 helper 声明。 |
| `src/core/LayerData.cpp` | 新增 `layerColorSlotToString/fromString/displayName/emoji`。 |
| `src/core/Project.h` | 新增 `QHash<QString, LayerColorSlot> layerColorSlots`，新增 `colorSlotFor()`。 |
| `src/core/ColorEffect.h` | 新增带 `LayerColorSlot` 的 `randomizeStackBySlot` 重载声明。 |
| `src/core/ColorEffect.cpp` | 新增 `ColorIntent`、`resolveColorIntent`、各部件 WithColor 策略。 |
| `src/app/ProjectIO.cpp` | 保存/读取 `layerColorSlots`。 |
| `src/app/ProjectController.h` | 新增设置颜色 Slot 的接口。 |
| `src/app/ProjectController.cpp` | 智能随机入口读取 `colorSlotFor()`。 |
| `src/ui/LayerTreePanel.cpp` | 右键菜单新增颜色类型选择，显示颜色 emoji。 |
| `src/ui/EffectPanel.cpp` | 可选显示当前层部件/颜色 Slot。 |

### 18.2 不建议改动的文件

| 文件 | 原因 |
|---|---|
| `assets/shaders/effect_chain.hlsl` | 颜色 Slot 最终仍转为现有 `EffectStack`，shader 不需要改。 |
| `src/render/LutBaker.cpp` | 烘焙使用 `EffectStack`，不需要知道 Slot。 |
| `src/render/FrameRenderer.cpp` | 渲染只吃最终效果，不需要知道 Slot。 |
| `src/core/SchemePalette.cpp` | 第一版可不改，保留现有 palette 生成。 |

---

## 19. 兼容性设计

### 19.1 旧工程兼容

旧工程没有 `layerColorSlots`。

读取后：

```text
所有层 colorSlot = Auto
```

效果：

```text
智能随机表现与现在完全一致
```

### 19.2 旧随机兼容

`randomizeStack()` 不改。

所有旧随机按钮：

```text
随机当前层
随机所有层
随机可编辑
随机全部
```

行为保持不变。

### 19.3 已烘焙方案兼容

已烘焙方案仍由 `isBaked` 和 `layerLutPath` 控制。

颜色 Slot 不影响已烘焙显示。

只有当用户执行包含已烘焙的“全部”类随机时，仍按当前逻辑降级为可编辑。

### 19.4 Skin 保护兼容

无论颜色 Slot 是什么：

```text
partSlot == Skin 或 skinSafeLayerKeys 命中 → 不变色
```

即：

```text
Skin + Red = Skin，不变色
Skin + Blue = Skin，不变色
```

---

## 20. 第一版开发优先级

### P0：数据结构与兼容

1. 新增 `LayerColorSlot`。
2. 新增字符串转换 / 中文显示 / emoji。
3. `Project` 新增 `layerColorSlots`。
4. `ProjectIO` 保存读取。
5. 旧工程默认 Auto。

### P1：智能随机接入

1. 新增 `randomizeStackBySlot(partSlot, colorSlot, palette)` 重载。
2. `Auto` 直接回退旧逻辑。
3. 指定颜色时生成 `ColorIntent`。
4. 先实现 12 色基础参数。
5. 智能当前层 / 智能所有层 / 智能可编辑 / 智能全部接入。

### P2：UI

1. LayerTree 右键菜单保留“部件 LayerSlot（旧逻辑）”。
2. LayerTree 右键菜单新增“单层颜色 LayerSlot”。
3. LayerTree 右键菜单新增“全层颜色 LayerSlot”。
4. 层名前显示颜色 emoji。
5. Tooltip 显示部件 + 单层颜色。
6. 支持单层清除颜色 Slot 回 Auto。
7. 支持全层一键回 Auto。

### P3：配色模板

1. 新增 8 个模板：暗蓝银、黑红金、墨绿金、暗紫银、冰蓝白、黑白灰、白绿金、粉紫银。
2. 模板可批量设置 `layerColorSlots`。
3. 不直接修改 `EffectStack`，只改变 slot，用户再点智能随机生效。

---

## 21. 推荐第一版最小实现

为了不破坏旧逻辑，第一版建议只做以下功能：

```text
1. 新增 LayerColorSlot enum
2. 保存/读取 layerColorSlots
3. LayerTree 右键保留部件 LayerSlot（旧逻辑）
4. LayerTree 右键新增单层颜色 LayerSlot
5. LayerTree 右键新增全层颜色 LayerSlot
6. 智能随机读取颜色 Slot
7. Auto 回退旧逻辑
8. 实现 12 色基础 ColorIntent
```

暂不做：

```text
1. 颜色预设编辑器
2. 自定义颜色库
3. 像素主色分析
4. 方案模板批量应用 UI
5. Shader 改动
```

这样开发风险最低。

---

## 22. 推荐代码伪流程

### 22.1 Project 读取 slot

```cpp
LayerColorSlot Project::colorSlotFor(const LayerData& l) const
{
    return layerColorSlots.value(l.key(), LayerColorSlot::Auto);
}
```

### 22.2 智能当前层

```cpp
const LayerSlot partSlot = m_project.slotFor(*layer);
const LayerColorSlot colorSlot = m_project.colorSlotFor(*layer);

randomizeStackBySlot(sc->layerEffects[k], partSlot, colorSlot, palette);
```

### 22.3 新随机重载

```cpp
void randomizeStackBySlot(EffectStack& s,
                          LayerSlot partSlot,
                          LayerColorSlot colorSlot,
                          const SchemePalette& palette,
                          quint32 seed)
{
    if (colorSlot == LayerColorSlot::Auto) {
        randomizeStackBySlot(s, partSlot, palette, seed);
        return;
    }

    Rng rng(seed);
    ColorIntent intent = resolveColorIntent(colorSlot, palette, rng.nextSeed());
    applyPartWithColor(s, partSlot, intent, palette, rng);
}
```

### 22.4 Skin 优先

```cpp
if (partSlot == LayerSlot::Skin) {
    applySkinReset(s);
    return;
}
```

---

## 23. 参数安全底线

无论颜色 Slot 怎么指定，必须遵守：

| 目标 | 规则 |
|---|---|
| 防死黑 | 暗色曲线黑位输出不低于 8~10。 |
| 防曝白 | 白/银/黄高光输出最高 246~248。 |
| 防糖果色 | Clothing 的 Pink/Yellow/Cyan/Blue 高亮必须降饱。 |
| 防发光脏 | Decor02 的 Black/Gray 自动转暗蓝/银白微光。 |
| 防肤色污染 | Skin 永远不受颜色 Slot 影响。 |
| 防金属丢质感 | WeaponMetal 优先映射 Gold/Silver/Copper/BlackIron/BlueSteel/DarkGold。 |

---

## 24. 测试方案

### 24.1 兼容测试

| 测试 | 预期 |
|---|---|
| 打开旧工程 | 不崩溃，所有颜色 Slot 为 Auto。 |
| 旧工程点智能随机 | 效果与当前版本一致。 |
| 旧工程保存再打开 | 正常。 |
| 没有 `layerColorSlots` 字段 | 正常默认 Auto。 |
| 未知颜色字符串 | 自动变 Auto。 |

### 24.2 功能测试

| 测试 | 预期 |
|---|---|
| `Clothing + Blue` | 服装偏蓝，不变成装饰发光逻辑。 |
| `Decor02 + Cyan` | 发光偏青，亮度可控。 |
| `WeaponMetal + Silver` | 金属偏银，保高光。 |
| `Clothing + Pink` | 粉色低饱，不大面积糖果。 |
| `Clothing + Black` | 黑但不死黑。 |
| `Clothing + White` | 白但不曝白。 |
| `Skin + Red` | 肤色不变。 |

### 24.3 批量测试

一次生成 21 个方案：

```text
00 Clothing 分别测试 12 色
01 Skirt Auto / White / Blue
03 Decor01 Yellow / Silver
addon Decor02 Cyan / Purple / White
```

检查：

1. 整体不乱。
2. 色调能识别。
3. 暗色保细节。
4. 亮色不曝白。
5. 发光不污染大面积服装。

---

## 25. 最终建议

推荐采用：

```text
三分类 LayerSlot 操作模型
```

即：

```text
部件 LayerSlot：保留旧逻辑，决定部件材质随机策略
单层颜色 LayerSlot：新增字段，决定当前层目标色调
全层颜色 LayerSlot：新增批量操作入口，把同一颜色预设写入全部层
```

第一版开发目标：

```text
只新增，不替换
Auto 完全回退旧逻辑
智能随机才读取颜色 Slot
旧随机不受影响
Shader / LUT / 渲染不改
```

这样可以满足：

1. 新增红橙黄绿青蓝紫粉黑白银灰调。
2. 每层可选部件类型和单层颜色类型。
3. 可通过全层颜色入口一键给所有层设置同一颜色预设。
4. 每类下面都能扩展颜色预设。
5. 不破坏现有随机、智能随机、烘焙、工程保存逻辑。
6. 后续可继续扩展配色模板和自定义颜色库。

---

*文档生成时间: 2026-06-05*
