#pragma once

#include "core/LayerData.h"
#include "core/ColorEffect.h"
#include "core/SchemePalette.h"
#include <QString>
#include <QSet>
#include <QHash>
#include <QVector>
#include <optional>

namespace HighPro {

// 单个变色方案 = 全层 EffectStack + 元数据.
// 方案 0 永远是 "本体" (isBuiltin=true, 全层 disabled, 不可改 effects, 仅可改名).
//
// M5: 两类来源
//   1) 用户手编 (isBaked=false): EffectStack 实时烘焙 → effect_chain PS 实时渲染
//   2) 已烘焙导入 (isBaked=true): 来自 add_lut/0N.png, 只存层 → LUT 文件路径,
//      渲染时走 recolor PS, 不走 effect_chain. EffectPanel 对此类禁编辑.
//
// P0 智能随机: 方案级 palette 跟着 Scheme 走 (随重排/删除一起搬迁).
struct Scheme
{
    QString                     name;       // 显示名: "本体" / "方案01-默认本体" 等
    bool                        isBuiltin = false;
    bool                        isBaked   = false;     // 来自 add_lut PNG 反推
    bool                        locked    = false;     // 🔒 锁: 不参与随机变色 (本体/已烘焙不受影响)
    QHash<QString, EffectStack> layerEffects;          // 用户手编: layerKey -> 7 效果栈
    QHash<QString, QString>     layerLutPath;          // 已烘焙: layerKey -> add_lut/0N.png 绝对路径

    // P0 智能随机: 该方案当前配色盘. 无值 = 尚未跑过智能随机.
    // 智能当前层会 ensurePaletteForScheme() 自动生成, 智能所有层会强制重生.
    std::optional<SchemePalette> palette;

    static Scheme makeBuiltin() {
        Scheme s; s.name = "方案 0 - 默认本体"; s.isBuiltin = true; return s;
    }
};

// 一期的轻量项目状态. M5: 多方案 schemes.
struct Project
{
    QString sourceRoot;
    QString outputRoot;

    QVector<LayerData> layers;          // 合成顺序: body→00..04→addon

    // 显示控制
    QSet<QString> hiddenLayerKeys;
    QString       currentAddonKey;

    // M7: 肤色保护层 (这些层在所有方案下都不应用变色, 保持本体)
    QSet<QString> skinSafeLayerKeys;

    // P0 智能随机: 用户手动指定的层语义. 未在表中或值为 Unknown → defaultSlotFor() 启发式.
    //   - 设 LayerSlot::Skin 时, ProjectController 同步加入 skinSafeLayerKeys.
    //   - 取消 skinSafeLayerKeys 时, ProjectController 同步把 Skin 改回 Unknown.
    //   - 最终保护源仍是 skinSafeLayerKeys, slotFor() 命中 skinSafe 永远返回 Skin.
    QHash<QString, LayerSlot> layerSlots;

    // P1 颜色 LayerSlot: 用户手动指定的目标色调.
    //   - 未在表中或 Auto → 完全沿用旧智能随机逻辑.
    //   - 单层颜色右键只写当前 layerKey.
    //   - 全层颜色右键批量写所有 layerKey, 不额外保存全局字段.
    QHash<QString, LayerColorSlot> layerColorSlots;

    QString currentAction;
    int     currentDirection = 0;
    int     currentFrame     = 0;

    // 老 add_lut PNG 兼容路径 (Ctrl+1..6 加载用)
    QHash<QString, QString> layerLutPath;

    // M5: 多方案. schemes[0] 永远是"本体". 1..N 用户方案.
    QVector<Scheme> schemes;
    int             currentSchemeIndex = 0;   // EffectPanel 编辑哪一个

    // 当前在 EffectPanel 编辑的层 key (UI 使用)
    QString currentLayerKey;

    // === 工具方法 ===
    void clear() {
        sourceRoot.clear();
        outputRoot.clear();
        layers.clear();
        hiddenLayerKeys.clear();
        skinSafeLayerKeys.clear();
        layerSlots.clear();
        layerColorSlots.clear();
        currentAddonKey.clear();
        currentAction.clear();
        currentDirection = 0;
        currentFrame = 0;
        layerLutPath.clear();
        schemes.clear();
        schemes.push_back(Scheme::makeBuiltin());
        currentSchemeIndex = 0;
        currentLayerKey.clear();
    }

    bool isLayerVisible(const LayerData& l) const {
        return !hiddenLayerKeys.contains(l.key());
    }

    bool isSkinSafe(const LayerData& l) const {
        return skinSafeLayerKeys.contains(l.key());
    }

