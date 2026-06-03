#include "MainWindow.h"
#include "PreviewPanel.h"
#include "LayerTreePanel.h"
#include "EffectPanel.h"
#include "SchemePanel.h"
#include "D3DWidget.h"
#include "app/AppSettings.h"
#include "app/ProjectController.h"
#include "render/DebugDumper.h"
#include "render/LutBaker.h"
#include "render/PngExporter.h"

#include <QMenuBar>
#include <QMenu>
#include <QStatusBar>
#include <QDockWidget>
#include <QFileDialog>
#include <QMessageBox>
#include <QLabel>
#include <QListWidget>
#include <QCloseEvent>
#include <QApplication>
#include <QStandardPaths>
#include <QDir>
#include <QVariantMap>
#include <QPushButton>

namespace HighPro {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("HighPro_LUT - 高性能序列帧角色换色工具"));
    setMinimumSize(1024, 640);

    // 允许 dock 嵌套 split (而不是只能 tab 堆叠).
    // 这样用户拖画廊到资源树/颜色效果旁边时, 可以并排成两列, 而不是切换 tab.
    setDockNestingEnabled(true);
    setDockOptions(QMainWindow::AnimatedDocks
                   | QMainWindow::AllowNestedDocks
                   | QMainWindow::AllowTabbedDocks);

    buildCentral();
    buildDocks();
    buildMenus();
    statusBar()->showMessage(QStringLiteral("就绪"));

    restoreGeometry();
}

MainWindow::~MainWindow() = default;

void MainWindow::buildCentral()
{
    m_preview = new PreviewPanel(this);
    setCentralWidget(m_preview);
}

void MainWindow::buildDocks()
{
    // 左侧: 资源树 (固定布局, 用户可拖动)
    m_dockLayer = new QDockWidget(QStringLiteral("资源树"), this);
    m_dockLayer->setObjectName("DockLayerTree");
    m_layerTree = new LayerTreePanel(m_dockLayer);
    m_dockLayer->setWidget(m_layerTree);
    m_dockLayer->setMinimumWidth(180);
    addDockWidget(Qt::LeftDockWidgetArea, m_dockLayer);

    // 底部: 颜色效果 (固定布局, 用户可拖动)
    m_dockEffect = new QDockWidget(QStringLiteral("颜色效果"), this);
    m_dockEffect->setObjectName("DockEffect");
    auto* effPanel = new EffectPanel(m_dockEffect);
    m_dockEffect->setWidget(effPanel);
    m_dockEffect->setMinimumHeight(280);
    addDockWidget(Qt::BottomDockWidgetArea, m_dockEffect);

    // 方案画廊: 默认与"颜色效果" 横向并排 (在底部右半).
    // 用户也可拖到 资源树 旁边并排, 或拖出成浮动窗口.
    m_dockScheme = new QDockWidget(QStringLiteral("方案画廊"), this);
    m_dockScheme->setObjectName("DockScheme");
    auto* schemePanel = new SchemePanel(m_dockScheme);
    m_dockScheme->setWidget(schemePanel);
    m_dockScheme->setMinimumWidth(220);
    // 先 add 到底部区, 再 split: 这样 m_dockEffect 占左, m_dockScheme 占右
    addDockWidget(Qt::BottomDockWidgetArea, m_dockScheme);
    splitDockWidget(m_dockEffect, m_dockScheme, Qt::Horizontal);

    // 资源树点击 → 切换当前编辑层
    connect(m_layerTree, &LayerTreePanel::currentLayerChanged, this, [this](const QString& key){
        ProjectController::instance().setCurrentLayerKey(key);
        const auto& proj = ProjectController::instance().project();
        QString title = QStringLiteral("颜色效果");
        for (const auto& l : proj.layers) {
            if (l.key() == key) { title += QStringLiteral(" (当前层: %1)").arg(l.displayName); break; }
        }
        m_dockEffect->setWindowTitle(title);
    });
    connect(&ProjectController::instance(), &ProjectController::currentLayerKeyChanged,
            this, [this]{
        const auto& proj = ProjectController::instance().project();
        QString title = QStringLiteral("颜色效果");
        for (const auto& l : proj.layers) {
            if (l.key() == proj.currentLayerKey) {
                title += QStringLiteral(" (当前层: %1)").arg(l.displayName);
                break;
            }
        }
        m_dockEffect->setWindowTitle(title);
    });
}

