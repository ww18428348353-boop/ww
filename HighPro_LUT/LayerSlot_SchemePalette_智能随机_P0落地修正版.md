# LayerSlot + SchemePalette + 智能随机 P0 落地修正版

> 项目: `HighPro_LUT`  
> 主题: P0 阶段只跑通 `LayerSlot + SchemePalette + 智能随机`  
> 范围: 不做复杂图像识别、不做 `LayerStats`、不做评分系统。  
> 目标: 先用半语义分块和方案级调色盘，把随机变色从“每层乱随机”升级成“整套方案统一配色”。

---

## 一、P0 总原则

P0 阶段不做复杂功能。

暂不做：

```text
复杂图像识别
LayerStats
自动材质分析
缩略图评分系统
机器学习 / AI 评分
发光面积判断
金属自动识别
```

先做：

```text
LayerSlot 人工语义分块
SchemePalette 方案级调色盘
每个 slot 使用不同随机范围
智能随机按钮走新逻辑
旧 randomizeStack 保留 legacy
```

核心目标：

```text
先把 LayerSlot + SchemePalette + 智能随机 跑通
```

---

## 二、必改点 1~20

### 1. `schemePalettes` 不建议放 `Project` 的 `QHash<int, SchemePalette>`

原计划：

```cpp
QHash<int, SchemePalette> schemePalettes;    // scheme idx → palette
```

不推荐。

原因：当前 `randomizeAllSchemes()` 里有方案重排逻辑：

```text
锁定方案 stable_partition 到前面
方案删除会导致 index 改变
方案重命名会导致 index 语义变化
```

如果 palette 按 `schemeIdx` 存，重排后会串色。

推荐改成放进 `Scheme`：

```cpp
#include <optional>

struct Scheme
{
    QString name;
    bool isBuiltin = false;
    bool isBaked   = false;
    bool locked    = false;

    QHash<QString, EffectStack> layerEffects;
    QHash<QString, QString>     layerLutPath;

    std::optional<SchemePalette> palette;
};
```

如果不想用 `std::optional`，也可以用：

```cpp
bool hasPalette = false;
SchemePalette palette;
```

推荐 `std::optional<SchemePalette>`。

好处：

```text
方案重排 → palette 跟着 Scheme 走
方案删除 → 不需要维护 QHash index
工程保存 → schemes[i].palette 直接序列化
```

---

### 2. `8 个文件` 实际不止 8 个文件

原计划是 8 个模块，但实际会改这些文件：

```text
src/core/LayerData.h
src/core/Project.h
src/core/SchemePalette.h      新增
src/core/SchemePalette.cpp    新增
src/core/ColorEffect.h
src/core/ColorEffect.cpp
src/app/ProjectController.h
src/app/ProjectController.cpp
src/ui/EffectPanel.cpp
src/ui/LayerTreePanel.cpp
src/app/ProjectIO.cpp
CMakeLists.txt                必须加
```

实际是 12 个文件左右。

遗漏重点：`CMakeLists.txt` 必须加入新文件：

```cmake
src/core/SchemePalette.h
src/core/SchemePalette.cpp
```

否则编译不过。

---

### 3. `LayerSlot::Unknown` 语义要定清楚

原计划：

```cpp
Unknown = 0, // 走启发式
```

这个可以，但必须明确：

```text
用户设置 Unknown = 清除手动指定
slotFor() 遇到 Unknown = defaultSlotFor()
```

推荐逻辑：

```cpp
LayerSlot Project::slotFor(const LayerData& l) const
{
    if (skinSafeLayerKeys.contains(l.key())) {
        return LayerSlot::Skin;
    }

    const LayerSlot saved = layerSlots.value(l.key(), LayerSlot::Unknown);
    if (saved != LayerSlot::Unknown) {
        return saved;
    }

    return defaultSlotFor(l);
}
```

注意：

```text
肤色保护优先级最高
```

---

### 4. `Skin` 和现有 `skinSafeLayerKeys` 要同步

项目已有肤色保护功能。新增 `LayerSlot::Skin` 后，不能出现两套状态互相打架。

推荐规则：

```text
skinSafeLayerKeys 是最终保护源
LayerSlot::Skin 是 UI 设置入口
```

右键设置 `Skin` 时：

```cpp
setLayerSkinSafe(key, true);
setLayerSlot(key, LayerSlot::Skin);
```

右键取消肤色保护时：

```cpp
setLayerSkinSafe(key, false);
if (layerSlots.value(key) == LayerSlot::Skin) {
    layerSlots.remove(key); // 或设 Unknown
}
```

