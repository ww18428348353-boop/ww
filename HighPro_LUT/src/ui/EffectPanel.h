#pragma once

#include <QWidget>

class QVBoxLayout;
class QScrollArea;

namespace HighPro {

// 7 个 AE 效果手风琴 + 滑块面板.
// 跟随 ProjectController::currentLayerKeyChanged 切换编辑对象.
// 任意滑块变 → 30ms debounce → 通知 ProjectController.
class EffectPanel : public QWidget
{
    Q_OBJECT
public:
    explicit EffectPanel(QWidget* parent = nullptr);

    void rebuildForCurrentLayer();

private slots:
    void onCurrentLayerChanged();

private:
    void buildUi();
    QWidget* buildSection(const QString& title, int idx, QWidget* body);

    QScrollArea*  m_scroll{ nullptr };
    QWidget*      m_content{ nullptr };
    QVBoxLayout*  m_contentLayout{ nullptr };

    // 用 sectionWidgets 装 7 个 section 的 body 容器, rebuild 时清空内部
    QVector<QWidget*> m_sections;
};

} // namespace HighPro
