# LayerColorSlot 颜色实现逻辑说明

> 范围：`单层颜色 LayerSlot` / `全层颜色 LayerSlot` 右键菜单，以及 `LayerColorSlot` 对智能随机变色结果的影响。

## 1. 新增枚举

位置：`src/core/LayerData.h`

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

含义：

- `Auto`：不指定目标颜色，完全走旧智能随机逻辑。
- `Red` ~ `Gray`：指定目标色调，智能随机时在原 `LayerSlot` 部件逻辑基础上追加颜色覆盖。

## 2. 为什么新增 `LayerColorSlot`

原 `LayerSlot` 负责“层是什么”：

| `LayerSlot` | 语义 |
|---|---|
| `Skin` | 肤色 |
| `Hair` | 头发 |
| `Clothing` | 服装 |
| `Skirt` | 裙摆 |
| `Decor01` / `Decor02` | 装饰 / 发光 |
| `WeaponMetal` / `WeaponNonMetal` | 武器材质 |

新增 `LayerColorSlot` 负责“层往什么颜色变”：

| `LayerColorSlot` | 语义 |
|---|---|
| `Red` | 红调 |
| `Orange` | 橙调 |
| `Yellow` | 黄调 |
| `Green` | 绿调 |
| `Cyan` | 青调 |
| `Blue` | 蓝调 |
| `Purple` | 紫调 |
| `Pink` | 粉调 |
| `Black` | 黑调 |
| `White` | 白调 |
| `Silver` | 银调 |
| `Gray` | 灰调 |

二者分离，避免把“部件语义”和“目标色调”混在同一个枚举里。

## 3. 数据保存结构

位置：`src/core/Project.h`

```cpp
QHash<QString, LayerSlot> layerSlots;
QHash<QString, LayerColorSlot> layerColorSlots;
```

规则：

| 字段 | 作用 |
|---|---|
| `layerSlots` | 保存部件 LayerSlot，例如服装、头发、武器金属。 |
| `layerColorSlots` | 保存颜色 LayerSlot，例如蓝调、黑调、银调。 |

`layerColorSlots` 按 `layerKey` 保存：

```json
{
  "layerColorSlots": {
    "num_00": "Blue",
    "num_01": "Black"
  }
}
```

`Auto` 不写入工程文件。读取旧工程时缺字段 = 全部 `Auto`。

## 4. UI 入口

位置：`src/ui/LayerTreePanel.cpp`

右键资源树后有三类菜单：

1. `部件 LayerSlot（旧逻辑）`
2. `单层颜色 LayerSlot`
3. `全层颜色 LayerSlot`

### 4.1 单层颜色 LayerSlot

行为：只修改当前右键层。

示例：

```text
右键 num_00
→ 单层颜色 LayerSlot
→ 蓝调
```

结果：

```cpp
layerColorSlots["num_00"] = LayerColorSlot::Blue;
```

同时清除当前层旧部件指定和肤色保护：

```cpp
layerSlots.remove(layerKey);
skinSafeLayerKeys.remove(layerKey);
```

原因：用户显式指定颜色后，该层应优先按颜色目标走，不再被旧部件菜单或肤色保护拦住。

### 4.2 全层颜色 LayerSlot

行为：批量修改所有层。

示例：

```text
右键任意层
→ 全层颜色 LayerSlot
→ 黑调
```

结果：

```cpp
for (const auto& l : layers) {
    layerColorSlots[l.key()] = LayerColorSlot::Black;
}
```

同时清空全部部件指定和肤色保护：

```cpp
layerSlots.clear();
skinSafeLayerKeys.clear();
```

原因：全层颜色是强制统一色调入口，优先级最高。

### 4.3 自动 / 全部自动

单层 `自动`：

```cpp
layerColorSlots.remove(layerKey);
```

全层 `全部自动`：

```cpp
layerColorSlots.clear();
```

恢复后智能随机重新回到旧 `LayerSlot + SchemePalette` 逻辑。

## 5. 显示逻辑

位置：`src/ui/LayerTreePanel.cpp`

资源树前缀优先级：

```text
肤色保护 > 颜色 LayerSlot > 部件 LayerSlot > 无前缀
```

显示规则：

| 状态 | 树节点显示 |
|---|---|
| 肤色保护 | `🛡️ layerName` |
| 颜色 Slot 非 Auto | `颜色 emoji + layerName` |
| 部件 Slot 非 Unknown | `部件 emoji + layerName` |
| 全自动 | `layerName` |

颜色 emoji 映射：