void MainWindow::buildMenus()
{
    auto* menuFile  = menuBar()->addMenu(QStringLiteral("文件(&F)"));
    auto* menuScan  = menuBar()->addMenu(QStringLiteral("扫描(&S)"));
    auto* menuScheme= menuBar()->addMenu(QStringLiteral("方案(&P)"));
    auto* menuExport= menuBar()->addMenu(QStringLiteral("导出(&E)"));
    auto* menuView  = menuBar()->addMenu(QStringLiteral("视图(&V)"));
    auto* menuHelp  = menuBar()->addMenu(QStringLiteral("帮助(&H)"));

    m_actOpenSource = menuFile->addAction(QStringLiteral("打开源目录..."));
    m_actOpenSource->setShortcut(QKeySequence::Open);
    connect(m_actOpenSource, &QAction::triggered, this, &MainWindow::onOpenSource);

    m_actOpenOutput = menuFile->addAction(QStringLiteral("设置输出目录..."));
    connect(m_actOpenOutput, &QAction::triggered, this, &MainWindow::onOpenOutput);

    menuFile->addSeparator();

    // M8: 工程文件 .hplut.json
    auto* actNewProj = menuFile->addAction(QStringLiteral("新建工程"));
    actNewProj->setShortcut(QKeySequence("Ctrl+Shift+N"));
    connect(actNewProj, &QAction::triggered, this, &MainWindow::onProjectNew);

    auto* actOpenProj = menuFile->addAction(QStringLiteral("打开工程..."));
    actOpenProj->setShortcut(QKeySequence("Ctrl+Shift+O"));
    connect(actOpenProj, &QAction::triggered, this, &MainWindow::onProjectOpen);

    auto* actSaveProj = menuFile->addAction(QStringLiteral("保存工程"));
    actSaveProj->setShortcut(QKeySequence::Save);
    connect(actSaveProj, &QAction::triggered, this, &MainWindow::onProjectSave);

    auto* actSaveAsProj = menuFile->addAction(QStringLiteral("工程另存为..."));
    actSaveAsProj->setShortcut(QKeySequence::SaveAs);
    connect(actSaveAsProj, &QAction::triggered, this, &MainWindow::onProjectSaveAs);

    // 最近工程子菜单 (上限 10), 内容由 rebuildRecentProjectsMenu 在打开/保存后刷新.
    menuFile->addSeparator();
    m_menuRecent = menuFile->addMenu(QStringLiteral("最近工程"));
    rebuildRecentProjectsMenu();

    menuFile->addSeparator();
    m_actExit = menuFile->addAction(QStringLiteral("退出"));
    m_actExit->setShortcut(QKeySequence("Ctrl+Q"));
    connect(m_actExit, &QAction::triggered, this, &QMainWindow::close);

    auto* actRescan = menuScan->addAction(QStringLiteral("重新扫描当前源目录"));
    actRescan->setShortcut(QKeySequence("F5"));
    connect(actRescan, &QAction::triggered, this, [this]{
        const QString src = AppSettings::instance().lastSourceDir();
        if (src.isEmpty()) {
            statusBar()->showMessage(QStringLiteral("未设置源目录"), 2000);
            return;
        }
        QString err;
        if (!ProjectController::instance().loadSource(src, &err)) {
            QMessageBox::warning(this, QStringLiteral("扫描失败"), err);
            statusBar()->showMessage(err, 5000);
        } else {
            statusBar()->showMessage(QStringLiteral("已重新扫描: %1").arg(src), 3000);
        }
    });

    // M5: 方案管理 (一期菜单驱动, 批 2 改成画廊 dock)
    {
        auto* actAdd = menuScheme->addAction(QStringLiteral("新增方案"));
        actAdd->setShortcut(QKeySequence("Ctrl+N"));
        connect(actAdd, &QAction::triggered, this, [this]{
            auto& ctl = ProjectController::instance();
            if (ctl.project().layers.isEmpty()) {
                statusBar()->showMessage(QStringLiteral("先打开源目录"), 2000); return;
            }
            if (ctl.schemeCount() >= 28) {
                statusBar()->showMessage(QStringLiteral("方案数已达上限 28"), 3000); return;
            }
            int idx = ctl.addScheme();
            ctl.setCurrentSchemeIndex(idx);
            statusBar()->showMessage(
                QStringLiteral("已新增方案 #%1 (共 %2 个)").arg(idx).arg(ctl.schemeCount()), 3000);
        });

        auto* actDel = menuScheme->addAction(QStringLiteral("删除当前方案"));
        connect(actDel, &QAction::triggered, this, [this]{
            auto& ctl = ProjectController::instance();
            int idx = ctl.currentSchemeIndex();
            if (idx <= 0) {
                statusBar()->showMessage(QStringLiteral("不能删除本体方案"), 2000); return;
            }
            if (!ctl.removeScheme(idx)) return;
            statusBar()->showMessage(
                QStringLiteral("已删除方案 #%1").arg(idx), 3000);
        });

        auto* actNext = menuScheme->addAction(QStringLiteral("切到下一个方案"));
        actNext->setShortcut(QKeySequence("Ctrl+]"));
        connect(actNext, &QAction::triggered, this, [this]{
            auto& ctl = ProjectController::instance();
            int n = ctl.schemeCount();
            if (n <= 1) return;
            int next = (ctl.currentSchemeIndex() + 1) % n;
            ctl.setCurrentSchemeIndex(next);
        });

        auto* actPrev = menuScheme->addAction(QStringLiteral("切到上一个方案"));
        actPrev->setShortcut(QKeySequence("Ctrl+["));
        connect(actPrev, &QAction::triggered, this, [this]{
            auto& ctl = ProjectController::instance();
            int n = ctl.schemeCount();
            if (n <= 1) return;
            int prev = (ctl.currentSchemeIndex() - 1 + n) % n;
            ctl.setCurrentSchemeIndex(prev);
        });
    }
    menuScheme->addSeparator();
    for (int i = 1; i <= 6; ++i) {
        const QString fn = QString("%1.png").arg(i, 2, 10, QChar('0'));
        auto* act = menuScheme->addAction(QStringLiteral("使用 add_lut/%1").arg(fn));
        act->setShortcut(QKeySequence(QString("Ctrl+%1").arg(i)));
        connect(act, &QAction::triggered, this, [this, fn]{
            int n = ProjectController::instance().applyLutToAllLayers(fn);
            statusBar()->showMessage(
                QStringLiteral("已应用 LUT: %1, 命中 %2 层").arg(fn).arg(n), 3000);
        });
    }
    menuScheme->addSeparator();
    auto* actClearLut = menuScheme->addAction(QStringLiteral("清除所有 LUT (恢复本体)"));
    actClearLut->setShortcut(QKeySequence("Ctrl+Shift+0"));
    connect(actClearLut, &QAction::triggered, this, [this]{
        ProjectController::instance().clearAllLayerLut();
        statusBar()->showMessage(QStringLiteral("已清除 LUT, 恢复本体"), 3000);
    });

    // 导出: 共用准备工作 (确认源 / 选择输出根目录 / baker), 返回是否就绪 + 写回 outRoot.
    // 行为: 每次导出都弹文件夹选择对话框 (默认起始 = 上次输出目录, 不存在时退到源目录).
    auto prepareExport = [this](QString& outRoot) -> bool {
        auto& proj = ProjectController::instance().projectMut();
        if (proj.layers.isEmpty()) {
            QMessageBox::information(this, QStringLiteral("导出"),
                QStringLiteral("还没加载源目录, 请先打开源目录"));
            return false;
        }
        // 弹窗起点优先级: proj.outputRoot → 上次输出目录 → 源目录
        QString startDir = proj.outputRoot;
        if (startDir.isEmpty()) startDir = AppSettings::instance().lastOutputDir();
        if (startDir.isEmpty()) startDir = proj.sourceRoot;

        outRoot = QFileDialog::getExistingDirectory(
            this, QStringLiteral("选择输出根目录 (add_lut/0N.png 将写入各层子目录)"),
            startDir);
        if (outRoot.isEmpty()) return false;     // 用户取消

        AppSettings::instance().setLastOutputDir(outRoot);
        proj.outputRoot = outRoot;
        return true;
    };

    {
        auto* actCur = menuExport->addAction(QStringLiteral("导出当前方案 add_lut PNG..."));
        actCur->setShortcut(QKeySequence("Ctrl+E"));
        connect(actCur, &QAction::triggered, this, [this, prepareExport]{
            QString outRoot;
            if (!prepareExport(outRoot)) return;

            static LutBaker baker;
            QString err;
            if (!baker.init(&err)) {
                QMessageBox::critical(this, QStringLiteral("导出失败"),
                    QStringLiteral("LutBaker 初始化失败: %1").arg(err));
                return;
            }
            auto r = PngExporter::exportCurrentScheme(
                ProjectController::instance().project(), baker);
            if (!r.lastError.isEmpty()) {
                QMessageBox::warning(this, QStringLiteral("导出失败"), r.lastError);
                statusBar()->showMessage(r.lastError, 8000);
                return;
            }
            statusBar()->showMessage(
                QStringLiteral("导出完成: 写出 %1 个 LUT, 跳过 %2 个; 输出目录 %3")
                    .arg(r.successCount).arg(r.skippedCount).arg(outRoot), 8000);
        });
    }

    {
        auto* actAll = menuExport->addAction(QStringLiteral("导出全部方案 add_lut PNG..."));
        actAll->setShortcut(QKeySequence("Ctrl+Shift+E"));
        connect(actAll, &QAction::triggered, this, [this, prepareExport]{
            QString outRoot;
            if (!prepareExport(outRoot)) return;

            const auto& proj = ProjectController::instance().project();
            const int n = proj.schemes.size() - 1;   // 不含本体
            if (n <= 0) {
                QMessageBox::information(this, QStringLiteral("导出"),
                    QStringLiteral("当前没有可导出的方案 (只有本体方案)"));
                return;
            }
            auto rc = QMessageBox::question(this, QStringLiteral("导出全部方案"),
                QStringLiteral("将导出 %1 个方案到:\n%2\n\n继续?").arg(n).arg(outRoot),
                QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Ok);
            if (rc != QMessageBox::Ok) return;

            static LutBaker baker;
            QString err;
            if (!baker.init(&err)) {
                QMessageBox::critical(this, QStringLiteral("导出失败"),
                    QStringLiteral("LutBaker 初始化失败: %1").arg(err));
                return;
            }
            auto r = PngExporter::exportAllSchemes(proj, baker);
            if (!r.lastError.isEmpty()) {
                QMessageBox::warning(this, QStringLiteral("导出失败"), r.lastError);
                statusBar()->showMessage(r.lastError, 8000);
                return;
            }
            statusBar()->showMessage(
                QStringLiteral("全部方案导出完成: 写出 %1 个 LUT, 跳过 %2 个; 输出目录 %3")
                    .arg(r.successCount).arg(r.skippedCount).arg(outRoot), 8000);
        });
    }

    menuView->addAction(m_dockLayer->toggleViewAction());
    menuView->addAction(m_dockEffect->toggleViewAction());
    menuView->addAction(m_dockScheme->toggleViewAction());

    menuView->addSeparator();
    {
        auto* actFit = menuView->addAction(QStringLiteral("适合窗口"));
        actFit->setShortcut(QKeySequence("Ctrl+0"));
        connect(actFit, &QAction::triggered, this, [this]{
            if (m_preview && m_preview->canvas()) {
                // 用 RTV 内容大小 (cellW × cols) 适配 — 简化: 复位 + 让 PreviewPanel 自己 cells 居中
                m_preview->canvas()->resetView();
            }
        });
        auto* actActual = menuView->addAction(QStringLiteral("100%"));
        actActual->setShortcut(QKeySequence("Ctrl+Alt+0"));
        connect(actActual, &QAction::triggered, this, [this]{
            if (m_preview && m_preview->canvas()) m_preview->canvas()->setContentZoom(1.0);
        });

        // 全屏画布: 由 PreviewPanel 提供 toolbar 按钮 + Ctrl+Space 快捷键.
        // 这里只保留菜单入口 (无快捷键, 避免与 PreviewPanel 冲突), 点击转发到按钮.
        auto* actFullCanvas = menuView->addAction(QStringLiteral("全屏画布..."));
        connect(actFullCanvas, &QAction::triggered, this, [this]{
            // 通过对象名找到 PreviewPanel 内 fullCanvas 按钮触发
            if (auto* btn = this->findChild<QPushButton*>("fullCanvasBtn")) {
                btn->toggle();
            }
        });
    }

    // 调试 (M3 排查偏色用, M5 后会移到隐藏菜单)
    auto* menuDebug = menuBar()->addMenu(QStringLiteral("调试(&D)"));
    auto* actDumpFrame = menuDebug->addAction(QStringLiteral("抓当前帧 + reference 对比"));
    actDumpFrame->setShortcut(QKeySequence("F12"));
    connect(actDumpFrame, &QAction::triggered, this, [this]{
        const QString deskDir = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
        const QString ourPng  = QDir(deskDir).filePath("our.png");
        const QString refPng  = QDir(deskDir).filePath("reference.png");

        auto* canvas = m_preview ? m_preview->canvas() : nullptr;
        if (!canvas || !canvas->currentRtv()) {
            QMessageBox::warning(this, "调试", "RTV 未就绪");
            return;
        }
        QString err;
        if (!DebugDumper::dumpRtv(canvas->currentRtv(),
                                  canvas->rtvWidth(),
                                  canvas->rtvHeight(),
                                  ourPng, &err)) {
            QMessageBox::warning(this, "调试", "抓帧失败: " + err);
            return;
        }

        // reference: 拿 body/<action>/<dir><frame>.tga + 当前 body 层 LUT
        const auto& proj = ProjectController::instance().project();
        QString framePath, lutPath;
        for (const auto& l : proj.layers) {
            if (l.kind != LayerKind::Body) continue;
            if (auto* a = l.action(proj.currentAction)) {
                framePath = a->framePath(proj.currentDirection, proj.currentFrame);
            }
            lutPath = proj.lutPathFor(l);
            break;
        }
        if (framePath.isEmpty() || lutPath.isEmpty()) {
            statusBar()->showMessage(
                QString("仅写出 our.png ; body 帧或 LUT 不存在 (frame=%1 lut=%2)")
                    .arg(framePath).arg(lutPath), 8000);
            return;
        }
        if (!DebugDumper::referenceCpuLut(framePath, lutPath, refPng, &err)) {
            QMessageBox::warning(this, "调试", "reference 失败: " + err);
            return;
        }
        statusBar()->showMessage(
            QString("已写出: %1 + %2").arg(ourPng, refPng), 8000);
    });

    m_actAbout = menuHelp->addAction(QStringLiteral("关于"));
    connect(m_actAbout, &QAction::triggered, this, &MainWindow::onAbout);
}