    // P0: 启发式默认 LayerSlot.
    // body → Skin, num_00..04 → Clothing/Skirt/Hair/Decor01/Decor02, num_05+ → Decor01,
    // addon → Decor02 (挂件多为发光 / 高亮点缀).
    LayerSlot defaultSlotFor(const LayerData& l) const {
        if (skinSafeLayerKeys.contains(l.key())) return LayerSlot::Skin;
        switch (l.kind) {
        case LayerKind::Body:
            return LayerSlot::Skin;
        case LayerKind::Numbered:
            switch (l.numberedIdx) {
            case 0:  return LayerSlot::Clothing;
            case 1:  return LayerSlot::Skirt;
            case 2:  return LayerSlot::Hair;
            case 3:  return LayerSlot::Decor01;
            case 4:  return LayerSlot::Decor02;
            default: return LayerSlot::Decor01;
            }
        case LayerKind::Addon:
            return LayerSlot::Decor02;
        }
        return LayerSlot::Clothing;
    }

    // P0: 实际 slot. 肤色保护永远优先 (双向同步源).
    //   skinSafe 命中     → Skin
    //   layerSlots 命中且非 Unknown → 用户指定
    //   其他              → 启发式
    LayerSlot slotFor(const LayerData& l) const {
        if (skinSafeLayerKeys.contains(l.key())) return LayerSlot::Skin;
        const LayerSlot saved = layerSlots.value(l.key(), LayerSlot::Unknown);
        if (saved != LayerSlot::Unknown) return saved;
        return defaultSlotFor(l);
    }

    // P1: 实际颜色 slot. Auto = 不干预旧智能随机.
    LayerColorSlot colorSlotFor(const LayerData& l) const {
        return layerColorSlots.value(l.key(), LayerColorSlot::Auto);
    }

    QString lutPathFor(const LayerData& l) const {
        return layerLutPath.value(l.key());
    }

    // 当前编辑方案 (用于 EffectPanel)
    Scheme*       currentScheme()       {
        if (currentSchemeIndex < 0 || currentSchemeIndex >= schemes.size()) return nullptr;
        return &schemes[currentSchemeIndex];
    }
    const Scheme* currentScheme() const {
        if (currentSchemeIndex < 0 || currentSchemeIndex >= schemes.size()) return nullptr;
        return &schemes[currentSchemeIndex];
    }

    // 在某方案中找某层的 effects
    const EffectStack* effectsForIn(const Scheme& s, const LayerData& l) const {
        auto it = s.layerEffects.find(l.key());
        return it == s.layerEffects.end() ? nullptr : &it.value();
    }

    // 当前方案中找某层的 effects (供 EffectPanel 用)
    const EffectStack* effectsFor(const LayerData& l) const {
        const auto* sc = currentScheme();
        if (!sc) return nullptr;
        return effectsForIn(*sc, l);
    }
    EffectStack& effectsForMut(const LayerData& l) {
        auto* sc = currentScheme();
        Q_ASSERT(sc && !sc->isBuiltin); // 本体不可写
        return sc->layerEffects[l.key()];
    }

    // 收集当前选中动作下所有可用方向 (各层并集)
    QList<int> availableDirections() const {
        QSet<int> dirs;
        for (const auto& l : layers) {
            if (!l.hasAction(currentAction)) continue;
            const auto* a = l.action(currentAction);
            if (!a) continue;
            for (int d : a->framesByDir.keys()) dirs.insert(d);
        }
        QList<int> out = dirs.values();
        std::sort(out.begin(), out.end());
        return out;
    }

    // 收集当前动作的最大帧数 (各层取 max, 各层不同长度时各自循环)
    int totalFrames() const {
        int maxN = 0;
        for (const auto& l : layers) {
            const auto* a = l.action(currentAction);
            if (!a) continue;
            const int n = a->frameCount(currentDirection);
            if (n > maxN) maxN = n;
        }
        return maxN > 0 ? maxN : 1;
    }

    // 全部层共有的动作交集 → 用作 UI 列表
    QStringList commonActions() const {
        QStringList out;
        if (layers.isEmpty()) return out;
        QSet<QString> base;
        for (const auto& a : layers.first().actions.keys()) base.insert(a);
        for (int i = 1; i < layers.size(); ++i) {
            QSet<QString> cur;
            for (const auto& a : layers[i].actions.keys()) cur.insert(a);
            base = base.intersect(cur);
        }
        out = base.values();
        std::sort(out.begin(), out.end());
        return out;
    }

    // 所有层的动作并集 (UI 兜底)
    QStringList unionActions() const {
        QSet<QString> u;
        for (const auto& l : layers) {
            for (const auto& a : l.actions.keys()) u.insert(a);
        }
        QStringList out = u.values();
        std::sort(out.begin(), out.end());
        return out;
    }
};

} // namespace HighPro
