#include "PreviewPanel.h"
#include "D3DWidget.h"
#include "render/FrameRenderer.h"
#include "render/FrameLoader.h"
#include "render/GifExporter.h"
#include "core/KPoint.h"
#include "app/ProjectController.h"
#include "app/AppSettings.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QButtonGroup>
#include <QToolButton>
#include <QCheckBox>
#include <QColorDialog>
#include <QMainWindow>
#include <QStatusBar>
#include <QMessageBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QSignalBlocker>
#include <QShortcut>
#include <QKeySequence>
#include <QApplication>
#include <QScrollArea>
#include <QFrame>
#include <QSizePolicy>
#include <QDebug>
#include <cmath>

namespace HighPro {

PreviewPanel::PreviewPanel(QWidget* parent) : QWidget(parent)
{
    m_renderer = std::make_unique<FrameRenderer>();

    // 让面板自身接受焦点, 点击空白区域时 SpinBox/LineEdit 自动失焦
    setFocusPolicy(Qt::ClickFocus);

    buildUi();
    connectSignals();

    // 恢复上次保存的背景图
    m_bgImagePath = AppSettings::instance().bgImage();
    if (!m_bgImagePath.isEmpty()) {
        m_bgImageTex = FrameLoader::instance().get(m_bgImagePath);
    }

    // 播放定时器
    m_playTimer.setInterval(1000 / qMax(1, AppSettings::instance().fps()));
    connect(&m_playTimer, &QTimer::timeout, this, &PreviewPanel::onTick);
    m_playTimer.start();

    // M5: 闪烁定时器 (60ms 一步, 总周期约 1.2s)
    m_blinkTimer.setInterval(60);
    connect(&m_blinkTimer, &QTimer::timeout, this, [this]{
        m_blinkPhaseMs = (m_blinkPhaseMs + m_blinkTimer.interval()) % 1200;
        if (m_canvas) m_canvas->requestRender();
    });
    m_blinkTimer.start();

    // 空格: 播放/暂停.
    // 用 ApplicationShortcut 全局生效, 但只在用户没在编辑控件 (QLineEdit/QSpinBox 等) 时触发.
    // 简化: 直接 ApplicationShortcut + 检查焦点是否在文本输入控件上 → 是则忽略
    auto* sc = new QShortcut(QKeySequence(Qt::Key_Space), this);
    sc->setContext(Qt::ApplicationShortcut);
    connect(sc, &QShortcut::activated, this, [this]{
        QWidget* fw = QApplication::focusWidget();
        if (fw) {
            const QString cls = QString::fromUtf8(fw->metaObject()->className());
            // 编辑控件 / SpinBox 行内时, 让空格走原生
            if (cls.contains("LineEdit") || cls.contains("SpinBox") ||
                cls.contains("ComboBox") || cls.contains("TextEdit")) {
                return;
            }
        }
        onPlayPause();
    });
}

PreviewPanel::~PreviewPanel() = default;

void PreviewPanel::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(2, 2, 2, 2);
    root->setSpacing(4);

    // 1) 画布
    m_canvas = new D3DWidget(this);
    m_canvas->setFocusPolicy(Qt::ClickFocus);
    m_canvas->setClearColor(AppSettings::instance().bgColor());
    m_canvas->setRenderCallback([this](ID3D11RenderTargetView* rtv, int w, int h) {
        render(rtv, w, h);
    });
    root->addWidget(m_canvas, 1);

    // 2) 控制条 (两行) — 包在 QScrollArea 里, 1K 屏幕不被挤扁
    auto* line1Container = new QWidget(this);
    line1Container->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* line1 = new QHBoxLayout(line1Container);
    line1->setContentsMargins(0, 2, 0, 2);
    line1->setSizeConstraint(QLayout::SetMinimumSize);
    line1->setSpacing(6);

    line1->addWidget(new QLabel(QStringLiteral("动作:"), this));
    m_actionCombo = new QComboBox(this);
    m_actionCombo->setMinimumWidth(120);
    m_actionCombo->setEnabled(false);
    line1->addWidget(m_actionCombo);

    line1->addSpacing(12);
    line1->addWidget(new QLabel(QStringLiteral("方向:"), this));
    m_dirRow = new QWidget(this);
    auto* drl = new QHBoxLayout(m_dirRow);
    drl->setContentsMargins(0,0,0,0);
    drl->setSpacing(2);
    m_dirGroup = new QButtonGroup(this);
    for (int i = 0; i < 8; ++i) {
        auto* b = new QToolButton(m_dirRow);
        b->setText(QString::number(i + 1));
        b->setCheckable(true);
        b->setFixedSize(28, 24);
        b->setEnabled(false);
        m_dirGroup->addButton(b, i);
        drl->addWidget(b);
    }
    line1->addWidget(m_dirRow);

    line1->addSpacing(12);
    line1->addWidget(new QLabel(QStringLiteral("帧率:"), this));
    m_fpsSpin = new QSpinBox(this);
    m_fpsSpin->setRange(1, 60);
    m_fpsSpin->setValue(AppSettings::instance().fps());
    m_fpsSpin->setSuffix(" fps");
    m_fpsSpin->setFixedWidth(80);
    line1->addWidget(m_fpsSpin);

    m_playBtn = new QPushButton(QStringLiteral("⏯"), this);
    m_playBtn->setFixedWidth(32);
    line1->addWidget(m_playBtn);

    m_frameLabel = new QLabel(QStringLiteral("当前: 0/0"), this);
    m_frameLabel->setMinimumWidth(110);
    line1->addWidget(m_frameLabel);

    // 画布显示百分比 (AE 风格, 帧率/帧号后)
    line1->addSpacing(12);
    line1->addWidget(new QLabel(QStringLiteral("画布显示:"), this));
    m_zoomLabel = new QLabel(QStringLiteral("100%"), this);
    m_zoomLabel->setAlignment(Qt::AlignCenter);
    m_zoomLabel->setMinimumWidth(56);
    m_zoomLabel->setStyleSheet(
        "QLabel{color:#ddd; padding:1px 8px;"
        " border:1px solid #555; border-radius:3px; background:#2b2b2b;}");
    line1->addWidget(m_zoomLabel);