`slotFor()` 中也必须保证：

```text
skinSafeLayerKeys 命中 → 永远 Skin
```

避免出现：

```text
slot 显示 Skin，但实际没保护
肤色保护了，但 slot 显示 Clothing
```

---

### 5. UI 入口和前面选择有冲突

前面选择的是：

```text
直接替换旧随机按钮
```

但原清单写：

```text
EffectPanel.cpp — 加 row0 智能按钮
4 个新按钮
现有 row1 随机完全不动
```

这变成了“并存”。

P0 推荐继续使用最干净方案：

```text
原 4 个随机按钮直接改成智能随机
旧 randomizeStack 代码保留，但 UI 不暴露
```

按钮名称可以改为：

```text
🎨 智能当前层
🎨 智能所有层
🎨 智能可编辑全部
🎨 智能全部
```

旧随机不显示。

如果要做 A/B 对比，可后续加调试入口，不建议 P0 暴露给普通用户。

---

### 6. `ProjectController` 需要加 `setLayerSlot`

UI 右键改 slot 必须有 controller 方法。

建议加：

```cpp
void setLayerSlot(const QString& layerKey, LayerSlot slot);
```

参考实现：

```cpp
void ProjectController::setLayerSlot(const QString& layerKey, LayerSlot slot)
{
    if (layerKey.isEmpty()) return;

    if (slot == LayerSlot::Unknown) {
        if (!m_project.layerSlots.remove(layerKey)) return;
    } else {
        if (m_project.layerSlots.value(layerKey, LayerSlot::Unknown) == slot) return;
        m_project.layerSlots.insert(layerKey, slot);
    }

    if (slot == LayerSlot::Skin) {
        m_project.skinSafeLayerKeys.insert(layerKey);
    } else if (m_project.skinSafeLayerKeys.contains(layerKey)) {
        // 用户显式选非 Skin 时，取消肤色保护
        m_project.skinSafeLayerKeys.remove(layerKey);
    }

    m_dirty = true;
    emit visibilityChanged(); // 刷 layer tree emoji
    emit effectsChanged();    // 智能随机 / 预览相关刷新
}
```

也可新增独立信号：

```cpp
void layerSlotsChanged();
```

P0 可复用 `visibilityChanged()`。

---

### 7. `Project::clear()` 要清新字段

如果 `layerSlots` 放在 `Project`：

```cpp
layerSlots.clear();
```

如果仍然保留 `schemePalettes`，也要：

```cpp
schemePalettes.clear();
```

但推荐不要用 `schemePalettes`，改为 `Scheme::palette`。

---

### 8. JSON 版本建议从 1 升到 2

当前 `ProjectIO.cpp` 写：

```cpp
o["version"] = 1;
root["version"] = 1;
```

新增字段后建议：

```cpp
o["version"] = 2;
root["version"] = 2;
```

老工程缺字段没问题，走默认值。

---

### 9. JSON 枚举建议同时支持 int 和 string

原计划：

```json
"layerSlots": { "key": slotInt }
```

可行，但可读性差，且 enum 顺序改动后会读错。

推荐加 helper：

```cpp
QString layerSlotToString(LayerSlot slot);
LayerSlot layerSlotFromString(const QString& s);
```

JSON 推荐：

```json
"layerSlots": {
  "num_00": "Clothing",
  "num_01": "Skirt",
  "num_02": "Hair"
}
```

比 int 更安全。

P0 最低要求：

```text
写 string
读 string
兼容读取 int
```

---

### 10. `LayerTreePanel` emoji 前缀要剥离所有 slot emoji

当前已有逻辑只剥：

```text
🛡
```

新增后必须剥全部：

```text
🛡
💇
👕
👗
💎
⚡
⚔
🪵
❓
```

否则多次刷新会变成：

```text
👕 👕 👕 num_00
```

建议写函数：

```cpp
QString stripSlotPrefix(QString text);
QString slotEmoji(LayerSlot slot);
QString slotDisplayName(LayerSlot slot);
```

显示逻辑：

```cpp
const QString base = stripSlotPrefix(node->text(0));
const LayerSlot slot = proj.slotFor(layer);
node->setText(0, slotEmoji(slot) + " " + base);
```

肤色保护优先：

```cpp
if (proj.isSkinSafe(l)) emoji = "🛡";
else emoji = slotEmoji(slot);
```

---

### 11. `defaultSlotFor()` 启发式别写死太自信

风险：

```text
num_02 不一定是头发
```