| Slot | Emoji | 中文名 |
|---|---:|---|
| `Red` | 🔴 | 红调 |
| `Orange` | 🟠 | 橙调 |
| `Yellow` | 🟡 | 黄调 |
| `Green` | 🟢 | 绿调 |
| `Cyan` | 🧊 | 青调 |
| `Blue` | 🔵 | 蓝调 |
| `Purple` | 🟣 | 紫调 |
| `Pink` | 🌸 | 粉调 |
| `Black` | ⚫ | 黑调 |
| `White` | ⚪ | 白调 |
| `Silver` | 🪙 | 银调 |
| `Gray` | ◻ | 灰调 |

Tooltip 同时显示：

```text
部件: xxx
颜色: xxx
```

## 6. 智能随机接入点

位置：`src/app/ProjectController.cpp`

智能随机时每层取两个 Slot：

```cpp
const LayerSlot slot = m_project.slotFor(l);
const LayerColorSlot colorSlot = m_project.colorSlotFor(l);
randomizeStackBySlot(stack, slot, colorSlot, palette);
```

调用范围：

| 功能 | 是否读取 `LayerColorSlot` |
|---|---:|
| 智能当前层 | 是 |
| 智能所有层 | 是 |
| 智能全部方案 | 是 |
| 智能 + 随机混合 | 是 |
| 旧随机 `randomizeStack()` | 否 |

所以颜色 Slot 只影响“智能类”随机，不破坏旧随机。

## 7. 核心变色流程

位置：`src/core/ColorEffect.cpp`

带颜色 Slot 的重载：

```cpp
void randomizeStackBySlot(EffectStack& s,
                          LayerSlot slot,
                          LayerColorSlot colorSlot,
                          const SchemePalette& palette,
                          quint32 seed)
{
    if (colorSlot == LayerColorSlot::Auto) {
        randomizeStackBySlot(s, slot, palette, seed);
        return;
    }

    Rng rng(seed);
    randomizeStackBySlot(s, slot, palette, rng.nextSeed());
    applyColorSlotPatch(s, slot, colorSlot, rng);
}
```

流程：

```text
先按部件 LayerSlot 生成基础效果
→ 再按 LayerColorSlot 追加颜色补丁
→ 得到最终 EffectStack
```

这保证颜色指定不是完全绕过旧逻辑，而是在旧部件策略上加“目标色调”。

## 8. 颜色参数表

位置：`paramsForColorSlot()`

每个颜色 Slot 转换成一组内部参数：

```cpp
struct ColorSlotParams {
    int hue;
    int jitter;
    int satLo, satHi;
    int lgtLo, lgtHi;
    int brtLo, brtHi;
    int ctrLo, ctrHi;
    int vibranceLo, vibranceHi;
    int vibSatLo, vibSatHi;
    int photoPreset;
    int photoLo, photoHi;
    bool mono;
    bool light;
    bool dark;
    bool metal;
};
```

核心字段：

| 字段 | 含义 |
|---|---|
| `hue` | 目标色相中心，0~359。 |
| `jitter` | 色相随机抖动范围。 |
| `satLo/satHi` | HSL 饱和度范围。 |
| `lgtLo/lgtHi` | HSL 明度范围。 |
| `brtLo/brtHi` | 亮度范围。 |
| `ctrLo/ctrHi` | 对比度范围。 |
| `photoPreset` | 可选照片滤镜预设。 |
| `mono` | 黑白灰银类，低饱处理。 |
| `light` | 亮色类，使用高光安全曲线。 |
| `dark` | 暗色类，使用深色安全曲线。 |
| `metal` | 金属类，使用金属曲线。 |

当前色调大致参数：

| Slot | Hue | 特点 |
|---|---:|---|
| `Red` | 0 | 暗红，较高对比。 |
| `Orange` | 28 | 暖橙，中等亮度。 |
| `Yellow` | 52 | 偏亮，低饱防刺眼。 |
| `Green` | 135 | 暗绿，压亮度。 |
| `Cyan` | 188 | 青色，中等明度。 |
| `Blue` | 228 | 暗蓝，高对比。 |
| `Purple` | 278 | 暗紫，高对比。 |
| `Pink` | 328 | 偏亮粉，低饱。 |
| `Black` | 220 | 近单色，强降饱，强压暗。 |
| `White` | 210 | 近单色，抬亮，低对比。 |
| `Silver` | 215 | 近单色，金属曲线。 |
| `Gray` | 210 | 近单色，中灰。 |

## 9. 部件适配规则

位置：`applyColorSlotPatch()`

颜色参数会根据部件 Slot 微调。

### 9.1 Skin

```cpp
if (color == LayerColorSlot::Auto || slot == LayerSlot::Skin) return;
```

肤色层不应用颜色补丁。

### 9.2 Hair

头发更克制：

```cpp
satLo = min(satLo, -20);
satHi = min(satHi, 0);
lgtLo = max(lgtLo, -14);
lgtHi = min(lgtHi, 12);
```

结果：头发不会过饱和，不会过黑或过白。

### 9.3 Decor01

主装饰略亮、略鲜：

