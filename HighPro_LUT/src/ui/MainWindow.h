#pragma once

#include <QMainWindow>

class QAction;
class QDockWidget;
class QMenu;

namespace HighPro {

class PreviewPanel;
class LayerTreePanel;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* e) override;

private slots:
    void onOpenSource();
    void onOpenOutput();
    void onAbout();

    // M8: 工程文件 .hplut.json
    void onProjectNew();
    void onProjectOpen();
    void onProjectSave();
    void onProjectSaveAs();

private:
    void buildMenus();
    void buildCentral();
    void buildDocks();
    void restoreGeometry();
    void persistGeometry();

    // 最近工程子菜单 (上限 10).
    void rebuildRecentProjectsMenu();
    bool openProjectFromPath(const QString& path);   // 给最近列表点击复用

    PreviewPanel*    m_preview{ nullptr };
    LayerTreePanel*  m_layerTree{ nullptr };
    QDockWidget*     m_dockLayer{ nullptr };
    QDockWidget*     m_dockEffect{ nullptr };
    QDockWidget*     m_dockScheme{ nullptr };

    QAction* m_actOpenSource{ nullptr };
    QAction* m_actOpenOutput{ nullptr };
    QAction* m_actExit{ nullptr };
    QAction* m_actAbout{ nullptr };
    QMenu*   m_menuRecent{ nullptr };       // "最近工程" 子菜单
};

} // namespace HighPro
