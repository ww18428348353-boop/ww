#pragma once

#include <QWidget>

class QTreeWidget;
class QTreeWidgetItem;

namespace HighPro {

// 资源树:
//   显示顺序 (UI 视角, AE 风格倒序): addon → 00..04 → body
//   合成顺序 (实际渲染): body → 00..04 → addon  (Project::layers 仍按合成顺序)
//
// 点击行为:
//   body / 数字层  : checkbox 显隐
//   addon (单选)   : 选中 = 显示该子层; 已选项再点 = 取消显示 (无 addon 显示)
class LayerTreePanel : public QWidget
{
    Q_OBJECT
public:
    explicit LayerTreePanel(QWidget* parent = nullptr);

public slots:
    void refresh();             // 完整重建 (loadProject 后)
    void syncCheckStates();     // 仅同步 checkbox, 不重建结构 (visibilityChanged 后)

signals:
    void currentLayerChanged(const QString& layerKey);

private slots:
    void onItemChanged(QTreeWidgetItem* it, int col);
    void onItemSelected();
    void onContextMenu(const QPoint& pos);

private:
    QTreeWidget* m_tree{ nullptr };
    bool         m_suppressSignal = false;
};

} // namespace HighPro
