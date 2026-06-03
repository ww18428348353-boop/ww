#pragma once

#include <QString>
#include <QVector>
#include <QHash>
#include <QMap>

namespace HighPro {

// 半语义层角色 (P0): 决定智能随机时该层用哪种参数策略.
// Unknown = 用户未指定, slotFor() 走启发式 defaultSlotFor().
//   Skin           = 肤色, 不参与变色 (与 skinSafeLayerKeys 双向同步)
//   Hair           = 头发, 低幅协调色
//   Clothing       = 服装主体, 主色承载
//   Skirt          = 裙摆, 跟服装主色或辅色协调
//   Decor01        = 主装饰, 点缀色 (accent)
//   Decor02        = 次装饰 / 宝石 / 发光点, 高亮 / glow
//   WeaponMetal    = 武器金属, 固定金属色池 (金/银/铜/黑铁/蓝钢)
//   WeaponNonMetal = 武器非金属, 跟服装或装饰统一
enum class LayerSlot : int {
    Unknown = 0,
    Skin,
    Hair,
    Clothing,
    Skirt,
    Decor01,
    Decor02,
    WeaponMetal,
    WeaponNonMetal,
};

// --- LayerSlot helper ---
// 字符串 ↔ enum (JSON 兼容). 未识别字符串返回 Unknown.
QString   layerSlotToString(LayerSlot slot);
LayerSlot layerSlotFromString(const QString& s);
// 单字符 emoji, 用于 LayerTreePanel 前缀.
QString   layerSlotEmoji(LayerSlot slot);
// UI 中文显示名 (右键菜单 / Tooltip).
QString   layerSlotDisplayName(LayerSlot slot);
// LayerTreePanel 在 refresh / sync 多次刷新时, 先剥掉旧 emoji + 空格再加新的.
// 否则会出现 "👕 👕 👕 num_00" 累积叠加.
QString   stripLayerSlotPrefix(QString text);

// 单个动作 = 多方向 × 多帧 (帧路径列表)
struct Action
{
    QString name;                                    // stand / walk / attack ...
    QMap<int, QVector<QString>> framesByDir;        // direction (0..7) -> [tga path...]

    QList<int> directions() const { return framesByDir.keys(); }
    int frameCount(int dir) const {
        auto it = framesByDir.find(dir);
        return it == framesByDir.end() ? 0 : it->size();
    }
    QString framePath(int dir, int idx) const {
        auto it = framesByDir.find(dir);
        if (it == framesByDir.end() || it->isEmpty()) return {};
        return it->at(idx % it->size());
    }
};

enum class LayerKind
{
    Body,        // body 层
    Numbered,    // 00..04..(未来 99) 数字层
    Addon,       // addon/<sub> 子层
};

// 单层资源 (扫描结果)
struct LayerData
{
    QString    displayName;       // "body" / "00" / "addon/01"
    QString    rootDir;           // 该层根目录(包含动作子目录)
    LayerKind  kind = LayerKind::Body;
    int        numberedIdx = -1;  // numbered: 00→0, 01→1
    int        addonSubIdx = -1;  // addon: 01→0, 02→1
    QString    addLutDir;         // <root>/add_lut (可能不存在)
    QMap<QString, Action> actions; // action_name -> Action

    QStringList actionNames() const { return actions.keys(); }
    bool        hasAction(const QString& a) const { return actions.contains(a); }
    const Action* action(const QString& a) const {
        auto it = actions.constFind(a);
        return it == actions.constEnd() ? nullptr : &(*it);
    }

    // 用于排序与稳定标识 (跨方案/缓存)
    QString key() const;
};

} // namespace HighPro