```cpp
satLo += 4;
satHi += 6;
lgtLo += 2;
lgtHi += 4;
```

结果：装饰比主体更容易看见。

### 9.4 Decor02

发光点更亮、更鲜：

```cpp
satLo += 2;
satHi += 8;
lgtLo += 4;
lgtHi += 8;
```

黑 / 灰发光点会被转成更合理的蓝 / 银：

```cpp
Black -> Blue
Gray  -> Silver
```

原因：发光点强行黑灰会失去“发光”语义。

### 9.5 WeaponMetal

武器金属强制走金属合理色：

```text
Yellow / Orange → Yellow（金色）
Black           → Black（黑铁）
Cyan / Blue     → Silver（冷银）
其他颜色        → Silver（默认金属）
```

并设置：

```cpp
metal = true;
```

结果：金属层保留高光和对比，不会变成普通布料色。

## 10. 最终写入 EffectStack

`applyColorSlotPatch()` 最终会覆盖或开启这些效果：

### 10.1 HSL

```cpp
s.enabled[EffectStack::EHsl] = true;
s.hsl.hue = hueAbsToShift(hueAbs);
s.hsl.saturation = random(satLo, satHi);
s.hsl.lightness = random(lgtLo, lgtHi);
```

作用：把层推向目标色相、目标饱和度、目标明度。

### 10.2 亮度 / 对比度

```cpp
s.enabled[EffectStack::EBrtCtr] = true;
s.brtCtr.brightness = random(brtLo, brtHi);
s.brtCtr.contrast = random(ctrLo, ctrHi);
```

作用：黑色更沉，白色更亮，暗色更有层次。

### 10.3 自然饱和度

```cpp
s.enabled[EffectStack::EVibrance] = true;
s.vibrance.vibrance = random(vibranceLo, vibranceHi);
s.vibrance.saturation = random(vibSatLo, vibSatHi);
```

作用：二次控制饱和，避免颜色过艳。

### 10.4 曲线

```cpp
s.enabled[EffectStack::ECurves] = true;
```

曲线选择：

| 条件 | 曲线 |
|---|---|
| `metal == true` | 金属曲线 |
| `dark == true` | `makeDeepSafeCurve()` |
| `light == true` | `makeHighlightSafeCurve()` |
| 其他 | `makeSoftContrastCurve()` |

### 10.5 照片滤镜

```cpp
if (photoPreset >= 0 && rng.chance(mono ? 25 : 45)) {
    s.enabled[EffectStack::EPhotoFilter] = true;
}
```

普通颜色 45% 概率加照片滤镜；黑白灰银 25% 概率加，避免单色被染脏。

## 11. 优先级总结

最终优先级：

```text
肤色保护 / Skin
→ 跳过变色

LayerColorSlot::Auto
→ 旧智能随机：LayerSlot + SchemePalette

LayerColorSlot 非 Auto
→ 先旧智能随机
→ 再颜色补丁覆盖 HSL / 亮度 / 曲线 / 滤镜
```

## 12. 单层与全层区别

| 操作 | 写入范围 | 是否新增字段 | 是否清部件 Slot | 是否清肤色保护 |
|---|---|---:|---:|---:|
| 单层颜色 | 当前层 | 否，写 `layerColorSlots[layerKey]` | 是，仅当前层 | 是，仅当前层 |
| 全层颜色 | 所有层 | 否，批量写 `layerColorSlots` | 是，全部清空 | 是，全部清空 |
| 单层自动 | 当前层 | 否 | 否 | 否 |
| 全部自动 | 所有层 | 否 | 否 | 否 |

## 13. 关键代码位置

| 文件 | 作用 |
|---|---|
| `src/core/LayerData.h` | 定义 `LayerColorSlot` 与 helper 声明。 |
| `src/core/LayerData.cpp` | 字符串转换、emoji、中文名。 |
| `src/core/Project.h` | 保存 `layerColorSlots`，提供 `colorSlotFor()`。 |
| `src/app/ProjectIO.cpp` | 工程 JSON 读写 `layerColorSlots`。 |
| `src/ui/LayerTreePanel.cpp` | 右键菜单、显示 emoji、Tooltip。 |
| `src/app/ProjectController.cpp` | 单层/全层颜色写入，智能随机时读取颜色 Slot。 |
| `src/core/ColorEffect.cpp` | `randomizeStackBySlot()` 重载、颜色参数、颜色补丁应用。 |

## 14. 一句话流程

```text
右键选择颜色 LayerSlot
→ Project.layerColorSlots 记录颜色目标
→ 智能随机读取 LayerSlot + LayerColorSlot
→ 先生成部件基础效果
→ 再用颜色补丁覆盖目标色调
→ 资源树显示对应颜色 emoji
→ 工程保存时写入 layerColorSlots
```