void MainWindow::restoreGeometry()
{
    auto& s = AppSettings::instance();
    resize(s.windowSize());
    move(s.windowPos());
    if (s.windowMaximized()) showMaximized();
    // 当布局版本变更 (新增 dock / 改默认排列) 时, restoreState 用 versionId 区分
    constexpr int kDockLayoutVersion = 2;   // M5 批 1 = 1; 画廊与颜色效果并排 = 2
    if (!s.dockState().isEmpty()) {
        // restoreState(state, version): version 不匹配返回 false, 沿用 buildDocks 的默认布局
        restoreState(s.dockState(), kDockLayoutVersion);
    }
}

void MainWindow::persistGeometry()
{
    auto& s = AppSettings::instance();
    s.setWindowMaximized(isMaximized());
    if (!isMaximized()) {
        s.setWindowSize(size());
        s.setWindowPos(pos());
    }
    constexpr int kDockLayoutVersion = 2;
    s.setDockState(saveState(kDockLayoutVersion));
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    persistGeometry();
    QMainWindow::closeEvent(e);
}

void MainWindow::onOpenSource()
{
    auto& s = AppSettings::instance();
    QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("选择资源根目录"),
        s.lastSourceDir().isEmpty() ? QString() : s.lastSourceDir());
    if (dir.isEmpty()) return;
    s.setLastSourceDir(dir);

    QString err;
    if (!ProjectController::instance().loadSource(dir, &err)) {
        QMessageBox::warning(this, QStringLiteral("加载失败"), err);
        statusBar()->showMessage(err, 5000);
        return;
    }
    const auto& proj = ProjectController::instance().project();
    statusBar()->showMessage(
        QStringLiteral("已加载: %1  共 %2 层")
            .arg(dir).arg(proj.layers.size()), 5000);
}

