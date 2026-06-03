#pragma once

#include "core/ColorEffect.h"
#include <QDialog>
#include <QHash>
#include <QString>
#include <QTimer>

class QListWidget;
class QStackedWidget;
class QLabel;

namespace HighPro {

// ❤️细化方案 弹窗.
//
// 用途: 在方案画廊右键 "❤️细化方案" 后打开. 针对某个 (已可编辑) 方案,
//       只允许调整每层的 [色相/饱和度] 与 [亮度/对比度] 两个效果.
//       其他 5 个效果完全保留 (不显示, 不修改).
//
// 数据流 (实时预览版):
//   1) 构造时把目标方案的 layerEffects 深拷贝到 m_snapshotForReset (取消时回滚用)
//      同时 m_workingEffects = 当前 Project 状态 (与 Project 共享语义, 不再分两套)
//   2) 用户在左侧选层 → 右侧 stacked 显示该层的 HSL / BrtCtr 滑块
//   3) 调整滑块 → ① 写 m_workingEffects ② 通过 30ms debounce 直接刷 Project + effectsChanged
//   4) 保存: 标 dirty + schemesChanged (供画廊缩略图重烘)
//   5) 取消: 把 m_snapshotForReset 写回 Project + 通知刷新, 等效于撤销所有改动
class SchemeRefineDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SchemeRefineDialog(int schemeIdx, QWidget* parent = nullptr);
    ~SchemeRefineDialog() override = default;

protected:
    void reject() override;        // 取消 / 关闭 / ESC → 回滚 snapshot
    void accept() override;        // 保存 → 标 dirty + schemesChanged

private slots:
    void onLayerChanged(int row);
    void onResetCurrentLayer();

private:
    void buildUi();
    void populateLayerList();
    QWidget* buildEffectPanel(const QString& layerKey);
    EffectStack& workingFor(const QString& layerKey);

    // 调度: 把 m_workingEffects[layerKey] 同步到 Project 的对应方案, 触发实时预览
    void scheduleApplyToProject(const QString& layerKey);
    void flushPendingToProject();         // debounce 触发 → 真写 Project + emit effectsChanged

    // 拖动结束 → 提交本次会话, 把会话起点快照入 undo 栈 (清空 session)
    void commitDragSession();

    int                          m_schemeIdx = -1;
    QHash<QString, EffectStack>  m_workingEffects;   // layerKey -> 当前编辑值
    QStringList                  m_layerKeys;        // 与 m_list 行号一一对应
    QHash<QString, EffectStack>  m_snapshotForReset; // 进入弹窗时的初始值 (取消时整体回滚)

    QListWidget*    m_list     = nullptr;
    QStackedWidget* m_stack    = nullptr;
    QLabel*         m_titleLbl = nullptr;

    // 实时预览的 debounce: 30ms (与 EffectPanel 节流口径一致, 避免高频拖动卡顿)
    QTimer          m_applyDebounce;
    QSet<QString>   m_dirtyLayers;        // 待 flush 的层

    // === 拖动会话 (用于 undo) ===
    // 用户每一次"完整的拖动 / spinbox 修改" = 1 个 undo 步, 而非每次 valueChanged.
    //
    // scheduleApplyToProject  → 写 working + debounce 刷预览 + 若当前无会话则记录起点快照
    // commitDragSession       → 拖动结束 (sliderReleased / spin editingFinished / 键盘单步),
    //                          把起点快照入 undo 栈, 清空 session.
    bool                                  m_sessionActive = false;
    QHash<QString, EffectStack>           m_sessionStartSnapshot;

    // 弹窗内 Undo / Redo 栈 — 每个 undo 步是一次"拖动会话"前的全层 EffectStack 快照.
    //   Ctrl+Z 回到上一个 flush 之前的滑块状态; Ctrl+Y 重做.
    //   栈深度 100. 与 ProjectController 全局 undo 解耦 (它针对方案级别批量操作).
    QVector<QHash<QString, EffectStack>> m_localUndo;
    QVector<QHash<QString, EffectStack>> m_localRedo;
    static constexpr int kLocalUndoLimit = 100;

    void localUndo();
    void localRedo();
    void rebuildCurrentLayerPanel();      // Undo/Redo 后用新值重建当前层 UI
};

} // namespace HighPro