    // 角色间距 X 轴 (可负, 让角色互相重叠靠近)
    line1->addSpacing(12);
    line1->addWidget(new QLabel(QStringLiteral("X 间距:"), this));
    m_gapXSpin = new QSpinBox(this);
    m_gapXSpin->setRange(-500, 500);
    m_gapXSpin->setSingleStep(10);
    m_gapXSpin->setValue(m_charGapXPx);
    m_gapXSpin->setSuffix(" px");
    m_gapXSpin->setFixedWidth(95);
    line1->addWidget(m_gapXSpin);

    // 角色间距 Y 轴
    line1->addWidget(new QLabel(QStringLiteral("Y 间距:"), this));
    m_gapYSpin = new QSpinBox(this);
    m_gapYSpin->setRange(-500, 500);
    m_gapYSpin->setSingleStep(10);
    m_gapYSpin->setValue(m_charGapYPx);
    m_gapYSpin->setSuffix(" px");
    m_gapYSpin->setFixedWidth(95);
    line1->addWidget(m_gapYSpin);

    line1->addSpacing(12);
    m_showLabelChk = new QCheckBox(QStringLiteral("方案 ID"), this);
    m_showLabelChk->setToolTip(QStringLiteral("画布上每个方案 cell 顶部显示 ID (本体不显示, 锁住的方案 ID 后追加 -锁)"));
    m_showLabelChk->setChecked(m_showLabel);
    line1->addWidget(m_showLabelChk);

    // 方案 ID 在 cell 内的 Y 位置 (px, 距 cell 顶部). 0 = 紧贴 cell 顶.
    line1->addWidget(new QLabel(QStringLiteral("ID Y:"), this));
    m_labelGapYSpin = new QSpinBox(this);
    m_labelGapYSpin->setRange(0, 800);
    m_labelGapYSpin->setSingleStep(10);
    m_labelGapYSpin->setValue(m_labelGapY);
    m_labelGapYSpin->setSuffix(" px");
    m_labelGapYSpin->setFixedWidth(90);
    m_labelGapYSpin->setToolTip(QStringLiteral("方案 ID 距 cell 顶部的 Y 偏移 (px). 跟随 zoom 缩放"));
    line1->addWidget(m_labelGapYSpin);

    // 全屏画布开关 (= Ctrl+Space).
    line1->addSpacing(12);
    m_fullCanvasBtn = new QPushButton(QStringLiteral("全屏画布"), this);
    m_fullCanvasBtn->setObjectName("fullCanvasBtn");    // 给 MainWindow 菜单转发用
    m_fullCanvasBtn->setCheckable(true);
    // 用 QShortcut + ApplicationShortcut 而非 QPushButton::setShortcut, 确保 fullscreen
    // 状态下 / 焦点不在按钮时仍能触发 (QPushButton::setShortcut 是 WindowShortcut, 焦点变化会失效).
    {
        auto* sc = new QShortcut(QKeySequence("Ctrl+Space"), this);
        sc->setContext(Qt::ApplicationShortcut);
        connect(sc, &QShortcut::activated, this, [this]{
            if (m_fullCanvasBtn) m_fullCanvasBtn->toggle();
        });
    }
    m_fullCanvasBtn->setToolTip(QStringLiteral("切换全屏预览画布 (Ctrl+Space). 退出后 dock 自动恢复"));
    line1->addWidget(m_fullCanvasBtn);

    // 显示 / 关闭 画布里未上锁的方案 (本体始终保留, 锁定方案始终保留)
    m_hideUnlockedBtn = new QPushButton(QStringLiteral("🔒 只看锁定"), this);
    m_hideUnlockedBtn->setCheckable(true);
    m_hideUnlockedBtn->setChecked(m_hideUnlocked);
    m_hideUnlockedBtn->setToolTip(QStringLiteral(
        "勾选: 画布只渲染锁定 🔒 的方案 (本体始终显示).\n"
        "便于在大批量随机后聚焦看挑选出的精品配色."));
    line1->addWidget(m_hideUnlockedBtn);

    line1->addStretch(1);

    // 用 QScrollArea 包裹 line1Container, 低分辨率屏幕可水平滚动
    auto* line1Scroll = new QScrollArea(this);
    line1Scroll->setWidget(line1Container);
    line1Scroll->setWidgetResizable(false);
    line1Scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    line1Scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    line1Scroll->setFrameShape(QFrame::NoFrame);
    line1Scroll->setFixedHeight(line1Container->sizeHint().height() + 4);
    line1Scroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    root->addWidget(line1Scroll);

    // 第二行: 背景
    auto* line2 = new QHBoxLayout();
    line2->setSpacing(6);
    line2->addWidget(new QLabel(QStringLiteral("背景:"), this));
    m_bgColorBtn = new QPushButton(this);
    m_bgColorBtn->setFixedWidth(120);
    {
        const QColor c = AppSettings::instance().bgColor();
        m_bgColorBtn->setText(c.name(QColor::HexRgb).toUpper());
        m_bgColorBtn->setStyleSheet(QString("background:%1; color:#fff").arg(c.name()));
    }
    line2->addWidget(m_bgColorBtn);

    m_bgImageBtn = new QPushButton(QStringLiteral("背景图..."), this);
    line2->addWidget(m_bgImageBtn);

    m_bgClearBtn = new QPushButton(QStringLiteral("清除"), this);
    line2->addWidget(m_bgClearBtn);

    line2->addStretch(1);
    root->addLayout(line2);

    // 第三行: 输出 GIF
    auto* line3 = new QHBoxLayout();
    line3->setSpacing(6);
    line3->addWidget(new QLabel(QStringLiteral("输出 GIF:"), this));

    line3->addWidget(new QLabel(QStringLiteral("循环次数:"), this));
    m_gifLoopSpin = new QSpinBox(this);
    m_gifLoopSpin->setRange(0, 9999);
    m_gifLoopSpin->setValue(m_gifLoop);
    m_gifLoopSpin->setSuffix(QStringLiteral(" 次"));
    m_gifLoopSpin->setSpecialValueText(QStringLiteral("无限"));
    m_gifLoopSpin->setFixedWidth(95);
    m_gifLoopSpin->setToolTip(QStringLiteral("GIF 播放循环次数: 0=无限循环, 1=播 1 次, 其他=播 N 次"));
    line3->addWidget(m_gifLoopSpin);

    line3->addSpacing(12);
    m_gifIdChk = new QCheckBox(QStringLiteral("GIF ID"), this);
    m_gifIdChk->setChecked(m_gifShowId);
    m_gifIdChk->setToolTip(QStringLiteral("勾选: GIF 帧含方案 ID label (与画布预览一致)"));
    line3->addWidget(m_gifIdChk);