默认映射可以用，但必须允许用户右键改。

推荐默认：

```cpp
LayerSlot Project::defaultSlotFor(const LayerData& l) const
{
    if (skinSafeLayerKeys.contains(l.key())) return LayerSlot::Skin;

    if (l.kind == LayerKind::Body) return LayerSlot::Skin;

    if (l.kind == LayerKind::Numbered) {
        switch (l.numberedIdx) {
        case 0: return LayerSlot::Clothing;
        case 1: return LayerSlot::Skirt;
        case 2: return LayerSlot::Hair;
        case 3: return LayerSlot::Decor01;
        case 4: return LayerSlot::Decor02;
        default: return LayerSlot::Decor01;
        }
    }

    if (l.kind == LayerKind::Addon) return LayerSlot::Decor02;

    return LayerSlot::Clothing;
}
```

UI 必须明显可改。

---

### 12. `smartRandomizeCurrentLayer()` 逻辑要特别注意

原计划：

```text
复用当前方案 palette，只算当前层
```

正确。

但如果当前方案还没有 palette，要生成并保存。

建议做一个辅助方法：

```cpp
SchemePalette& ensurePaletteForScheme(int schemeIdx);
```

如果 palette 放进 `Scheme`：

```cpp
SchemePalette& ProjectController::ensurePaletteForScheme(int idx)
{
    Scheme& sc = m_project.schemes[idx];
    if (!sc.palette.has_value()) {
        sc.palette = generatePalette(idx, QRandomGenerator::global()->generate());
    }
    return sc.palette.value();
}
```

这样：

```text
智能当前层不会每点一次就整套配色变掉
```

---

### 13. `smartRandomizeAllLayers()` 是否重生 palette 要明确

推荐语义：

```text
点一次智能所有层 = 当前方案换一套完整配色
```

实现：

```cpp
sc.palette = generatePalette(currentSchemeIndex, seed);
```

然后全层按这个 palette 重算。

---

### 14. `smartRandomizeAllSchemes()` 对 baked 方案处理要复用旧逻辑

旧 `randomizeAllSchemes(includeBaked)` 有 baked 降级逻辑：

```cpp
sc.isBaked = false;
sc.layerLutPath.clear();
确保 layerEffects 存在
```

新智能方法也必须复制这段。

否则已烘焙方案仍走 LUT 渲染，看不到新效果。

---

### 15. `randomizeStackBySlot()` 要保留 `shadowProtectThreshold`

旧 `randomizeStack()` 文档里说：

```text
保存当前 shadowProtectThreshold
reset 后恢复
```

新函数也要这样做：

```cpp
const int shadow = stack.shadowProtectThreshold;
stack.reset();
stack.shadowProtectThreshold = shadow;
```

`applySkinReset` 也建议保持用户原来的 `shadowProtectThreshold`。

---

### 16. `Decor02` P0 先不做面积判断

原函数签名：

```cpp
void randomizeStackBySlot(EffectStack& stack,
                          LayerSlot slot,
                          const SchemePalette& palette,
                          quint32 seed = 0);
```

没有 `LayerStats`，所以 P0 没法判断：

```text
Decor02 面积小 → Glow
Decor02 面积大 → Decor01
```

P0 接受固定规则：

```text
Decor02 = 高亮 / 发光点缀
```

如果用户发现某资源里 `Decor02` 是大面积层，让用户右键改成：

```text
Decor01
Skirt
Clothing
```

---

### 17. 武器金属不会自动命中，没问题，但 UI 要能设置

当前层结构无法区分金属 / 非金属，默认不会跑到 `WeaponMetal`。

这没问题。

但右键菜单必须包含：

```text
⚔ 武器金属
🪵 武器非金属
```

否则金属随机代码写了也没人能用。

---

### 18. `ColorEffect.h` 依赖要避免循环 include

`Project.h` 现在包含：

```cpp
#include "core/LayerData.h"
#include "core/ColorEffect.h"
```

如果 `ColorEffect.h` 再 include `SchemePalette.h`，依赖会变重。

推荐在 `ColorEffect.h` 前置声明：

```cpp
enum class LayerSlot : int;
struct SchemePalette;
```

声明函数：

```cpp
void randomizeStackBySlot(EffectStack& stack,
                          LayerSlot slot,
                          const SchemePalette& palette,
                          quint32 seed = 0);
```

在 `ColorEffect.cpp` 里 include：

```cpp
#include "core/LayerData.h"
#include "core/SchemePalette.h"
```

这样依赖最稳。

---

