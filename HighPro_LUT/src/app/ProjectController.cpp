#include "ProjectController.h"
#include "core/ResourceScanner.h"
#include "ProjectIO.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QSet>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <array>

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
    clearUndo();                    // 新源 → 旧 undo/redo 失效

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
    m_project.layerSlots = tmp.layerSlots;   // P0: 用户手动指定的层语义
    m_project.layerColorSlots = tmp.layerColorSlots; // P1: 用户手动指定的颜色语义
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
    clearUndo();            // 加载新工程 → 旧 undo/redo 失效
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

void ProjectController::setAllLayersVisible(bool visible)
{
    bool changed = false;
    if (visible) {
        if (!m_project.hiddenLayerKeys.isEmpty()) {
            m_project.hiddenLayerKeys.clear();
            changed = true;
        }
    } else {
        for (const auto& l : m_project.layers) {
            const QString k = l.key();
            if (!m_project.hiddenLayerKeys.contains(k)) {
                m_project.hiddenLayerKeys.insert(k);
                changed = true;
            }
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

void ProjectController::emitPreviewRefresh()
{
    // 不动 m_dirty, 也不 emit schemesChanged (避免缩略图 N 次重烘).
    emit effectsChanged();
}

void ProjectController::resetCurrentLayerEffects()
{
    const QString& k = m_project.currentLayerKey;
    if (k.isEmpty()) return;
    auto* sc = m_project.currentScheme();
    if (!sc || sc->isBuiltin) return;
    pushUndoSnapshot(QStringLiteral("重置当前层效果"));
    sc->layerEffects[k].reset();
    m_dirty = true;
    emit effectsChanged();
}

void ProjectController::resetAllLayerEffects()
{
    auto* sc = m_project.currentScheme();
    if (!sc || sc->isBuiltin) return;
    pushUndoSnapshot(QStringLiteral("重置所有层效果"));
    for (const auto& l : m_project.layers) {
        sc->layerEffects[l.key()].reset();
    }
    m_dirty = true;
    emit effectsChanged();
}

void ProjectController::resetAllSchemesEffects(bool includeBaked)
{
    pushUndoSnapshot(includeBaked ? QStringLiteral("重置全部方案")
                                  : QStringLiteral("重置可编辑方案"));
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
    pushUndoSnapshot(QStringLiteral("复制当前层到所有层"));
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
    pushUndoSnapshot(QStringLiteral("随机当前层"));
    randomizeStack(sc->layerEffects[k]);
    m_dirty = true;
    emit effectsChanged();
}

void ProjectController::randomizeAllLayers(bool sameSeedAllLayers)
{
    auto* sc = m_project.currentScheme();
    if (!sc || sc->isBuiltin) return;
    if (sc->locked) return;     // 🔒 锁住的方案不参与变色
    pushUndoSnapshot(QStringLiteral("随机所有层"));

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
    pushUndoSnapshot(includeBaked ? QStringLiteral("随机全部方案")
                                  : QStringLiteral("随机可编辑方案"));
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
    pushUndoSnapshot(QStringLiteral("新建方案"));
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
    pushUndoSnapshot(QStringLiteral("删除方案"));
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
    pushUndoSnapshot(QStringLiteral("重命名方案"));
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
    pushUndoSnapshot(locked ? QStringLiteral("锁定方案") : QStringLiteral("解锁方案"));
    sc.locked = locked;
    m_dirty = true;
    emit schemesChanged();
}

// ===========================================================================
// Undo / Redo 栈 (简易快照)
// ===========================================================================

ProjectController::UndoSnapshot ProjectController::captureSnapshot(const QString& label) const
{
    UndoSnapshot s;
    s.label = label;
    s.schemes = m_project.schemes;
    s.currentSchemeIndex = m_project.currentSchemeIndex;
    return s;
}

void ProjectController::restoreSnapshot(const UndoSnapshot& s)
{
    m_project.schemes = s.schemes;
    m_project.currentSchemeIndex = s.currentSchemeIndex;
}

void ProjectController::pushUndoSnapshot(const QString& label)
{
    m_undoStack.push_back(captureSnapshot(label));
    while (m_undoStack.size() > kUndoLimit) m_undoStack.removeFirst();
    // 任何新操作 → redo 链失效 (避免歧义状态)
    m_redoStack.clear();
}

void ProjectController::undo()
{
    if (m_undoStack.isEmpty()) return;
    UndoSnapshot prev = m_undoStack.takeLast();
    // 当前态进 redo 栈, 标签沿用同一个 (用户语义: undo 后 redo 回到原状态)
    UndoSnapshot cur = captureSnapshot(prev.label);
    m_redoStack.push_back(std::move(cur));
    while (m_redoStack.size() > kUndoLimit) m_redoStack.removeFirst();

    restoreSnapshot(prev);
    m_dirty = true;
    emit schemesChanged();
    emit currentSchemeChanged();
    emit effectsChanged();
}

void ProjectController::redo()
{
    if (m_redoStack.isEmpty()) return;
    UndoSnapshot next = m_redoStack.takeLast();
    // 当前态进 undo (恢复 redo 之前的状态以便再 Ctrl+Z)
    m_undoStack.push_back(captureSnapshot(next.label));
    while (m_undoStack.size() > kUndoLimit) m_undoStack.removeFirst();

    restoreSnapshot(next);
    m_dirty = true;
    emit schemesChanged();
    emit currentSchemeChanged();
    emit effectsChanged();
}

// ===========================================================================
// 方案画廊新增: 细化方案 / 配色方案转移
// ===========================================================================

bool ProjectController::ensureSchemeEditable(int schemeIdx,
                                             bool allowConvertBaked,
                                             QString* errorOut)
{
    if (schemeIdx < 0 || schemeIdx >= m_project.schemes.size()) {
        if (errorOut) *errorOut = QStringLiteral("方案索引非法");
        return false;
    }
    Scheme& sc = m_project.schemes[schemeIdx];
    if (sc.isBuiltin) {
        if (errorOut) *errorOut = QStringLiteral("本体方案不可编辑");
        return false;
    }
    if (!sc.isBaked) return true;     // 已是可编辑

    if (!allowConvertBaked) {
        if (errorOut) *errorOut = QStringLiteral("已烘焙方案需要确认降级");
        return false;
    }

    // 降级: 清空 add_lut 引用, 补齐每层空 EffectStack
    sc.isBaked = false;
    sc.layerLutPath.clear();
    for (const auto& l : m_project.layers) {
        if (!sc.layerEffects.contains(l.key())) {
            sc.layerEffects.insert(l.key(), EffectStack{});
        }
    }
    m_dirty = true;
    emit schemesChanged();
    return true;
}

bool ProjectController::applyRefinedLayerEffects(int schemeIdx,
                                                 const QHash<QString, EffectStack>& refinedEffects,
                                                 QString* errorOut)
{
    if (schemeIdx < 0 || schemeIdx >= m_project.schemes.size()) {
        if (errorOut) *errorOut = QStringLiteral("方案索引非法");
        return false;
    }
    Scheme& sc = m_project.schemes[schemeIdx];
    if (sc.isBuiltin) {
        if (errorOut) *errorOut = QStringLiteral("本体方案不可写入效果");
        return false;
    }
    if (sc.isBaked) {
        if (errorOut) *errorOut = QStringLiteral("已烘焙方案需要先降级再写入");
        return false;
    }

    // 收集有效 layerKey 集合 (project.layers 提供)
    QSet<QString> validKeys;
    for (const auto& l : m_project.layers) validKeys.insert(l.key());

    // 先快照, 再写入 (失败也保留快照, 用户可 Ctrl+Z 回滚)
    pushUndoSnapshot(QStringLiteral("细化方案保存"));

    int written = 0;
    for (auto it = refinedEffects.constBegin(); it != refinedEffects.constEnd(); ++it) {
        if (!validKeys.contains(it.key())) continue;
        sc.layerEffects[it.key()] = it.value();
        ++written;
    }
    if (written == 0) {
        if (errorOut) *errorOut = QStringLiteral("没有可写入的层");
        // 没改东西, 撤回这一次快照避免污染栈
        if (!m_undoStack.isEmpty()) m_undoStack.removeLast();
        return false;
    }
    m_dirty = true;
    emit effectsChanged();
    emit schemesChanged();      // 缩略图栏需要重烘
    return true;
}

bool ProjectController::transferSchemeColorGroup(int sourceSchemeIdx,
                                                 int targetSchemeIdx,
                                                 const QString& groupKey,
                                                 QString* errorOut)
{
    if (sourceSchemeIdx == targetSchemeIdx) return true; // 同方案, 无操作
    if (sourceSchemeIdx < 0 || sourceSchemeIdx >= m_project.schemes.size()
     || targetSchemeIdx < 0 || targetSchemeIdx >= m_project.schemes.size()) {
        if (errorOut) *errorOut = QStringLiteral("方案索引非法");
        return false;
    }
    Scheme& src = m_project.schemes[sourceSchemeIdx];
    Scheme& dst = m_project.schemes[targetSchemeIdx];
    if (src.isBuiltin || dst.isBuiltin) {
        if (errorOut) *errorOut = QStringLiteral("本体方案不可作为源或目标");
        return false;
    }

    // 解析 groupKey → 实际要复制的 layer key 列表
    QStringList layerKeys;
    if (groupKey == QStringLiteral("body")) {
        for (const auto& l : m_project.layers) {
            if (l.kind == LayerKind::Body) layerKeys << l.key();
        }
    } else if (groupKey == QStringLiteral("addon")) {
        for (const auto& l : m_project.layers) {
            if (l.kind == LayerKind::Addon) layerKeys << l.key();
        }
    } else if (groupKey.startsWith(QStringLiteral("num:"))) {
        const QString numStr = groupKey.mid(4);
        bool ok = false;
        const int n = numStr.toInt(&ok);
        if (!ok) {
            if (errorOut) *errorOut = QStringLiteral("分组数字解析失败: %1").arg(groupKey);
            return false;
        }
        for (const auto& l : m_project.layers) {
            if (l.kind == LayerKind::Numbered && l.numberedIdx == n) layerKeys << l.key();
        }
    } else {
        if (errorOut) *errorOut = QStringLiteral("未识别的分组: %1").arg(groupKey);
        return false;
    }

    if (layerKeys.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("分组 [%1] 在当前资源树下没有对应层").arg(groupKey);
        return false;
    }

    // 早期拒绝 "已烘焙 → 可编辑" 这条不支持的路径, 不进栈
    if (src.isBaked && !dst.isBaked) {
        if (errorOut) *errorOut = QStringLiteral(
            "已烘焙方案不能转移到可编辑方案 (源是 PNG LUT, 不能反推效果参数)");
        return false;
    }

    pushUndoSnapshot(QStringLiteral("配色方案转移"));

    // 按 4 种组合落地
    const bool srcBaked = src.isBaked;
    const bool dstBaked = dst.isBaked;

    if (!srcBaked && !dstBaked) {
        // 可编辑 → 可编辑: 复制 EffectStack
        for (const QString& k : layerKeys) {
            auto it = src.layerEffects.find(k);
            if (it == src.layerEffects.end()) {
                dst.layerEffects[k] = EffectStack{};
            } else {
                dst.layerEffects[k] = it.value();
            }
        }
    } else if (!srcBaked && dstBaked) {
        // 可编辑 → 已烘焙: 目标降级后复制
        if (!ensureSchemeEditable(targetSchemeIdx, true, errorOut)) return false;
        for (const QString& k : layerKeys) {
            auto it = src.layerEffects.find(k);
            if (it == src.layerEffects.end()) {
                dst.layerEffects[k] = EffectStack{};
            } else {
                dst.layerEffects[k] = it.value();
            }
        }
    } else if (srcBaked && dstBaked) {
        // 已烘焙 → 已烘焙: 复制 layerLutPath
        for (const QString& k : layerKeys) {
            const QString p = src.layerLutPath.value(k);
            if (p.isEmpty()) dst.layerLutPath.remove(k);
            else             dst.layerLutPath.insert(k, p);
        }
    }
    // (srcBaked && !dstBaked) 已在 push 前拦截, 不会到这里.

    m_dirty = true;
    emit effectsChanged();
    emit schemesChanged();
    return true;
}

// ===========================================================================
// P0 智能随机 (Palette + LayerSlot 驱动)
// ===========================================================================

SchemePalette& ProjectController::ensurePaletteForScheme(int schemeIdx)
{
    Q_ASSERT(schemeIdx >= 0 && schemeIdx < m_project.schemes.size());
    Scheme& sc = m_project.schemes[schemeIdx];
    if (!sc.palette.has_value()) {
        sc.palette = generatePalette(schemeIdx, QRandomGenerator::global()->generate());
    }
    return sc.palette.value();
}

void ProjectController::smartRandomizeCurrentLayer()
{
    auto* sc = m_project.currentScheme();
    if (!sc || sc->isBuiltin) return;
    if (sc->locked) return;

    const QString& k = m_project.currentLayerKey;
    if (k.isEmpty()) return;

    // 找到对应层 (取 slot 用)
    const LayerData* layer = nullptr;
    for (const auto& l : m_project.layers) {
        if (l.key() == k) { layer = &l; break; }
    }
    if (!layer) return;

    // Skin 跳过 (与 randomizeStackBySlot Skin 分支等价, 但提前 short-circuit)
    if (m_project.isSkinSafe(*layer)) return;

    pushUndoSnapshot(QStringLiteral("智能随机当前层"));
    const SchemePalette& palette = ensurePaletteForScheme(m_project.currentSchemeIndex);
    const LayerSlot slot = m_project.slotFor(*layer);
    const LayerColorSlot colorSlot = m_project.colorSlotFor(*layer);

    randomizeStackBySlot(sc->layerEffects[k], slot, colorSlot, palette);
    m_dirty = true;
    emit effectsChanged();
}

void ProjectController::smartRandomizeAllLayers()
{
    auto* sc = m_project.currentScheme();
    if (!sc || sc->isBuiltin) return;
    if (sc->locked) return;

    pushUndoSnapshot(QStringLiteral("智能随机所有层"));
    // 强制重生 palette: "智能所有层 = 换一套完整配色"
    sc->palette = generatePalette(m_project.currentSchemeIndex,
                                   QRandomGenerator::global()->generate());
    const SchemePalette& palette = sc->palette.value();

    int processed = 0;
    for (const auto& l : m_project.layers) {
        if (!m_project.isLayerVisible(l)) continue;
        if (m_project.isSkinSafe(l)) continue;
        const LayerSlot slot = m_project.slotFor(l);
        const LayerColorSlot colorSlot = m_project.colorSlotFor(l);
        randomizeStackBySlot(sc->layerEffects[l.key()], slot, colorSlot, palette);
        ++processed;
    }
    if (processed > 0) { m_dirty = true; emit effectsChanged(); }
}

void ProjectController::smartRandomizeAllSchemes(bool includeBaked)
{
    pushUndoSnapshot(includeBaked ? QStringLiteral("智能随机全部方案")
                                  : QStringLiteral("智能随机可编辑方案"));
    int processed = 0;
    bool anyTagChange = false;

    // 记录当前选中方案名 (重排后用名字找回新 idx)
    QString currentName;
    if (m_project.currentSchemeIndex >= 0
        && m_project.currentSchemeIndex < m_project.schemes.size()) {
        currentName = m_project.schemes[m_project.currentSchemeIndex].name;
    }

    for (int i = 0; i < m_project.schemes.size(); ++i) {
        Scheme& sc = m_project.schemes[i];
        if (sc.isBuiltin) continue;
        if (sc.locked) continue;
        if (sc.isBaked && !includeBaked) continue;

        // 已烘焙降级 (复用旧 randomizeAllSchemes 逻辑)
        if (sc.isBaked && includeBaked) {
            sc.isBaked = false;
            sc.layerLutPath.clear();
            anyTagChange = true;
            for (const auto& l : m_project.layers) {
                if (!sc.layerEffects.contains(l.key())) {
                    sc.layerEffects.insert(l.key(), EffectStack{});
                }
            }
        }

        // 每方案按 idx 取风格 + 新 seed 抖动. 不复用旧 palette: 智能全部 = 全部重生.
        sc.palette = generatePalette(i, QRandomGenerator::global()->generate());
        const SchemePalette& palette = sc.palette.value();

        for (const auto& l : m_project.layers) {
            if (!m_project.isLayerVisible(l)) continue;
            if (m_project.isSkinSafe(l)) continue;
            const LayerSlot slot = m_project.slotFor(l);
            const LayerColorSlot colorSlot = m_project.colorSlotFor(l);
            randomizeStackBySlot(sc.layerEffects[l.key()], slot, colorSlot, palette);
            ++processed;
        }
    }

    // === 重排: 锁住的方案排在前面 (idx 1 起依次), 未锁的排后面 ===
    // 与旧 randomizeAllSchemes 完全一致. palette 跟着 Scheme 走, 不会串色.
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
        for (int i = 0; i < merged.size(); ++i) {
            if (merged[i].name != m_project.schemes[i].name) { reordered = true; break; }
        }
        if (reordered) {
            m_project.schemes = std::move(merged);
            for (int i = 0; i < m_project.schemes.size(); ++i) {
                Scheme& sc = m_project.schemes[i];
                if (sc.isBuiltin) continue;
                QString rest = sc.name;
                QRegularExpression re("^方案\\s*\\d+\\s*-\\s*");
                rest.remove(re);
                sc.name = QString("方案 %1 - %2").arg(i).arg(rest);
            }
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

// ===========================================================================
// 智能 + 随机 混合 (每个参数 = 智能 * 0.5 + 随机 * 0.5 中间态)
// ===========================================================================
namespace {

// hue 走最短路径平均: [-180, 180] 范围.
//   e.g. -170 与 +170 直接 (a+b)/2 = 0, 但实际最短路径是 180.
int averageHueShortPath(int a, int b)
{
    a = std::clamp(a, -180, 180);
    b = std::clamp(b, -180, 180);
    int diff = b - a;
    if (diff >  180) diff -= 360;
    if (diff < -180) diff += 360;
    int mid = a + diff / 2;
    if (mid >  180) mid -= 360;
    if (mid < -180) mid += 360;
    return mid;
}

inline int blendInt(int x, int y) { return (x + y) / 2; }

// 把两份 EffectStack 按 50/50 插值到 out.
//   - enabled[i]: OR (任一开启则开)
//   - 数值参数: 算术平均
//   - hue: 最短路径平均
//   - curves / chMix.monochrome / photoFilter.preset: 智能版优先
//   - shadowProtectThreshold: 智能版优先
void blendStack(const EffectStack& smart, const EffectStack& legacy, EffectStack& out)
{
    out.reset();

    // enabled OR
    for (size_t i = 0; i < EffectStack::kCount; ++i) {
        out.enabled[i] = smart.enabled[i] || legacy.enabled[i];
    }
    out.shadowProtectThreshold = smart.shadowProtectThreshold;

    // HSL
    out.hsl.hue        = averageHueShortPath(smart.hsl.hue, legacy.hsl.hue);
    out.hsl.saturation = blendInt(smart.hsl.saturation, legacy.hsl.saturation);
    out.hsl.lightness  = blendInt(smart.hsl.lightness,  legacy.hsl.lightness);

    // BrtCtr
    out.brtCtr.brightness = blendInt(smart.brtCtr.brightness, legacy.brtCtr.brightness);
    out.brtCtr.contrast   = blendInt(smart.brtCtr.contrast,   legacy.brtCtr.contrast);

    // Curves: 智能版优先 (智能版才用预设曲线; 旧随机几乎不开曲线, 强行插值会破坏曲线形状)
    out.curves = smart.curves;

    // ChMix: 数值平均
    out.chMix.rr = blendInt(smart.chMix.rr, legacy.chMix.rr);
    out.chMix.rg = blendInt(smart.chMix.rg, legacy.chMix.rg);
    out.chMix.rb = blendInt(smart.chMix.rb, legacy.chMix.rb);
    out.chMix.r_const = blendInt(smart.chMix.r_const, legacy.chMix.r_const);
    out.chMix.gr = blendInt(smart.chMix.gr, legacy.chMix.gr);
    out.chMix.gg = blendInt(smart.chMix.gg, legacy.chMix.gg);
    out.chMix.gb = blendInt(smart.chMix.gb, legacy.chMix.gb);
    out.chMix.g_const = blendInt(smart.chMix.g_const, legacy.chMix.g_const);
    out.chMix.br = blendInt(smart.chMix.br, legacy.chMix.br);
    out.chMix.bg = blendInt(smart.chMix.bg, legacy.chMix.bg);
    out.chMix.bb = blendInt(smart.chMix.bb, legacy.chMix.bb);
    out.chMix.b_const = blendInt(smart.chMix.b_const, legacy.chMix.b_const);
    out.chMix.monochrome = smart.chMix.monochrome;

    // ColorBal: 9 个分量 + preserveLuma 平均
    out.colorBal.sR = blendInt(smart.colorBal.sR, legacy.colorBal.sR);
    out.colorBal.sG = blendInt(smart.colorBal.sG, legacy.colorBal.sG);
    out.colorBal.sB = blendInt(smart.colorBal.sB, legacy.colorBal.sB);
    out.colorBal.mR = blendInt(smart.colorBal.mR, legacy.colorBal.mR);
    out.colorBal.mG = blendInt(smart.colorBal.mG, legacy.colorBal.mG);
    out.colorBal.mB = blendInt(smart.colorBal.mB, legacy.colorBal.mB);
    out.colorBal.hR = blendInt(smart.colorBal.hR, legacy.colorBal.hR);
    out.colorBal.hG = blendInt(smart.colorBal.hG, legacy.colorBal.hG);
    out.colorBal.hB = blendInt(smart.colorBal.hB, legacy.colorBal.hB);
    out.colorBal.preserveLuma = smart.colorBal.preserveLuma;

    // PhotoFilter:
    //   density 取算术平均;
    //   preset/filterR/G/B 智能版优先 (RGB 三通道独立插值会出怪色)
    out.photoFilter.preset      = smart.photoFilter.preset;
    out.photoFilter.filterR     = smart.photoFilter.filterR;
    out.photoFilter.filterG     = smart.photoFilter.filterG;
    out.photoFilter.filterB     = smart.photoFilter.filterB;
    out.photoFilter.density     = blendInt(smart.photoFilter.density, legacy.photoFilter.density);
    out.photoFilter.preserveLuma = smart.photoFilter.preserveLuma;

    // Vibrance
    out.vibrance.vibrance   = blendInt(smart.vibrance.vibrance,   legacy.vibrance.vibrance);
    out.vibrance.saturation = blendInt(smart.vibrance.saturation, legacy.vibrance.saturation);
}

} // namespace

void ProjectController::mixRandomizeAllSchemes(bool includeBaked)
{
    pushUndoSnapshot(includeBaked ? QStringLiteral("智能+随机 全部方案")
                                  : QStringLiteral("智能+随机 可编辑方案"));
    int processed = 0;
    bool anyTagChange = false;

    QString currentName;
    if (m_project.currentSchemeIndex >= 0
        && m_project.currentSchemeIndex < m_project.schemes.size()) {
        currentName = m_project.schemes[m_project.currentSchemeIndex].name;
    }

    for (int i = 0; i < m_project.schemes.size(); ++i) {
        Scheme& sc = m_project.schemes[i];
        if (sc.isBuiltin) continue;
        if (sc.locked) continue;
        if (sc.isBaked && !includeBaked) continue;

        if (sc.isBaked && includeBaked) {
            sc.isBaked = false;
            sc.layerLutPath.clear();
            anyTagChange = true;
            for (const auto& l : m_project.layers) {
                if (!sc.layerEffects.contains(l.key())) {
                    sc.layerEffects.insert(l.key(), EffectStack{});
                }
            }
        }

        // 该方案生成 palette (供 smart 用)
        sc.palette = generatePalette(i, QRandomGenerator::global()->generate());
        const SchemePalette& palette = sc.palette.value();

        for (const auto& l : m_project.layers) {
            if (!m_project.isLayerVisible(l)) continue;
            if (m_project.isSkinSafe(l)) continue;

            const LayerSlot slot = m_project.slotFor(l);
            const LayerColorSlot colorSlot = m_project.colorSlotFor(l);

            // 生成两份独立的栈, 50/50 插值
            EffectStack smartStack;
            randomizeStackBySlot(smartStack, slot, colorSlot, palette,
                                 QRandomGenerator::global()->generate());

            EffectStack legacyStack;
            randomizeStack(legacyStack, QRandomGenerator::global()->generate());

            EffectStack blended;
            blendStack(smartStack, legacyStack, blended);

            sc.layerEffects[l.key()] = blended;
            ++processed;
        }
    }

    // 重排 (跟 smartRandomizeAllSchemes 一致)
    bool reordered = false;
    if (m_project.schemes.size() > 2) {
        QVector<Scheme> head;
        QVector<Scheme> locked;
        QVector<Scheme> rest;
        for (int i = 0; i < m_project.schemes.size(); ++i) {
            const auto& sc = m_project.schemes[i];
            if (sc.isBuiltin)    head.push_back(sc);
            else if (sc.locked)  locked.push_back(sc);
            else                 rest.push_back(sc);
        }
        QVector<Scheme> merged;
        merged.reserve(m_project.schemes.size());
        merged += head; merged += locked; merged += rest;
        for (int i = 0; i < merged.size(); ++i) {
            if (merged[i].name != m_project.schemes[i].name) { reordered = true; break; }
        }
        if (reordered) {
            m_project.schemes = std::move(merged);
            for (int i = 0; i < m_project.schemes.size(); ++i) {
                Scheme& sc = m_project.schemes[i];
                if (sc.isBuiltin) continue;
                QString rest = sc.name;
                QRegularExpression re("^方案\\s*\\d+\\s*-\\s*");
                rest.remove(re);
                sc.name = QString("方案 %1 - %2").arg(i).arg(rest);
            }
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

void ProjectController::setLayerSlot(const QString& layerKey, LayerSlot slot)
{
    if (layerKey.isEmpty()) return;

    bool changed = false;

    if (slot == LayerSlot::Unknown) {
        // 清除手动指定
        if (m_project.layerSlots.remove(layerKey)) changed = true;
    } else {
        const LayerSlot old = m_project.layerSlots.value(layerKey, LayerSlot::Unknown);
        if (old != slot) {
            m_project.layerSlots.insert(layerKey, slot);
            changed = true;
        }
        if (m_project.layerColorSlots.remove(layerKey)) changed = true;
    }

    // skinSafe 双向同步:
    //   slot == Skin    → 强制加入 skinSafeLayerKeys
    //   slot != Skin && != Unknown → 取消 skinSafeLayerKeys (用户显式选了别的)
    //   slot == Unknown → 不动 skinSafeLayerKeys (清除指定不影响保护状态)
    if (slot == LayerSlot::Skin) {
        if (!m_project.skinSafeLayerKeys.contains(layerKey)) {
            m_project.skinSafeLayerKeys.insert(layerKey);
            changed = true;
        }
    } else if (slot != LayerSlot::Unknown) {
        if (m_project.skinSafeLayerKeys.remove(layerKey)) changed = true;
    }

    if (changed) {
        m_dirty = true;
        emit visibilityChanged();   // 复用: LayerTreePanel 刷 emoji 前缀
        emit effectsChanged();      // 缩略图重烘 (slot 变了, 智能随机结果会变)
    }
}

void ProjectController::setLayerColorSlot(const QString& layerKey, LayerColorSlot slot)
{
    if (layerKey.isEmpty()) return;

    bool changed = false;
    if (slot == LayerColorSlot::Auto) {
        if (m_project.layerColorSlots.remove(layerKey)) changed = true;
    } else {
        const LayerColorSlot old = m_project.layerColorSlots.value(layerKey, LayerColorSlot::Auto);
        if (old != slot) {
            m_project.layerColorSlots.insert(layerKey, slot);
            changed = true;
        }
        if (m_project.layerSlots.remove(layerKey)) changed = true;
        if (m_project.skinSafeLayerKeys.remove(layerKey)) changed = true;
    }

    if (changed) {
        m_dirty = true;
        emit visibilityChanged();
        emit effectsChanged();
    }
}

void ProjectController::setAllLayerColorSlots(LayerColorSlot slot)
{
    bool changed = false;

    if (slot == LayerColorSlot::Auto) {
        // "全部自动" = 清空所有手动指定 (含 layerColorSlots / layerSlots / skinSafe).
        // 这样空白处右键 "取消所有层级预设" 后, 列表上不会再有任何 emoji 残留.
        if (!m_project.layerColorSlots.isEmpty()) { m_project.layerColorSlots.clear(); changed = true; }
        if (!m_project.layerSlots.isEmpty())      { m_project.layerSlots.clear();      changed = true; }
        if (!m_project.skinSafeLayerKeys.isEmpty()){ m_project.skinSafeLayerKeys.clear(); changed = true; }
    } else {
        if (!m_project.layerSlots.isEmpty()) {
            m_project.layerSlots.clear();
            changed = true;
        }
        if (!m_project.skinSafeLayerKeys.isEmpty()) {
            m_project.skinSafeLayerKeys.clear();
            changed = true;
        }
        for (const auto& l : m_project.layers) {
            const LayerColorSlot old = m_project.layerColorSlots.value(l.key(), LayerColorSlot::Auto);
            if (old != slot) {
                m_project.layerColorSlots.insert(l.key(), slot);
                changed = true;
            }
        }
    }

    if (changed) {
        m_dirty = true;
        emit visibilityChanged();
        emit effectsChanged();
    }
}

void ProjectController::randomizeAllLayerColorSlots()
{
    if (m_project.layers.isEmpty()) return;

    // 候选: Red..Gray (跳过 Auto), 共 12 个.
    static const std::array<LayerColorSlot, 12> kCandidates = {
        LayerColorSlot::Red,    LayerColorSlot::Orange, LayerColorSlot::Yellow,
        LayerColorSlot::Green,  LayerColorSlot::Cyan,   LayerColorSlot::Blue,
        LayerColorSlot::Purple, LayerColorSlot::Pink,   LayerColorSlot::Black,
        LayerColorSlot::White,  LayerColorSlot::Silver, LayerColorSlot::Gray,
    };

    bool changed = false;
    if (!m_project.layerSlots.isEmpty())          { m_project.layerSlots.clear();          changed = true; }
    if (!m_project.skinSafeLayerKeys.isEmpty())   { m_project.skinSafeLayerKeys.clear();   changed = true; }

    auto* rng = QRandomGenerator::global();
    for (const auto& l : m_project.layers) {
        const LayerColorSlot pick = kCandidates[rng->bounded((quint32)kCandidates.size())];
        const LayerColorSlot old  = m_project.layerColorSlots.value(l.key(), LayerColorSlot::Auto);
        if (old != pick) {
            m_project.layerColorSlots.insert(l.key(), pick);
            changed = true;
        }
    }

    if (changed) {
        m_dirty = true;
        emit visibilityChanged();
        emit effectsChanged();
    }
}

void ProjectController::randomizeAllLayerSlots()
{
    if (m_project.layers.isEmpty()) return;

    // 候选: Hair..WeaponNonMetal (跳过 Unknown / Skin), 共 7 个.
    // 跳过 Skin: 避免随机把普通层误标为肤色保护, 让该层不再可变色.
    static const std::array<LayerSlot, 7> kCandidates = {
        LayerSlot::Hair,        LayerSlot::Clothing,    LayerSlot::Skirt,
        LayerSlot::Decor01,     LayerSlot::Decor02,
        LayerSlot::WeaponMetal, LayerSlot::WeaponNonMetal,
    };

    bool changed = false;
    if (!m_project.layerColorSlots.isEmpty())   { m_project.layerColorSlots.clear();   changed = true; }
    if (!m_project.skinSafeLayerKeys.isEmpty()) { m_project.skinSafeLayerKeys.clear(); changed = true; }

    auto* rng = QRandomGenerator::global();
    for (const auto& l : m_project.layers) {
        const LayerSlot pick = kCandidates[rng->bounded((quint32)kCandidates.size())];
        const LayerSlot old  = m_project.layerSlots.value(l.key(), LayerSlot::Unknown);
        if (old != pick) {
            m_project.layerSlots.insert(l.key(), pick);
            changed = true;
        }
    }

    if (changed) {
        m_dirty = true;
        emit visibilityChanged();
        emit effectsChanged();
    }
}

} // namespace HighPro