    line3->addSpacing(12);
    m_gifExportBtn = new QPushButton(QStringLiteral("输出 GIF 图..."), this);
    m_gifExportBtn->setToolTip(QStringLiteral("把当前动作 (全帧) × 所有方案 渲染成 GIF (含背景)"));
    line3->addWidget(m_gifExportBtn);

    line3->addStretch(1);
    root->addLayout(line3);

    // === 全屏模式下 画布下方 智能/随机/重置 按钮区 (默认隐藏, 全屏时显示) ===
    // 布局: 3 行 (智能 / 随机 / 重置), 与截图三对齐
    m_fullscreenBtnBar = new QWidget(this);
    m_fullscreenBtnBar->setObjectName("FullscreenBtnBar");
    auto* fsVBox = new QVBoxLayout(m_fullscreenBtnBar);
    fsVBox->setContentsMargins(8, 6, 8, 6);
    fsVBox->setSpacing(4);

    // 通用按钮样式
    const QString kSmartStyle = QStringLiteral(
        "QPushButton { border: 1px solid #4a7fbf; background: #283848; color: #ddd;"
        " padding: 4px 12px; border-radius: 3px; font-size: 12px; }"
        "QPushButton:hover { background: #344a60; }");
    const QString kMixStyle = QStringLiteral(
        "QPushButton { border: 1px solid #9060b8; background: #382848; color: #ddd;"
        " padding: 4px 12px; border-radius: 3px; font-size: 12px; }"
        "QPushButton:hover { background: #4a3860; }");
    const QString kRandStyle = QStringLiteral(
        "QPushButton { border: 1px solid #555; background: #2e2e2e; color: #ccc;"
        " padding: 4px 12px; border-radius: 3px; font-size: 12px; }"
        "QPushButton:hover { background: #3a3a3a; }");
    const QString kResetStyle = QStringLiteral(
        "QPushButton { border: 1px solid #555; background: #2e2e2e; color: #bbb;"
        " padding: 4px 12px; border-radius: 3px; font-size: 12px; }"
        "QPushButton:hover { background: #3a3a3a; }");

    // ── 行 1: 智能 ──
    auto* row1 = new QHBoxLayout();
    row1->setSpacing(8);
    auto* fsSmartAll = new QPushButton(QStringLiteral("\xF0\x9F\x8E\xA8 智能所有层"), m_fullscreenBtnBar);
    auto* fsSmartEditable = new QPushButton(QStringLiteral("\xF0\x9F\x8E\xA8 智能可编辑"), m_fullscreenBtnBar);
    auto* fsSmartEvery = new QPushButton(QStringLiteral("\xF0\x9F\x8E\xA8 智能全部"), m_fullscreenBtnBar);
    auto* fsMixEvery = new QPushButton(QStringLiteral("\xF0\x9F\x8E\xA8\xF0\x9F\x8E\xB2 智能+随机"), m_fullscreenBtnBar);
    for (auto* b : { fsSmartAll, fsSmartEditable, fsSmartEvery }) b->setStyleSheet(kSmartStyle);
    fsMixEvery->setStyleSheet(kMixStyle);
    for (auto* b : { fsSmartAll, fsSmartEditable, fsSmartEvery, fsMixEvery }) {
        b->setFixedHeight(30);
        row1->addWidget(b);
    }
    row1->addStretch(1);
    fsVBox->addLayout(row1);

    // ── 行 2: 随机 ──
    auto* row2 = new QHBoxLayout();
    row2->setSpacing(8);
    auto* fsRandAll = new QPushButton(QStringLiteral("\xF0\x9F\x8E\xB2 随机所有层"), m_fullscreenBtnBar);
    auto* fsRandEditable = new QPushButton(QStringLiteral("\xF0\x9F\x8E\xB2 随机可编辑"), m_fullscreenBtnBar);
    auto* fsRandEvery = new QPushButton(QStringLiteral("\xF0\x9F\x8E\xB2 随机全部"), m_fullscreenBtnBar);
    for (auto* b : { fsRandAll, fsRandEditable, fsRandEvery }) {
        b->setStyleSheet(kRandStyle);
        b->setFixedHeight(30);
        row2->addWidget(b);
    }
    row2->addStretch(1);
    fsVBox->addLayout(row2);

    // ── 行 3: 重置 ──
    auto* row3 = new QHBoxLayout();
    row3->setSpacing(8);
    auto* fsResetAll = new QPushButton(QStringLiteral("↺ 重置所有层"), m_fullscreenBtnBar);
    auto* fsResetEditable = new QPushButton(QStringLiteral("↺ 重置可编辑"), m_fullscreenBtnBar);
    auto* fsResetEvery = new QPushButton(QStringLiteral("↺ 重置全部"), m_fullscreenBtnBar);
    for (auto* b : { fsResetAll, fsResetEditable, fsResetEvery }) {
        b->setStyleSheet(kResetStyle);
        b->setFixedHeight(30);
        row3->addWidget(b);
    }
    row3->addStretch(1);
    fsVBox->addLayout(row3);

    // 连接信号 → ProjectController
    connect(fsSmartAll, &QPushButton::clicked, this, []{
        ProjectController::instance().smartRandomizeAllLayers(); });
    connect(fsSmartEditable, &QPushButton::clicked, this, []{
        ProjectController::instance().smartRandomizeAllSchemes(false); });
    connect(fsSmartEvery, &QPushButton::clicked, this, []{
        ProjectController::instance().smartRandomizeAllSchemes(true); });
    connect(fsMixEvery, &QPushButton::clicked, this, []{
        ProjectController::instance().mixRandomizeAllSchemes(true); });
    connect(fsRandAll, &QPushButton::clicked, this, []{
        ProjectController::instance().randomizeAllLayers(); });
    connect(fsRandEditable, &QPushButton::clicked, this, []{
        ProjectController::instance().randomizeAllSchemes(false); });
    connect(fsRandEvery, &QPushButton::clicked, this, []{
        ProjectController::instance().randomizeAllSchemes(true); });
    connect(fsResetAll, &QPushButton::clicked, this, []{
        ProjectController::instance().resetAllLayerEffects(); });
    connect(fsResetEditable, &QPushButton::clicked, this, []{
        ProjectController::instance().resetAllSchemesEffects(false); });
    connect(fsResetEvery, &QPushButton::clicked, this, []{
        ProjectController::instance().resetAllSchemesEffects(true); });