### 19. `SchemePalette` 需要 JSON helper

不要把 palette 序列化逻辑散落。可以放在 `ProjectIO.cpp` 匿名 namespace，也可以放在 `SchemePalette.h/.cpp`。

P0 推荐放 `ProjectIO.cpp` 匿名 namespace，集中读写工程。

字段建议：

```json
"palette": {
  "primaryHue": 220,
  "secondaryHue": 190,
  "accentHue": 45,
  "accent2Hue": 285,
  "glowHue": 200,
  "metalTone": "Silver",
  "mood": "Frost",
  "bias": 0.13
}
```

---

### 20. `schemeIdx % 27` 要跳过本体 idx 0

原计划：

```text
smartRandomizeAllSchemes 按 schemeIdx % 27 取风格
```

注意：方案 0 是本体，不参与。

推荐：

```cpp
const int styleIdx = (schemeIdx - 1) % 27;
```

这样：

```text
方案 1 → 第 1 套风格
方案 2 → 第 2 套风格
...
方案 27 → 第 27 套风格
方案 28 → 第 1 套风格
```

不要让方案 1 拿第 2 套风格。

---

## 三、推荐修正版落地清单

### 1. `src/core/LayerData.h`

修改内容：

```text
加 LayerSlot enum
可加 slot string / emoji helper 声明
```

建议 enum：

```cpp
enum class LayerSlot : int {
    Unknown = 0,        // 走启发式
    Skin,               // 🛡 肤色，不参与变色
    Hair,               // 💇 头发，低幅协调
    Clothing,           // 👕 服装，主色
    Skirt,              // 👗 裙摆，主色协调
    Decor01,            // 💎 装饰01，点缀
    Decor02,            // ⚡ 装饰02，高亮 / 发光
    WeaponMetal,        // ⚔ 武器金属
    WeaponNonMetal      // 🪵 武器非金属
};
```

---

### 2. `src/core/SchemePalette.h`

新增文件。

内容：

```text
StyleMood enum
MetalTone enum
SchemePalette struct
SchemeStyleDef struct
kSchemeStyleCount
函数声明 generatePalette()
枚举 string helper 可选
```

---

### 3. `src/core/SchemePalette.cpp`

新增文件。

内容：

```text
const SchemeStyleDef kSchemeStyles[27]
generatePalette(int schemeIdx, quint32 seed)
27 风格槽位
风格 hue 微抖动
metalTone / mood / bias 生成
```

注意：

```cpp
const int styleIdx = (schemeIdx - 1) % 27;
```

---

### 4. `src/core/Project.h`

修改内容：

```text
Project::layerSlots
Scheme::palette
Project::slotFor()
Project::defaultSlotFor()
Project::clear() 清 layerSlots
```

推荐：

```cpp
QHash<QString, LayerSlot> layerSlots;
```

`Scheme` 内：

```cpp
std::optional<SchemePalette> palette;
```

需要 include：

```cpp
#include "core/SchemePalette.h"
#include <optional>
```

---

### 5. `src/core/ColorEffect.h`

修改内容：

```text
保留旧 randomizeStack()
新增 randomizeStackBySlot()
```

推荐前置声明，避免重 include：

```cpp
enum class LayerSlot : int;
struct SchemePalette;

void randomizeStackBySlot(EffectStack& stack,
                          LayerSlot slot,
                          const SchemePalette& palette,
                          quint32 seed = 0);
```

---

### 6. `src/core/ColorEffect.cpp`

修改内容：

```text
新增 randomizeStackBySlot()
新增 8 个 applyXxx() 静态函数
旧 randomizeStack() 不动
```

8 个分派：

```text
applySkinReset
applyHair
applyClothing
applySkirt
applyDecor01
applyDecor02
applyWeaponMetal
applyWeaponNonMetal
```

随机范围：

```text
Hair: secondaryHue ±15°, sat -15..+5
Clothing: primaryHue ±8°, sat -18..+8
Skirt: primaryHue ±15° 或 secondaryHue ±10°，大面积低饱
Decor01: accentHue ±10°, sat -5..+18
Decor02: accent2Hue / glowHue ±10°, sat 0..+25, lightness +2..+18
WeaponMetal: 6 金属色池固定 hue ±5°, 加曲线增对比保高光
WeaponNonMetal: 70% 随 Clothing，30% 随 Decor01
Skin: reset，不变色
```

必须保留：

```cpp
const int shadow = stack.shadowProtectThreshold;
stack.reset();
stack.shadowProtectThreshold = shadow;
```

---