void MainWindow::onOpenOutput()
{
    auto& s = AppSettings::instance();
    QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("选择输出目录"),
        s.lastOutputDir().isEmpty() ? QString() : s.lastOutputDir());
    if (dir.isEmpty()) return;
    s.setLastOutputDir(dir);
    statusBar()->showMessage(QStringLiteral("输出目录: %1").arg(dir));
}

void MainWindow::onAbout()
{
    QMessageBox::about(this,
        QStringLiteral("关于 HighPro_LUT"),
        QStringLiteral(
            "<b>HighPro_LUT</b> v%1<br>"
            "高性能序列帧角色换色工具<br><br>"
            "C++17 / Qt %2 / Direct3D 11<br>"
            "(c) 2026 HighPro"
        ).arg(QApplication::applicationVersion(), QT_VERSION_STR));
}

// === M8 工程持久化槽 ===

void MainWindow::onProjectNew()
{
    auto& ctl = ProjectController::instance();
    if (ctl.isDirty()) {
        auto rc = QMessageBox::question(this, QStringLiteral("新建工程"),
            QStringLiteral("当前有未保存的修改, 新建会丢弃. 继续?"),
            QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel);
        if (rc != QMessageBox::Ok) return;
    }
    ctl.projectMut().clear();
    ctl.setDirty(false);
    AppSettings::instance().setLastProjectPath({});
    setWindowTitle(QStringLiteral("HighPro_LUT - 高性能序列帧角色换色工具 - [新工程]"));
    statusBar()->showMessage(QStringLiteral("新工程"), 3000);
    emit ctl.projectLoaded();    // 触发各 panel 重建
}