    m_fullscreenBtnBar->setVisible(false);  // 默认隐藏, 全屏时显示
    root->addWidget(m_fullscreenBtnBar);

    // toolbar 底部留白 50px (避让用户视线 / 美观)
    auto* toolbarPad = new QWidget(this);
    toolbarPad->setFixedHeight(50);
    root->addWidget(toolbarPad);
}

void PreviewPanel::connectSignals()
{
    auto& ctl = ProjectController::instance();
    connect(&ctl, &ProjectController::projectLoaded,
            this, &PreviewPanel::onProjectLoaded);
    connect(&ctl, &ProjectController::actionChanged,
            this, &PreviewPanel::onActionChanged);
    connect(&ctl, &ProjectController::directionChanged,
            this, &PreviewPanel::onDirectionChanged);
    connect(&ctl, &ProjectController::frameChanged,
            this, &PreviewPanel::onFrameChanged);
    connect(&ctl, &ProjectController::visibilityChanged,
            this, [this]{ if (m_canvas) m_canvas->requestRender(); });
    connect(&ctl, &ProjectController::lutChanged,
            this, [this]{ if (m_canvas) m_canvas->requestRender(); });
    connect(&ctl, &ProjectController::effectsChanged,
            this, [this]{ if (m_canvas) m_canvas->requestRender(); });
    connect(&ctl, &ProjectController::schemesChanged,
            this, [this]{ if (m_canvas) m_canvas->requestRender(); });
    connect(&ctl, &ProjectController::currentSchemeChanged,
            this, [this]{ if (m_canvas) m_canvas->requestRender(); });

    if (m_canvas) {
        connect(m_canvas, &D3DWidget::viewTransformChanged,
                this, &PreviewPanel::updateZoomLabel);
    }

    if (m_gapXSpin) {
        connect(m_gapXSpin, qOverload<int>(&QSpinBox::valueChanged),
                this, [this](int v){
            m_charGapXPx = v;
            if (m_canvas) m_canvas->requestRender();
        });
    }
    if (m_gapYSpin) {
        connect(m_gapYSpin, qOverload<int>(&QSpinBox::valueChanged),
                this, [this](int v){
            m_charGapYPx = v;
            if (m_canvas) m_canvas->requestRender();
        });
    }
    if (m_showLabelChk) {
        connect(m_showLabelChk, &QCheckBox::toggled, this, [this](bool on){
            m_showLabel = on;
            if (m_canvas) m_canvas->requestRender();
        });
    }
    if (m_labelGapYSpin) {
        connect(m_labelGapYSpin, qOverload<int>(&QSpinBox::valueChanged),
                this, [this](int v){
            m_labelGapY = v;
            if (m_canvas) m_canvas->requestRender();
        });
    }
    if (m_fullCanvasBtn) {
        connect(m_fullCanvasBtn, &QPushButton::toggled, this, [this](bool on){
            QMainWindow* mw = qobject_cast<QMainWindow*>(this->window());
            if (!mw) return;
            // 简化路径: 仅 hide/show dock + 切 windowState. 不序列化 saveState
            // (那会把整个 layout dump 到 byte array, 大窗口下会卡 100~300ms).
            // Qt 内部本来就保留 dock 的 splitter/位置/浮停状态, hide 不影响.
            static const char* kPrevWinState = "_fc_prev_win_state";
            static const char* kPrevDockVis  = "_fc_prev_dock_vis";

            const auto allDocks = mw->findChildren<QDockWidget*>();

            if (on) {
                mw->setProperty(kPrevWinState, (int)mw->windowState());
                QStringList vis;
                for (auto* d : allDocks) {
                    vis << (d->isVisible() ? d->objectName() : QString());
                }
                mw->setProperty(kPrevDockVis, vis);
                // 关闭 paint 通告减少中间抖动
                mw->setUpdatesEnabled(false);
                for (auto* d : allDocks) d->hide();
                mw->showFullScreen();
                mw->setUpdatesEnabled(true);
                // 全屏时显示 智能/随机/重置 按钮行
                if (m_fullscreenBtnBar) m_fullscreenBtnBar->setVisible(true);
            } else {
                Qt::WindowStates ws = (Qt::WindowStates)mw->property(kPrevWinState).toInt();
                ws &= ~Qt::WindowFullScreen;
                mw->setUpdatesEnabled(false);
                if (ws == Qt::WindowNoState) mw->showNormal();
                else                          mw->setWindowState(ws);
                const auto vis = mw->property(kPrevDockVis).toStringList();
                if (vis.size() == allDocks.size()) {
                    for (int i = 0; i < allDocks.size(); ++i) {
                        if (!vis[i].isEmpty()) allDocks[i]->show();
                    }
                }
                mw->setUpdatesEnabled(true);
                // 退出全屏时隐藏
                if (m_fullscreenBtnBar) m_fullscreenBtnBar->setVisible(false);
            }
        });
    }
    if (m_hideUnlockedBtn) {
        connect(m_hideUnlockedBtn, &QPushButton::toggled, this, [this](bool on){
            m_hideUnlocked = on;
            if (m_canvas) m_canvas->requestRender();
        });
    }

    if (m_gifLoopSpin) {
        connect(m_gifLoopSpin, qOverload<int>(&QSpinBox::valueChanged),
                this, [this](int v){ m_gifLoop = v; });
    }
    if (m_gifIdChk) {
        connect(m_gifIdChk, &QCheckBox::toggled,
                this, [this](bool on){ m_gifShowId = on; });
    }
    if (m_gifExportBtn) {
        connect(m_gifExportBtn, &QPushButton::clicked, this, &PreviewPanel::onExportGif);
    }

    connect(m_actionCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &PreviewPanel::onActionCombo);
    connect(m_dirGroup, &QButtonGroup::idClicked,
            this, &PreviewPanel::onDirButton);
    connect(m_playBtn, &QPushButton::clicked, this, &PreviewPanel::onPlayPause);
    connect(m_fpsSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, &PreviewPanel::onFpsChanged);
    connect(m_bgColorBtn, &QPushButton::clicked, this, &PreviewPanel::onPickBgColor);
    connect(m_bgImageBtn, &QPushButton::clicked, this, &PreviewPanel::onPickBgImage);
    connect(m_bgClearBtn, &QPushButton::clicked, this, &PreviewPanel::onClearBgImage);
}