### 7. `src/app/ProjectController.h`

修改内容：

```text
加 setLayerSlot()
加 3 个智能随机方法
可加 ensurePaletteForScheme() private helper
```

接口：

```cpp
void setLayerSlot(const QString& layerKey, LayerSlot slot);

void smartRandomizeCurrentLayer();
void smartRandomizeAllLayers();
void smartRandomizeAllSchemes(bool includeBaked);
```

---

### 8. `src/app/ProjectController.cpp`

修改内容：

```text
实现 setLayerSlot()
实现 ensurePaletteForScheme()
实现 smartRandomizeCurrentLayer()
实现 smartRandomizeAllLayers()
实现 smartRandomizeAllSchemes()
```

关键规则：

```text
smartRandomizeCurrentLayer → 复用当前方案 palette，没有则生成
smartRandomizeAllLayers → 当前方案重生 palette
smartRandomizeAllSchemes → 每方案按 idx 取 kSchemeStyles 风格
includeBaked=true → 已烘焙方案降级为可编辑
锁定方案跳过
本体方案跳过
肤色保护层跳过
不可见层跳过
```

---

### 9. `src/ui/EffectPanel.cpp`

P0 推荐：直接替换原随机按钮。

原按钮：

```text
🎲 随机当前层
🎲 随机所有层
🎲 随机可编辑全部
🎲 随机全部
```

改成：

```text
🎨 智能当前层
🎨 智能所有层
🎨 智能可编辑全部
🎨 智能全部
```

连接改为：

```cpp
ProjectController::instance().smartRandomizeCurrentLayer();
ProjectController::instance().smartRandomizeAllLayers();
ProjectController::instance().smartRandomizeAllSchemes(false);
ProjectController::instance().smartRandomizeAllSchemes(true);
```

旧 `randomizeCurrentLayer()` 等函数可以保留，但 UI 不暴露。

---

### 10. `src/ui/LayerTreePanel.cpp`

修改内容：

```text
右键 slot 子菜单
emoji 前缀
syncCheckStates 剥所有前缀
肤色保护仍然 🛡 优先
```

右键菜单项：

```text
设置层语义 →
  ❓ 自动 / 未知
  🛡 肤色
  💇 头发
  👕 服装
  👗 裙摆
  💎 装饰01
  ⚡ 装饰02
  ⚔ 武器金属
  🪵 武器非金属
```

注意：

```text
选择 Skin 要同步 skinSafeLayerKeys
选择非 Skin 要取消肤色保护或至少保证显示一致
```

---

### 11. `src/app/ProjectIO.cpp`

修改内容：

```text
layerSlots JSON
schemes[i].palette JSON
version 2
老工程缺字段 → 默认启发式
```

推荐 JSON：

```json
"layerSlots": {
  "num_00": "Clothing",
  "num_01": "Skirt",
  "num_02": "Hair"
}
```

每个方案：

```json
"palette": {
  "primaryHue": 220,
  "secondaryHue": 190,
  "accentHue": 45,
  "accent2Hue": 285,
  "glowHue": 200,
  "metalTone": "Silver",
  "mood": "Frost",
  "bias": 0.13
}
```

---

### 12. `CMakeLists.txt`

必须添加：

```cmake
src/core/SchemePalette.h
src/core/SchemePalette.cpp
```

否则新模块不会参与编译。

---

## 四、P0 不做内容

P0 阶段明确不做：

```text
LayerStats
图像统计自动识别材质
自动识别发光层面积
自动识别金属层
质量评分系统
候选多次生成 + 打分
缩略图直方图分析
OKLCH / HSLuv 颜色空间
AI 评分
```

这些放 P1 / P2。

---

## 五、最终 P0 目标

P0 完成后，用户体验应是：

```text
打开工程
→ 层树自动显示语义 emoji
→ 用户可右键修正层语义
→ 点击智能随机所有层
→ 当前方案生成统一 Palette
→ 每个层按 Slot 使用不同随机规则
→ 27 个方案按固定风格槽生成
→ 随机结果比旧逻辑明显统一、干净、像皮肤配色
```

最重要的是：

```text
不要让每层独立全范围 hue 随机
不要让旧随机默认暴露给用户
不要让 scheme palette 按 index 存在 Project 里
```

最终落地路线：

```text
LayerSlot
→ SchemePalette
→ randomizeStackBySlot
→ ProjectController 智能随机
→ LayerTreePanel 右键设置语义
→ EffectPanel 按钮调用智能随机
→ ProjectIO 保存 slot + palette
```
