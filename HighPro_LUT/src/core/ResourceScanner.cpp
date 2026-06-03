#include "ResourceScanner.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <algorithm>

namespace HighPro {

namespace {

const QRegularExpression kFrameRe(R"(^(\d)(\d{3})\.tga$)",
                                  QRegularExpression::CaseInsensitiveOption);
const QRegularExpression kNumberedRe(R"(^\d{2,3}$)");

// 扫一个动作目录, 填 framesByDir
bool scanActionDir(const QString& actionDir, Action& out)
{
    QDir d(actionDir);
    QFileInfoList files = d.entryInfoList(QDir::Files | QDir::NoDotAndDotDot,
                                          QDir::Name);

    QMap<int, QVector<QPair<int, QString>>> tmp;     // dir -> [(frameId, path)]

    for (const QFileInfo& fi : files) {
        const QString name = fi.fileName();
        const QRegularExpressionMatch m = kFrameRe.match(name);
        if (!m.hasMatch()) continue;
        const int dirId   = m.captured(1).toInt();
        const int frameId = m.captured(2).toInt();
        tmp[dirId].push_back({ frameId, fi.absoluteFilePath() });
    }

    for (auto it = tmp.begin(); it != tmp.end(); ++it) {
        auto& list = it.value();
        std::sort(list.begin(), list.end(),
                  [](const QPair<int,QString>& a, const QPair<int,QString>& b) {
                      return a.first < b.first;
                  });
        QVector<QString> paths;
        paths.reserve(list.size());
        for (auto& p : list) paths.push_back(p.second);
        out.framesByDir.insert(it.key(), paths);
    }
    return !out.framesByDir.isEmpty();
}

// 扫一层根目录下的所有动作 + add_lut
void scanLayerRoot(const QString& layerRoot, LayerData& out, QStringList& warns)
{
    QDir d(layerRoot);
    if (!d.exists()) return;

    const QFileInfoList subs = d.entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);

    for (const QFileInfo& sub : subs) {
        const QString name = sub.fileName();
        if (name.compare("add_lut", Qt::CaseInsensitive) == 0) {
            out.addLutDir = sub.absoluteFilePath();
            continue;
        }
        Action a;
        a.name = name;
        if (scanActionDir(sub.absoluteFilePath(), a)) {
            out.actions.insert(name, a);
        }
    }

    if (out.actions.isEmpty()) {
        warns << QString("层 \"%1\" 下未找到任何动作目录(含 .tga)").arg(out.displayName);
    }
}

} // namespace


ResourceScanner::Result ResourceScanner::scan(const QString& sourceRoot)
{
    Result r;
    r.sourceRoot = sourceRoot;

    QDir root(sourceRoot);
    if (!root.exists()) {
        r.error = QString("源目录不存在: %1").arg(sourceRoot);
        return r;
    }

    const QFileInfoList tops = root.entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);

    QVector<LayerData> bodyLayer;          // 至多 1 个
    QVector<LayerData> numberedLayers;     // 0..N
    QVector<LayerData> addonLayers;        // 0..N

    for (const QFileInfo& fi : tops) {
        const QString name = fi.fileName();
        const QString path = fi.absoluteFilePath();

        if (name.compare("body", Qt::CaseInsensitive) == 0) {
            LayerData l;
            l.displayName = "body";
            l.rootDir     = path;
            l.kind        = LayerKind::Body;
            scanLayerRoot(path, l, r.warnings);
            if (!l.actions.isEmpty()) bodyLayer.push_back(std::move(l));
            continue;
        }

        if (name.compare("addon", Qt::CaseInsensitive) == 0) {
            // addon 下还要再扫一层 (01/02/03 ...)
            QDir aroot(path);
            const QFileInfoList subs = aroot.entryInfoList(
                QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
            int idx = 0;
            for (const QFileInfo& sub : subs) {
                LayerData l;
                l.displayName = QString("addon/%1").arg(sub.fileName());
                l.rootDir     = sub.absoluteFilePath();
                l.kind        = LayerKind::Addon;
                l.addonSubIdx = idx++;
                scanLayerRoot(sub.absoluteFilePath(), l, r.warnings);
                if (!l.actions.isEmpty()) addonLayers.push_back(std::move(l));
            }
            continue;
        }

        if (kNumberedRe.match(name).hasMatch()) {
            LayerData l;
            l.displayName = name;
            l.rootDir     = path;
            l.kind        = LayerKind::Numbered;
            l.numberedIdx = name.toInt();
            scanLayerRoot(path, l, r.warnings);
            if (!l.actions.isEmpty()) numberedLayers.push_back(std::move(l));
            continue;
        }

        // 其它目录忽略
    }

    // numberedLayers 按数字排序 (00 < 01 < 04)
    std::sort(numberedLayers.begin(), numberedLayers.end(),
              [](const LayerData& a, const LayerData& b) {
                  return a.numberedIdx < b.numberedIdx;
              });

    // 合成顺序: body (底) → 00..04 → addon (顶)
    r.layers.reserve(bodyLayer.size() + numberedLayers.size() + addonLayers.size());
    for (auto& l : bodyLayer)      r.layers.push_back(std::move(l));
    for (auto& l : numberedLayers) r.layers.push_back(std::move(l));
    for (auto& l : addonLayers)    r.layers.push_back(std::move(l));

    if (r.layers.isEmpty()) {
        r.error = QString("源目录下未找到任何有效层 (body / 数字层 / addon): %1")
                  .arg(sourceRoot);
    }
    return r;
}

} // namespace HighPro