void PreviewPanel::onProjectLoaded()
{
    const auto& proj = ProjectController::instance().project();

    // 填充动作下拉
    {
        QSignalBlocker b(m_actionCombo);
        m_actionCombo->clear();
        m_actionCombo->setEnabled(true);
        QStringList acts = proj.unionActions();
        for (const QString& a : acts) m_actionCombo->addItem(a);
        const int idx = m_actionCombo->findText(proj.currentAction);
        if (idx >= 0) m_actionCombo->setCurrentIndex(idx);
    }

    rebuildDirButtons();
    updateFrameLabel();
    prefetchUpcomingFrames(16);   // 项目加载: 预读多一点
    if (m_canvas) m_canvas->requestRender();
}

void PreviewPanel::onActionChanged()
{
    const auto& proj = ProjectController::instance().project();
    QSignalBlocker b(m_actionCombo);
    const int idx = m_actionCombo->findText(proj.currentAction);
    if (idx >= 0) m_actionCombo->setCurrentIndex(idx);

    rebuildDirButtons();
    updateFrameLabel();
    prefetchUpcomingFrames(16);   // 切动作: 后续帧大概率没缓存
}

void PreviewPanel::onDirectionChanged()
{
    const auto& proj = ProjectController::instance().project();
    auto* btn = m_dirGroup->button(proj.currentDirection);
    if (btn && btn->isEnabled()) {
        QSignalBlocker b(m_dirGroup);
        btn->setChecked(true);
    }
    updateFrameLabel();
    prefetchUpcomingFrames(16);   // M9: 切方向预读
}

void PreviewPanel::onFrameChanged()
{
    updateFrameLabel();
    if (m_canvas) m_canvas->requestRender();
}

void PreviewPanel::onActionCombo(int)
{
    const QString a = m_actionCombo->currentText();
    if (!a.isEmpty()) ProjectController::instance().setCurrentAction(a);
}

void PreviewPanel::onDirButton(int dirId)
{
    ProjectController::instance().setCurrentDirection(dirId);
}

void PreviewPanel::onPlayPause()
{
    m_playing = !m_playing;
    if (m_playing) m_playTimer.start(); else m_playTimer.stop();
    m_playBtn->setText(m_playing ? QStringLiteral("⏯") : QStringLiteral("▶"));
}

void PreviewPanel::onFpsChanged(int v)
{
    AppSettings::instance().setFps(v);
    m_playTimer.setInterval(1000 / qMax(1, v));
}

void PreviewPanel::onPickBgColor()
{
    QColor c = QColorDialog::getColor(AppSettings::instance().bgColor(),
                                      this, QStringLiteral("背景色"));
    if (!c.isValid()) return;
    AppSettings::instance().setBgColor(c);
    m_bgColorBtn->setText(c.name(QColor::HexRgb).toUpper());
    m_bgColorBtn->setStyleSheet(QString("background:%1; color:#fff").arg(c.name()));
    if (m_canvas) {
        m_canvas->setClearColor(c);
        m_canvas->requestRender();
    }
}

void PreviewPanel::onPickBgImage()
{
    QString p = QFileDialog::getOpenFileName(this,
        QStringLiteral("选择背景图"),
        QString(),
        QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp *.tga)"));
    if (p.isEmpty()) return;
    AppSettings::instance().setBgImage(p);
    m_bgImagePath = p;
    m_bgImageTex = FrameLoader::instance().get(p);
    if (m_canvas) m_canvas->requestRender();
}

void PreviewPanel::onClearBgImage()
{
    AppSettings::instance().setBgImage(QString());
    m_bgImagePath.clear();
    m_bgImageTex.reset();
    if (m_canvas) m_canvas->requestRender();
}

void PreviewPanel::onTick()
{
    if (!m_playing) return;
    ProjectController::instance().advanceFrame();
    prefetchUpcomingFrames(8);
}

