#pragma once

#include <QString>
#include <QVector>
#include <QHash>
#include <QMap>

namespace HighPro {

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
