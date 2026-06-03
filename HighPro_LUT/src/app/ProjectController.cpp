#include "ProjectController.h"
#include "core/ResourceScanner.h"
#include "ProjectIO.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QSet>
#include <QRandomGenerator>

namespace HighPro {

ProjectController& ProjectController::instance()
{
    static ProjectController c;
    return c;
}

bool ProjectController::loadSource(const QString& sourceRoot, QString* errorOut)
{
    auto r = ResourceScanner::scan(sourceRoot);
    if (!r.isOk()) {
        if (errorOut) *errorOut = r.error;
        return false;
    }

    m_project.clear();
    m_project.sourceRoot = r.sourceRoot;
    m_project.layers     = std::move(r.layers);

    // M5: 扫描每层 add_lut 目录, 取所有 layer 共有的 0N.png 作为已烘焙方案加入 schemes.
    //   规则: 文件名形如 0N.png (01.png .. 99.png), N >= 1.
    //   一个方案 N 仅当 (至少一个层有 add_lut/0N.png) 才加入; 没有该 0N 的层在该方案下走本体.
    {
        // step1: 收集每层 add_lut/*.png 的文件名集合
        QHash<QString, QSet<QString>> layerLutFiles; // layerKey -> { "01.png", "02.png", ... }
        QSet<QString> unionN;
        for (const auto& l : m_project.layers) {
            if (l.addLutDir.isEmpty()) continue;
            QDir d(l.addLutDir);
            if (!d.exists()) continue;
            const auto names = d.entryList(QStringList{"*.png"}, QDir::Files);
            QSet<QString> s;
            for (const QString& n : names) {
                // 仅接受形如 NN.png (>= 01)
                const QString stem = QFileInfo(n).baseName();
                if (stem.size() < 2) continue;
                bool ok = false;
                int idx = stem.toInt(&ok);
                if (!ok || idx <= 0 || idx > 99) continue;
                s.insert(n.toLower());
                unionN.insert(QString("%1.png").arg(idx, 2, 10, QChar('0')));
            }
            layerLutFiles.insert(l.key(), s);
        }

        // step2: 排序后逐个加方案
        QStringList sortedNames = unionN.values();
        std::sort(sortedNames.begin(), sortedNames.end());
        for (const QString& fn : sortedNames) {
            const int idx = QFileInfo(fn).baseName().toInt();
            Scheme sc;
            sc.isBaked = true;
            sc.name = QString("方案 %1 - %2 变色").arg(idx).arg(QFileInfo(fn).baseName());
            for (const auto& l : m_project.layers) {
                const auto& set = layerLutFiles.value(l.key());
                if (set.contains(fn)) {
                    sc.layerLutPath.insert(l.key(), QDir(l.addLutDir).filePath(fn));
                }
            }
            // 至少一层命中才加入 (上面 unionN 已保证, 这里冗余防御)
            if (!sc.layerLutPath.isEmpty()) {
                m_project.schemes.push_back(sc);
            }
        }

        // 当前方案: 优先选第一个 baked, 没有就停留在本体
        m_project.currentSchemeIndex = (m_project.schemes.size() >= 2) ? 1 : 0;
    }

    // 默认 action: 优先 stand, 否则首项
    const QStringList allActs = m_project.unionActions();
    if (allActs.contains("stand")) {
        m_project.currentAction = "stand";
    } else if (!allActs.isEmpty()) {
        m_project.currentAction = allActs.first();
    }

    // 默认 direction: 取该动作的第一个可用方向
    const auto dirs = m_project.availableDirections();
    m_project.currentDirection = dirs.isEmpty() ? 0 : dirs.first();
    m_project.currentFrame     = 0;

    // 默认: 仅显示第一个 addon, 其他 addon 默认隐藏.
    bool firstAddonSeen = false;
    for (const auto& l : m_project.layers) {
        if (l.kind == LayerKind::Addon) {
            if (!firstAddonSeen) {
                firstAddonSeen = true;
                m_project.currentAddonKey = l.key();
            } else {
                m_project.hiddenLayerKeys.insert(l.key());
            }
        }
    }

    // 默认编辑层 = body (若没有则取第一层)
    for (const auto& l : m_project.layers) {
        if (l.kind == LayerKind::Body) { m_project.currentLayerKey = l.key(); break; }
    }
    if (m_project.currentLayerKey.isEmpty() && !m_project.layers.isEmpty()) {
        m_project.currentLayerKey = m_project.layers.first().key();
    }

    m_dirty = false;        // 新加载源目录 → 视为干净状态 (含自动导入的 add_lut 方案)
    m_currentProjectPath.clear();   // 没有对应工程文件

    emit projectLoaded();
    emit actionChanged();
    emit directionChanged();
    emit frameChanged();
    emit visibilityChanged();
    emit currentLayerKeyChanged();
    emit effectsChanged();
    return true;
}

// === M8 工程持久化 ===

bool ProjectController::saveProject(const QString& path, const QJsonObject& uiState, QString* errorOut)
{
    if (!ProjectIO::saveToFile(m_project, path, uiState, errorOut)) return false;
    m_currentProjectPath = path;
    m_dirty = false;        // 保存成功 → 清 dirty
    return true;
}

bool ProjectController::loadProject(const QString& path, QJsonObject* outUiState, QString* errorOut)
{
    // 读取 JSON 但不动 m_project, 先看 sourceRoot
    Project tmp;
    QJsonObject ui;
    if (!ProjectIO::loadFromFile(path, tmp, &ui, errorOut)) return false;

    // 用 sourceRoot 跑 ResourceScanner 重建 layers
    if (tmp.sourceRoot.isEmpty()) {
        if (errorOut) *errorOut = "工程未记录源目录 (sourceRoot 空)";
        return false;
    }
    auto r = ResourceScanner::scan(tmp.sourceRoot);
    if (!r.isOk()) {
        if (errorOut) *errorOut = QString("源目录扫描失败: %1\n%2").arg(tmp.sourceRoot, r.error);
        return false;
    }

    // 替换当前项目: 先清空, 套用扫描结果, 再覆盖工程文件保存的状态
    m_project.clear();
    m_project.layers = std::move(r.layers);
    m_project.sourceRoot = tmp.sourceRoot;
    m_project.outputRoot = tmp.outputRoot;
    m_project.currentAction = tmp.currentAction;
    m_project.currentDirection = tmp.currentDirection;
    m_project.currentFrame = tmp.currentFrame;
    m_project.currentSchemeIndex = tmp.currentSchemeIndex;
    m_project.currentLayerKey = tmp.currentLayerKey;
    m_project.currentAddonKey = tmp.currentAddonKey;
    m_project.hiddenLayerKeys = tmp.hiddenLayerKeys;
    m_project.skinSafeLayerKeys = tmp.skinSafeLayerKeys;
    m_project.layerLutPath = tmp.layerLutPath;
    m_project.schemes = tmp.schemes;

    // 兜底: 如果工程没保存方案 (老文件), 至少有本体
    if (m_project.schemes.isEmpty()) {
        m_project.schemes.push_back(Scheme::makeBuiltin());
    }
    if (m_project.currentSchemeIndex < 0 || m_project.currentSchemeIndex >= m_project.schemes.size()) {
        m_project.currentSchemeIndex = 0;
    }

    // 默认 action / direction / 方向兜底
    if (m_project.currentAction.isEmpty()) {
        const QStringList allActs = m_project.unionActions();
        if (allActs.contains("stand")) m_project.currentAction = "stand";
        else if (!allActs.isEmpty())   m_project.currentAction = allActs.first();
    }
    const auto dirs = m_project.availableDirections();
    if (!dirs.contains(m_project.currentDirection)) {
        m_project.currentDirection = dirs.isEmpty() ? 0 : dirs.first();
    }

    m_currentProjectPath = path;
    m_dirty = false;        // 加载完是干净状态
    if (outUiState) *outUiState = ui;

    emit projectLoaded();
    emit actionChanged();
    emit directionChanged();
    emit frameChanged();
    emit visibilityChanged();
    emit currentLayerKeyChanged();
    emit effectsChanged();
    emit schemesChanged();
    emit currentSchemeChanged();
    return true;
}

void ProjectController::setCurrentAction(const QString& a)
{
    if (m_project.currentAction == a) return;
    m_project.currentAction = a;
    m_project.currentFrame  = 0;

    const auto dirs = m_project.availableDirections();
    if (!dirs.contains(m_project.currentDirection)) {
        m_project.currentDirection = dirs.isEmpty() ? 0 : dirs.first();
        emit directionChanged();
    }
    emit actionChanged();
    emit frameChanged();
}

void ProjectController::setCurrentDirection(int d)
{
    if (m_project.currentDirection == d) return;
    m_project.currentDirection = d;
    m_project.currentFrame     = 0;
    emit directionChanged();
    emit frameChanged();
}

void ProjectController::setCurrentFrame(int f)
{
    const int total = m_project.totalFrames();
    const int n = total > 0 ? (f % total + total) % total : 0;
    if (m_project.currentFrame == n) return;
    m_project.currentFrame = n;
    emit frameChanged();
}

void ProjectController::advanceFrame()
{
    setCurrentFrame(m_project.currentFrame + 1);
}

void ProjectController::setLayerVisible(const QString& layerKey, bool visible)
{
    bool changed = false;
    if (visible) {
        if (m_project.hiddenLayerKeys.remove(layerKey)) changed = true;
    } else {
        if (!m_project.hiddenLayerKeys.contains(layerKey)) {
            m_project.hiddenLayerKeys.insert(layerKey);
            changed = true;
        }
    }
    if (changed) { m_dirty = true; emit visibilityChanged(); }
}

void ProjectController::setCurrentAddon(const QString& layerKey)
{
    if (m_project.currentAddonKey == layerKey) return;
    m_project.currentAddonKey = layerKey;
    m_dirty = true;
    emit visibilityChanged();
}

void ProjectController::setLayerSkinSafe(const QString& layerKey, bool skinSafe)
{
    bool changed = false;
    if (skinSafe) {
        if (!m_project.skinSafeLayerKeys.contains(layerKey)) {
            m_project.skinSafeLayerKeys.insert(layerKey);
            changed = true;
        }
    } else {
        if (m_project.skinSafeLayerKeys.remove(layerKey)) changed = true;
    }
    if (changed) {
        m_dirty = true;
        emit visibilityChanged();   // 复用 visibilityChanged 信号刷预览/树
        emit effectsChanged();      // 缩略图也要重烘 (肤色层不参与变色)
    }
}

void ProjectController::setLayerLut(const QString& layerKey, const QString& lutPath)
{
    if (lutPath.isEmpty()) {
        if (m_project.layerLutPath.remove(layerKey)) { m_dirty = true; emit lutChanged(); }
    } else {
        m_project.layerLutPath.insert(layerKey, lutPath);
        m_dirty = true;
        emit lutChanged();
    }
}

int ProjectController::applyLutToAllLayers(const QString& filename)
{
    int n = 0;
    for (const auto& l : m_project.layers) {
        if (l.addLutDir.isEmpty()) continue;
        const QString candidate = l.addLutDir + QStringLiteral("/") + filename;
        if (QFile::exists(candidate)) {
            m_project.layerLutPath.insert(l.key(), candidate);
            ++n;
        } else {
            m_project.layerLutPath.remove(l.key());
        }
    }
    if (n > 0 || !m_project.layerLutPath.isEmpty()) { m_dirty = true; emit lutChanged(); }
    return n;
}

void ProjectController::clearAllLayerLut()
{
    if (m_project.layerLutPath.isEmpty()) return;
    m_project.layerLutPath.clear();
    m_dirty = true;
    emit lutChanged();
}

// === M4 ===

void ProjectController::setCurrentLayerKey(const QString& key)
{
    if (m_project.currentLayerKey == key) return;
    m_project.currentLayerKey = key;
    // 确保当前编辑方案中该层有 EffectStack
    if (auto* sc = m_project.currentScheme()) {
        if (!sc->isBuiltin && !key.isEmpty() && !sc->layerEffects.contains(key)) {
            sc->layerEffects.insert(key, EffectStack{});
        }
    }
    emit currentLayerKeyChanged();
}

void ProjectController::notifyEffectsChanged()
{
    m_dirty = true;
    emit effectsChanged();
}

void ProjectController::resetCurrentLayerEffects()
{
    const QString& k = m_project.currentLayerKey;
    if (k.isEmpty()) return;
    auto* sc = m_project.currentScheme();
    if (!sc || sc->isBuiltin) return;
    sc->layerEffects[k].reset();
    m_dirty = true;
    emit effectsChanged();
}

void ProjectController::resetAllLayerEffects()
{
    auto* sc = m_project.currentScheme();
    if (!sc || sc->isBuiltin) return;
    for (const auto& l : m_project.layers) {
        sc->layerEffects[l.key()].reset();
    }
    m_dirty = true;
    emit effectsChanged();
}

void ProjectController::resetAllSchemesEffects(bool includeBaked)
{
    int processed = 0;
    bool tagsChanged = false;
    for (int i = 0; i < m_project.schemes.size(); ++i) {
        Scheme& sc = m_project.schemes[i];
        if (sc.isBuiltin) continue;
        if (sc.isBaked && !includeBaked) continue;

        if (sc.isBaked && includeBaked) {
            // 已烘焙 → 转可编辑 (丢失原 LUT 引用)
            sc.isBaked = false;
            sc.layerLutPath.clear();
            for (const auto& l : m_project.layers) {
                if (!sc.layerEffects.contains(l.key())) {
                    sc.layerEffects.insert(l.key(), EffectStack{});
                }
            }
            tagsChanged = true;
        }
        for (const auto& l : m_project.layers) {
            sc.layerEffects[l.key()].reset();
            ++processed;
        }
    }
    if (tagsChanged) { m_dirty = true; emit schemesChanged(); }
    if (processed > 0) { m_dirty = true; emit effectsChanged(); }
}

void ProjectController::copyCurrentLayerEffectsToAll()
{
    const QString& k = m_project.currentLayerKey;
    if (k.isEmpty()) return;
    auto* sc = m_project.currentScheme();
    if (!sc || sc->isBuiltin) return;
    auto it = sc->layerEffects.find(k);
    if (it == sc->layerEffects.end()) return;
    EffectStack src = it.value();
    for (const auto& l : m_project.layers) {
        sc->layerEffects[l.key()] = src;
    }
    m_dirty = true;
    emit effectsChanged();
}

// M7: 智能随机
void ProjectController::randomizeCurrentLayer()
{
    auto* sc = m_project.currentScheme();
    if (!sc || sc->isBuiltin) return;
    if (sc->locked) return;     // 🔒 锁住的方案不参与变色
    const QString& k = m_project.currentLayerKey;
    if (k.isEmpty()) return;
    randomizeStack(sc->layerEffects[k]);
    m_dirty = true;
    emit effectsChanged();
}

void ProjectController::randomizeAllLayers(bool sameSeedAllLayers)
{
    auto* sc = m_project.currentScheme();
    if (!sc || sc->isBuiltin) return;
    if (sc->locked) return;     // 🔒 锁住的方案不参与变色

    // sameSeedAllLayers=true  → 各层用同一种子, 整套配色"协调统一" (色相中心一致)
    // sameSeedAllLayers=false → 每层独立种子, 各层完全独立的随机效果 (默认行为)
    quint32 seedShared = QRandomGenerator::global()->generate();
    if (seedShared == 0) seedShared = 1;

    int processed = 0;
    for (const auto& l : m_project.layers) {
        if (m_project.isSkinSafe(l)) continue;
        if (!m_project.isLayerVisible(l)) continue;
        EffectStack& dst = sc->layerEffects[l.key()];
        randomizeStack(dst, sameSeedAllLayers ? seedShared : 0);
        ++processed;
    }
    if (processed > 0) { m_dirty = true; emit effectsChanged(); }
}

void ProjectController::randomizeAllSchemes(bool includeBaked)
{
    int processed = 0;
    bool anyTagChange = false;
    QString currentName;
    if (m_project.currentSchemeIndex >= 0
        && m_project.currentSchemeIndex < m_project.schemes.size()) {
        currentName = m_project.schemes[m_project.currentSchemeIndex].name;
    }

    for (int i = 0; i < m_project.schemes.size(); ++i) {
        Scheme& sc = m_project.schemes[i];
        if (sc.isBuiltin) continue;       // 本体永不动
        if (sc.locked) continue;          // 🔒 锁住的方案完全跳过
        if (sc.isBaked && !includeBaked) continue;

        if (sc.isBaked && includeBaked) {
            // 已烘焙 → 转可编辑 (丢失原 add_lut PNG 引用)
            sc.isBaked = false;
            sc.layerLutPath.clear();
            anyTagChange = true;
            // 确保各层 EffectStack 存在
            for (const auto& l : m_project.layers) {
                if (!sc.layerEffects.contains(l.key())) {
                    sc.layerEffects.insert(l.key(), EffectStack{});
                }
            }
        }

        // 每方案每层独立随机
        for (const auto& l : m_project.layers) {
            if (m_project.isSkinSafe(l)) continue;
            if (!m_project.isLayerVisible(l)) continue;
            randomizeStack(sc.layerEffects[l.key()], 0);
            ++processed;
        }
    }

    // === 重排: 锁住的方案排在前面 (idx 1 起依次), 未锁的排后面 ===
    // 用户语义: 锁住 = "我固定要保留的", 重要, 应靠前; 随机出来的排后头.
    // 本体 (idx 0) 永远固定. 1..N 范围内做 stable_partition: locked 在前.
    bool reordered = false;
    if (m_project.schemes.size() > 2) {
        QVector<Scheme> head;   // 本体
        QVector<Scheme> locked; // 已锁 (在前)
        QVector<Scheme> rest;   // 未锁 (在后)
        for (int i = 0; i < m_project.schemes.size(); ++i) {
            const auto& sc = m_project.schemes[i];
            if (sc.isBuiltin)    head.push_back(sc);
            else if (sc.locked)  locked.push_back(sc);
            else                 rest.push_back(sc);
        }
        QVector<Scheme> merged;
        merged.reserve(m_project.schemes.size());
        merged += head; merged += locked; merged += rest;
        // 检查是否真有变化 (避免无谓 emit)
        for (int i = 0; i < merged.size(); ++i) {
            if (merged[i].name != m_project.schemes[i].name) { reordered = true; break; }
        }
        if (reordered) {
            m_project.schemes = std::move(merged);
            // 重命名: "方案 N - 后缀" 中 N 强制 = 新 idx (本体不动).
            // 后缀保留, 不丢用户语义信息.
            for (int i = 0; i < m_project.schemes.size(); ++i) {
                Scheme& sc = m_project.schemes[i];
                if (sc.isBuiltin) continue;
                QString rest = sc.name;
                // 剥老前缀 "方案 \d+ - "
                QRegularExpression re("^方案\\s*\\d+\\s*-\\s*");
                rest.remove(re);
                sc.name = QString("方案 %1 - %2").arg(i).arg(rest);
            }
            // 用旧名找回 currentSchemeIndex 不再可靠 (已改名),
            // 改用"内容指纹": 看 currentName 重排前所在的 isBuiltin/locked 桶 + 桶内偏移.
            // 简化: 用 重排 前/后 的 isBuiltin/isBaked/locked + layerEffects/layerLutPath 指针对照.
            // 这里直接保留 currentName 寻找前缀剥离后相等的项.
            QRegularExpression reCurr("^方案\\s*\\d+\\s*-\\s*");
            QString currTail = currentName; currTail.remove(reCurr);
            int newIdx = 0;
            for (int i = 0; i < m_project.schemes.size(); ++i) {
                QString tail = m_project.schemes[i].name;
                tail.remove(reCurr);
                if (tail == currTail) { newIdx = i; break; }
            }
            m_project.currentSchemeIndex = newIdx;
            anyTagChange = true;
        }
    }

    if (processed > 0 || anyTagChange) {
        m_dirty = true;
        if (anyTagChange) emit schemesChanged();
        if (processed > 0) emit effectsChanged();
        if (reordered) emit currentSchemeChanged();
    }
}

// === M5 方案管理 ===

void ProjectController::setCurrentSchemeIndex(int i)
{
    // -1 = 不选中任何方案 (画布不显示选中标识)
    if (i < -1 || i >= m_project.schemes.size()) return;
    if (m_project.currentSchemeIndex == i) return;
    m_project.currentSchemeIndex = i;
    // 仅切换"当前方案" — 不算未保存修改 (跟切动作/帧一样属于预览状态)
    emit currentSchemeChanged();
    emit effectsChanged();    // 切方案后预览 / EffectPanel 都要刷
}

int ProjectController::addScheme(const QString& name)
{
    Scheme s;
    s.isBuiltin = false;
    s.isBaked   = false;
    const int newIdx = m_project.schemes.size();    // 新方案的 idx
    s.name = name.isEmpty()
        ? QString("方案 %1 - %2 变色")
              .arg(newIdx)
              .arg(newIdx, 2, 10, QChar('0'))         // 02/05/11
        : name;
    for (const auto& l : m_project.layers) {
        s.layerEffects.insert(l.key(), EffectStack{});
    }
    m_project.schemes.push_back(s);
    m_dirty = true;
    emit schemesChanged();
    return newIdx;
}

bool ProjectController::removeScheme(int idx)
{
    if (idx <= 0 || idx >= m_project.schemes.size()) return false; // 本体不可删
    m_project.schemes.removeAt(idx);
    m_dirty = true;
    if (m_project.currentSchemeIndex >= m_project.schemes.size()) {
        m_project.currentSchemeIndex = m_project.schemes.size() - 1;
        emit currentSchemeChanged();
    }
    emit schemesChanged();
    emit effectsChanged();
    return true;
}

void ProjectController::renameScheme(int idx, const QString& name)
{
    if (idx < 0 || idx >= m_project.schemes.size()) return;
    if (m_project.schemes[idx].name == name) return;
    m_project.schemes[idx].name = name;
    m_dirty = true;
    emit schemesChanged();
}

bool ProjectController::isSchemeLocked(int idx) const
{
    if (idx < 0 || idx >= m_project.schemes.size()) return false;
    return m_project.schemes[idx].locked;
}

void ProjectController::setSchemeLocked(int idx, bool locked)
{
    if (idx < 0 || idx >= m_project.schemes.size()) return;
    Scheme& sc = m_project.schemes[idx];
    if (sc.isBuiltin) return;          // 本体不锁
    if (sc.locked == locked) return;
    sc.locked = locked;
    m_dirty = true;
    emit schemesChanged();
}

} // namespace HighPro