// 输出 GIF: 当前动作全帧 × 多方案网格. 走 PreviewPanel::render() 复用所有渲染逻辑,
// 离屏 RTV (尺寸根据画布 cells 网格计算) → readback → gif.h.
void PreviewPanel::onExportGif()
{
    auto& ctl = ProjectController::instance();
    const auto& proj = ctl.project();

    if (proj.layers.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("输出 GIF"),
            QStringLiteral("还没加载源目录"));
        return;
    }
    const int totalFrames = proj.totalFrames();
    if (totalFrames < 1) {
        QMessageBox::information(this, QStringLiteral("输出 GIF"),
            QStringLiteral("当前动作没有可用帧"));
        return;
    }

    // 选输出文件
    QString defName = QString("%1_%2_%3frame.gif")
        .arg(QFileInfo(proj.sourceRoot).fileName(),
             proj.currentAction.isEmpty() ? "stand" : proj.currentAction)
        .arg(totalFrames);
    QString lastDir = AppSettings::instance().lastOutputDir();
    if (lastDir.isEmpty()) lastDir = proj.sourceRoot;
    QString outPath = QFileDialog::getSaveFileName(this,
        QStringLiteral("保存 GIF 到..."),
        QDir(lastDir).filePath(defName),
        QStringLiteral("GIF 动画 (*.gif)"));
    if (outPath.isEmpty()) return;
    if (!outPath.endsWith(".gif", Qt::CaseInsensitive)) outPath += ".gif";

    // === 计算 GIF 尺寸 ===
    //   策略: 复用 PreviewPanel cell 网格布局, 但锁定 zoom=1.0 以稳定输出.
    //   尺寸 = cols*cellW + (cols-1)*gapXScaled, rows*cellH + (rows-1)*gapYScaled.
    //   cellW/H = baseW/H (TGA 实际尺寸, 100% zoom).
    int baseW = 500, baseH = 500;   // 默认 (会被实际帧覆盖)
    if (!proj.layers.isEmpty()) {
        const auto& a = proj.layers.first().action(proj.currentAction);
        if (a && a->framesByDir.contains(proj.currentDirection)
              && !a->framesByDir[proj.currentDirection].isEmpty()) {
            // 取一帧推断尺寸
            auto tex = FrameLoader::instance().get(a->framesByDir[proj.currentDirection].first());
            if (tex) { baseW = tex->width(); baseH = tex->height(); }
        }
    }

    // 复用 PreviewPanel 的 28 上限 + 自适应布局
    const int N = qMin((int)proj.schemes.size(), 28);
    if (N < 1) {
        QMessageBox::information(this, QStringLiteral("输出 GIF"),
            QStringLiteral("没有方案可输出")); return;
    }
    const int cols = qMin(N, 7);
    const int rows = (N + cols - 1) / cols;
    const int cellW = baseW;
    const int cellH = baseH;
    const int gapX  = qBound(-baseW + 50, m_charGapXPx, 500);
    const int gapY  = qBound(-baseH + 50, m_charGapYPx, 500);
    const int stepX = cellW + gapX;
    const int stepY = cellH + gapY;
    const int gridW = stepX * (cols - 1) + cellW;
    const int gridH = stepY * (rows - 1) + cellH;
    const int padX = 40, padY = 40;
    const int gifW = qMax(64, gridW + padX * 2);
    const int gifH = qMax(64, gridH + padY * 2);

    // 不限大小 (用户要求与预览一致, 不压缩降采样).

    // === 准备临时切换 (导出过程中暂停播放, 临时改 m_showLabel = m_gifShowId) ===
    const bool wasPlaying = m_playing;
    const bool savedShowLabel = m_showLabel;
    if (m_playTimer.isActive()) m_playTimer.stop();
    m_playing = false;
    m_showLabel = m_gifShowId;
    m_renderingForGif = true;      // 关闭三角标识 (用户要求 GIF 干净)
    const int savedFrame = proj.currentFrame;

    // 为 GIF 渲染搭一份"独立"的 cells 计算: 不依赖 D3DWidget 当前 zoom/pan.
    // 直接渲染到外部 RTV. 复用 m_renderer 的 renderCells.
    auto renderToRtv = [&](ID3D11RenderTargetView* rtv, int rtvW, int rtvH){
        if (!m_renderer) return;
        // gridW/gridH 居中放在 rtv 内
        const int originX = (rtvW - gridW) / 2;
        const int originY = (rtvH - gridH) / 2;

        std::vector<FrameRenderer::Cell> cells;
        cells.reserve(N);
        const int curIdx = proj.currentSchemeIndex;
        for (int i = 0; i < N; ++i) {
            const int row = i / cols;
            const int col = i % cols;
            FrameRenderer::Cell c;
            c.x = originX + col * stepX;
            c.y = originY + row * stepY;
            c.w = cellW;
            c.h = cellH;
            // 直接调 PreviewPanel 私有 buildItemsForScheme? 没暴露. 走 lambda 复制:
            // 复用渲染主路径 — 借用 render() 成员函数, 把它 RTV 重定向到外部.
            // 简化: 这里用 PreviewPanel::render(rtv, w, h), 它内部会重新 build cells,
            // 但 cells 布局会跟当前 D3DWidget zoom 关联. 不理想.
            // 折中: 把 m_canvas->setContentZoom 临时锁 1.0, 让 render 用 1.0 cells.
            c.items.clear();   // 实际 items 由 PreviewPanel::render 自己 buildItemsForScheme
            (void)c;           // 这里不用, render() 自己重建
            (void)cells;
            (void)curIdx;
        }
        // 走 PreviewPanel::render — 它会自己用 m_charGapX/Y + m_canvas 的 zoom (1.0) 算 cells
        this->render(rtv, rtvW, rtvH);
    };

    // 临时把 zoom 锁到 1.0
    double savedZoom = 1.0;
    if (m_canvas) {
        savedZoom = m_canvas->contentZoom();
        m_canvas->setContentZoom(1.0);
    }

    // 状态栏提示
    if (auto* mw = qobject_cast<QMainWindow*>(this->window())) {
        if (mw->statusBar()) {
            mw->statusBar()->showMessage(
                QStringLiteral("正在生成 GIF (%1 帧, %2×%3) ...").arg(totalFrames).arg(gifW).arg(gifH));
        }
    }
    QApplication::processEvents();

    GifExporter::Options opts;
    opts.outPath   = outPath;
    opts.width     = gifW;
    opts.height    = gifH;
    opts.fps       = qBound(1, m_fpsSpin ? m_fpsSpin->value() : 12, 50);
    opts.loopCount = qBound(0, m_gifLoop, 9999);
    opts.bgColor   = AppSettings::instance().bgColor();
    opts.showLabel = m_gifShowId;

    auto setFrame = [&](int f){
        ctl.setCurrentFrame(f);
        // 强制等待异步加载: 这里粗暴用 processEvents 几次, 让 FrameLoader::pump 推 GPU 上传
        for (int i = 0; i < 4; ++i) QApplication::processEvents();
    };

    auto result = GifExporter::exportGif(opts, totalFrames, setFrame, renderToRtv);

    // 恢复
    m_showLabel = savedShowLabel;
    m_renderingForGif = false;
    if (m_canvas) m_canvas->setContentZoom(savedZoom);
    ctl.setCurrentFrame(savedFrame);
    m_playing = wasPlaying;
    if (m_playing) m_playTimer.start(qMax(1, 1000 / qMax(1, m_fpsSpin ? m_fpsSpin->value() : 10)));

    if (auto* mw = qobject_cast<QMainWindow*>(this->window())) {
        if (mw->statusBar()) mw->statusBar()->clearMessage();
    }

    if (!result.ok) {
        QMessageBox::warning(this, QStringLiteral("输出 GIF 失败"), result.error);
        return;
    }
    AppSettings::instance().setLastOutputDir(QFileInfo(outPath).absolutePath());
    QMessageBox::information(this, QStringLiteral("输出 GIF"),
        QStringLiteral("写出 %1 帧, %2 KB\n%3")
            .arg(result.framesWritten)
            .arg(result.bytesWritten / 1024)
            .arg(outPath));
}

void PreviewPanel::prefetchUpcomingFrames(int aheadN)
{
    const auto& proj = ProjectController::instance().project();
    if (proj.layers.isEmpty()) return;

    const QString action = proj.currentAction;
    const int dir = proj.currentDirection;
    if (action.isEmpty()) return;

    QStringList paths;
    paths.reserve(aheadN * proj.layers.size());

    for (const auto& l : proj.layers) {
        if (!proj.isLayerVisible(l)) continue;
        const Action* a = l.action(action);
        if (!a) continue;
        int useDir = dir;
        if (!a->framesByDir.contains(useDir) && !a->framesByDir.isEmpty()) {
            useDir = a->framesByDir.firstKey();
        }
        const int n = a->frameCount(useDir);
        if (n <= 0) continue;
        // 从下一帧开始预读 aheadN 个 (循环范围)
        for (int k = 1; k <= aheadN; ++k) {
            const int idx = (proj.currentFrame + k) % n;
            const QString p = a->framePath(useDir, idx);
            if (!p.isEmpty()) paths << p;
        }
    }
    if (!paths.isEmpty()) FrameLoader::instance().prefetch(paths);
}