void MainWindow::onProjectOpen()
{
    QString last = AppSettings::instance().lastProjectPath();
    QString dir = last.isEmpty() ? AppSettings::instance().lastSourceDir() : QFileInfo(last).absolutePath();
    QString path = QFileDialog::getOpenFileName(this, QStringLiteral("打开工程"),
        dir, QStringLiteral("HighPro 工程 (*.hplut.json *.json)"));
    if (path.isEmpty()) return;

    openProjectFromPath(path);
}

bool MainWindow::openProjectFromPath(const QString& path)
{
    if (path.isEmpty()) return false;
    if (!QFileInfo::exists(path)) {
        QMessageBox::warning(this, QStringLiteral("打开工程失败"),
            QStringLiteral("文件不存在: %1").arg(path));
        // 不存在的文件从最近列表清掉
        auto& set = AppSettings::instance();
        auto list = set.recentProjects();
        list.removeAll(path);
        // 重新写回 (没有公开 setter, 用 push 不合适 — 直接借 clear+逐项加回)
        set.clearRecentProjects();
        for (const auto& p : list) set.pushRecentProject(p);
        rebuildRecentProjectsMenu();
        return false;
    }

    QString err;
    QJsonObject ui;
    if (!ProjectController::instance().loadProject(path, &ui, &err)) {
        QMessageBox::warning(this, QStringLiteral("打开工程失败"), err);
        return false;
    }
    auto& set = AppSettings::instance();
    set.setLastProjectPath(path);
    set.setLastSourceDir(ProjectController::instance().project().sourceRoot);
    set.pushRecentProject(path);
    rebuildRecentProjectsMenu();
    setWindowTitle(QStringLiteral("HighPro_LUT - %1").arg(QFileInfo(path).fileName()));
    statusBar()->showMessage(QStringLiteral("已打开工程: %1").arg(path), 4000);
    return true;
}

void MainWindow::rebuildRecentProjectsMenu()
{
    if (!m_menuRecent) return;
    m_menuRecent->clear();

    const auto list = AppSettings::instance().recentProjects();
    if (list.isEmpty()) {
        auto* a = m_menuRecent->addAction(QStringLiteral("(无)"));
        a->setEnabled(false);
        return;
    }
    int i = 1;
    for (const QString& p : list) {
        // 显示: "1  filename.hplut.json    (完整路径作为 tooltip)"
        QString label = QString::number(i++) + "  " + QFileInfo(p).fileName();
        auto* a = m_menuRecent->addAction(label);
        a->setToolTip(p);
        connect(a, &QAction::triggered, this, [this, p]{ openProjectFromPath(p); });
        if (i - 1 >= AppSettings::kMaxRecentProjects) break;
    }
    m_menuRecent->addSeparator();
    auto* actClear = m_menuRecent->addAction(QStringLiteral("清空最近工程"));
    connect(actClear, &QAction::triggered, this, [this]{
        AppSettings::instance().clearRecentProjects();
        rebuildRecentProjectsMenu();
    });
}

void MainWindow::onProjectSave()
{
    auto& ctl = ProjectController::instance();
    if (ctl.project().sourceRoot.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("保存工程"),
            QStringLiteral("还没加载源目录, 无法保存"));
        return;
    }
    QString path = ctl.currentProjectPath();
    if (path.isEmpty()) {
        onProjectSaveAs();
        return;
    }
    QString err;
    if (!ctl.saveProject(path, {}, &err)) {
        QMessageBox::warning(this, QStringLiteral("保存工程失败"), err);
        return;
    }
    auto& set = AppSettings::instance();
    set.setLastProjectPath(path);
    set.pushRecentProject(path);
    rebuildRecentProjectsMenu();
    statusBar()->showMessage(QStringLiteral("已保存工程: %1").arg(path), 3000);
}