void PreviewPanel::rebuildDirButtons()
{
    const auto& proj = ProjectController::instance().project();
    const auto dirs = proj.availableDirections();
    QSet<int> avail; for (int d : dirs) avail.insert(d);

    for (auto* b : m_dirGroup->buttons()) {
        const int id = m_dirGroup->id(b);
        b->setEnabled(avail.contains(id));
        b->setChecked(false);
    }
    auto* cur = m_dirGroup->button(proj.currentDirection);
    if (cur && cur->isEnabled()) cur->setChecked(true);
}

void PreviewPanel::updateFrameLabel()
{
    const auto& proj = ProjectController::instance().project();
    const int total = proj.totalFrames();
    m_frameLabel->setText(QString(QStringLiteral("当前: %1/%2"))
                          .arg(proj.currentFrame + 1)
                          .arg(total));
}

void PreviewPanel::updateZoomLabel()
{
    if (!m_zoomLabel || !m_canvas) return;
    const double z = m_canvas->contentZoom();
    int pct = (int)std::lround(z * 100.0);
    m_zoomLabel->setText(QString::number(pct) + "%");
}

void PreviewPanel::render(ID3D11RenderTargetView* rtv, int w, int h)
{
    if (!m_renderer) return;
    if (!m_renderer->init()) return;

    const auto& proj = ProjectController::instance().project();
    auto& loader = FrameLoader::instance();

    // M9: 主线 pump 已解码的后台帧 → 上传 GPU. 单帧最多 4 张, 防卡顿.
    loader.pump(4);

    // 没方案 → 单 cell 整 viewport, 啥都不画 (clear 即可)
    if (proj.layers.isEmpty() || proj.schemes.isEmpty()) {
        std::vector<FrameRenderer::Cell> empty;
        m_renderer->renderCells(rtv, w, h, AppSettings::instance().bgColor(), empty, m_bgImageTex);
        return;
    }

    // 收集每个方案的 DrawItem 列表 (frame×layer 同步, effects 取自该方案).
    auto buildItemsForScheme = [&](const Scheme& sc) {
        std::vector<FrameRenderer::DrawItem> out;
        out.reserve(proj.layers.size());
        for (const auto& l : proj.layers) {
            if (!proj.isLayerVisible(l)) continue;
            const Action* a = l.action(proj.currentAction);
            if (!a) continue;
            int useDir = proj.currentDirection;
            if (!a->framesByDir.contains(useDir) || a->framesByDir.value(useDir).isEmpty()) {
                if (a->framesByDir.isEmpty()) continue;
                useDir = a->framesByDir.firstKey();
            }
            const QString path = a->framePath(useDir, proj.currentFrame);
            if (path.isEmpty()) continue;
            auto tex = loader.get(path);
            if (!tex) continue;

            FrameRenderer::DrawItem di;
            di.tex = tex;
            di.visible = true;

            // M7: 肤色保护层 → 强制走本体, 不变色
            if (proj.isSkinSafe(l)) {
                out.push_back(std::move(di));
                continue;
            }

            if (sc.isBaked) {
                // 已烘焙方案: 走 recolor PS, 用该层对应的 add_lut/0N.png
                const QString lp = sc.layerLutPath.value(l.key());
                if (!lp.isEmpty()) {
                    di.lut = loader.get(lp);
                    di.useLut = (di.lut && di.lut->isValid());
                }
                // 该层无对应 LUT → 走本体 (di.useLut=false, di.effects=null)
            } else if (!sc.isBuiltin) {
                // 用户手编方案: 走 effect_chain PS
                di.effects = proj.effectsForIn(sc, l);
            } else {
                // 本体方案: 兼容老 Ctrl+1..6 已应用的 LUT 路径 (一般为空)
                const QString lutPath = proj.lutPathFor(l);
                if (!lutPath.isEmpty()) {
                    di.lut = loader.get(lutPath);
                    di.useLut = (di.lut && di.lut->isValid());
                }
            }
            out.push_back(std::move(di));
        }
        return out;
    };

    // 估算 cell 基础像素大小 (取 body 帧大小, 兜底 360×360).
    int baseW = 360, baseH = 360;
    if (!proj.layers.isEmpty()) {
        const auto& first = proj.layers.first();
        const Action* a = first.action(proj.currentAction);
        if (a) {
            int useDir = proj.currentDirection;
            if (!a->framesByDir.contains(useDir) && !a->framesByDir.isEmpty())
                useDir = a->framesByDir.firstKey();
            const QString p = a->framePath(useDir, proj.currentFrame);
            if (!p.isEmpty()) {
                if (auto tex = loader.get(p)) {
                    baseW = tex->width();
                    baseH = tex->height();
                }
            }
        }
    }

    // 网格布局 (M5 自适应):
    //   N ≤ 7  → 1 行 N 列
    //   N ≤ 14 → 2 行 7 列 (上 7 / 下 N-7)
    //   N ≤ 21 → 3 行 7 列
    //   N ≤ 28 → 4 行 7 列 (上限)
    // 排列规则: 本体方案永远在 "右起第 1 格" (idx 顺序 [0,1,2,...] 从右到左).
    //
    // "🔒 只看锁定" 模式: 过滤出本体 + 全部 locked 方案, 把它们当成一组新的 schemes
    //                   重新走 N/cols/rows 布局; 原始 idx 通过 visibleIdx 映射保留 (label 显示).
    QVector<int> visibleIdx;
    {
        const int total = (int)proj.schemes.size();
        if (m_hideUnlocked) {
            for (int i = 0; i < total; ++i) {
                if (proj.schemes[i].isBuiltin || proj.schemes[i].locked) visibleIdx.push_back(i);
            }
            // 边缘: 没有任何锁定方案 → 至少保留本体, 否则画布空白用户摸不着头脑
            if (visibleIdx.size() <= 1 && total > 0) {
                visibleIdx.clear();
                for (int i = 0; i < total; ++i) visibleIdx.push_back(i);
            }
        } else {
            for (int i = 0; i < total; ++i) visibleIdx.push_back(i);
        }
    }
    const int N = qMin(visibleIdx.size(), 28);
    const int cols = qMin(N, 7);
    const int rows = (N + cols - 1) / cols;

    const double zoom = m_canvas->contentZoom();
    const int cellW = qMax(1, (int)std::floor(baseW * zoom));
    const int cellH = qMax(1, (int)std::floor(baseH * zoom));

    // X / Y 轴间距: 用户在 100% 缩放下输入的目标间距, 按 zoom 等比缩放.
    //   gap@zoom = m_charGapXPx × zoom / m_charGapYPx × zoom
    //   每格步长 = cell + gap (gap 可负, 让角色重叠)
    const int gapXScaled = (int)std::round(m_charGapXPx * zoom);
    const int gapYScaled = (int)std::round(m_charGapYPx * zoom);
    const int stepX = cellW + gapXScaled;
    const int stepY = cellH + gapYScaled;
    const int gridW = stepX * (cols - 1) + cellW;     // 总宽 (含负间距时可能小于 cellW*cols)
    const int gridH = stepY * (rows - 1) + cellH;

    // pan: D3DWidget 单位是 widget 像素, 需转 RTV 像素 (devicePixelRatio).
    const double dpr = m_canvas->devicePixelRatioF();
    const int panX = (int)std::round(m_canvas->contentPanX() * dpr);
    const int panY = (int)std::round(m_canvas->contentPanY() * dpr);

    // grid 居中后 + pan 偏移. 这是 grid 左上角.
    const int originX = (w - gridW) / 2 + panX;
    const int originY = (h - gridH) / 2 + panY;

    // 计算每个方案的 (row, col): 本体 (idx=0) 在第 0 行最左, 方案01 → 同行左起第 2, ...
    //   idx i 在视觉位置: row = i / cols, col = i % cols (从左到右)
    const int curIdx = proj.currentSchemeIndex;

    // 闪烁 alpha: 1.2s 周期, 用三角波 0.25..1.0
    auto blinkAlpha = [this]() -> float {
        const double T = 1200.0;
        double t = (double)m_blinkPhaseMs / T;          // 0..1
        double tri = std::abs(t * 2.0 - 1.0);           // 0..1..0
        // 反过来让 0→full, 中间→暗
        double a = 0.25 + 0.75 * tri;
        return (float)a;
    };

    // K 点位置 (相对原始 TGA 像素), 然后缩放到 cell viewport.
    //   返回 (kxScaled, kyScaled + 50×scale): "三角顶点 ▲ 应在的位置".
    //   "+50" 是 K 点下方 50px(原始尺寸下), zoom 缩放后视觉间距随之等比 → 跟随缩放.
    //   三角"大小"用 cell.triPixelW/H 固定像素, 不缩放.
    auto cellTriTip = [&](int cellW, int cellH) -> QPoint {
        const QPoint k0 = kPointForSize(QSize(baseW, baseH));   // K 点 (原始 TGA 像素)
        float scale = qMin(float(cellW) / baseW, float(cellH) / baseH);
        if (scale > 1.0f) scale = 1.0f;
        int wpx = (int)std::floor(baseW * scale + 0.5f);
        int hpx = (int)std::floor(baseH * scale + 0.5f);
        int xOff = (cellW - wpx) / 2;
        int yOff = (cellH - hpx) / 2;
        const int kxScaled = xOff + (int)std::round(k0.x() * scale);
        const int kyScaled = yOff + (int)std::round(k0.y() * scale);
        const int triOffsetY = (int)std::round(50.0 * (double)scale);
        return QPoint(kxScaled, kyScaled + triOffsetY);
    };

    // label 顶 y = cell 顶 + m_labelGapY * scale (随 zoom 等比, 与"X/Y 间距"一致语义).
    // m_labelGapY > 0 → 向下偏移; < 0 → 向上 (会被钳回 0). 默认 30px.
    // 缩放规则: scale = zoom (而非 cell/base 比, 否则放大时被钳到 1.0 → ID 不跟随放大).
    auto cellLabelY = [&](int cellW, int cellH) -> int {
        const double scale = zoom;          // 直接用画布 zoom, 放大缩小都等比
        int y = (int)std::round(m_labelGapY * scale);
        return qBound(0, y, qMax(0, cellH - 30));
    };

    std::vector<FrameRenderer::Cell> cells;
    cells.reserve(N);
    for (int i = 0; i < N; ++i) {
        const int row = i / cols;
        const int col = i % cols;
        const int realIdx = visibleIdx[i];      // 原 schemes 下标 (供 label / 高亮匹配)
        FrameRenderer::Cell c;
        c.x = originX + col * stepX;
        c.y = originY + row * stepY;
        c.w = cellW;
        c.h = cellH;
        c.items = buildItemsForScheme(proj.schemes[realIdx]);
        c.highlighted = (realIdx == curIdx);

        // 画布 label: 仅 ID (如 "02"); 锁住的方案末尾追加 "-锁".
        // 本体 (idx 0) 不画 label (用户偏好: 仅编号; 本体无意义).
        if (m_showLabel && realIdx > 0) {
            QString text = QString("%1").arg(realIdx, 2, 10, QChar('0'));
            if (proj.schemes[realIdx].locked) text += QStringLiteral("-锁");
            c.label = text;
            // 锁定时换暖色, 易区分
            c.labelColor = proj.schemes[realIdx].locked ? QColor(255, 200, 80)
                                                        : QColor(220, 220, 220);
            c.labelY = cellLabelY(cellW, cellH);
        }
        if (c.highlighted && !m_renderingForGif) {
            const QPoint tip = cellTriTip(cellW, cellH);
            // 把"三角顶点位置"作为 kpoint 传, FrameRenderer 直接用 (不再 +50).
            c.kpointX = tip.x();
            c.kpointY = tip.y();
            c.triPixelW = 24;     // 大小固定, 不随 zoom 变
            c.triPixelH = 22;
            c.highlightAlpha = blinkAlpha();
        }
        cells.push_back(std::move(c));
    }

    m_renderer->renderCells(rtv, w, h, AppSettings::instance().bgColor(), cells, m_bgImageTex);
}

} // namespace HighPro