void MainWindow::onProjectSaveAs()
{
    auto& ctl = ProjectController::instance();
    if (ctl.project().sourceRoot.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("工程另存为"),
            QStringLiteral("还没加载源目录, 无法保存"));
        return;
    }
    QString last = AppSettings::instance().lastProjectPath();
    QString dir = last.isEmpty()
        ? AppSettings::instance().lastSourceDir()
        : QFileInfo(last).absolutePath();
    QString defName = QFileInfo(ctl.project().sourceRoot).fileName() + ".hplut.json";
    QString path = QFileDialog::getSaveFileName(this, QStringLiteral("工程另存为"),
        QDir(dir).filePath(defName),
        QStringLiteral("HighPro 工程 (*.hplut.json *.json)"));
    if (path.isEmpty()) return;
    if (!path.endsWith(".json", Qt::CaseInsensitive)) path += ".hplut.json";

    QString err;
    if (!ctl.saveProject(path, {}, &err)) {
        QMessageBox::warning(this, QStringLiteral("保存失败"), err);
        return;
    }
    auto& set = AppSettings::instance();
    set.setLastProjectPath(path);
    set.pushRecentProject(path);
    rebuildRecentProjectsMenu();
    setWindowTitle(QStringLiteral("HighPro_LUT - %1").arg(QFileInfo(path).fileName()));
    statusBar()->showMessage(QStringLiteral("已保存工程: %1").arg(path), 3000);
}

} // namespace HighPro
