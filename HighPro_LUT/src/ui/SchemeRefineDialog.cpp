#include "SchemeRefineDialog.h"
#include "app/ProjectController.h"
#include "core/Project.h"
#include "core/LayerData.h"
#include "ui/widgets/CurveEditor.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QListWidget>
#include <QListWidgetItem>
#include <QStackedWidget>
#include <QLabel>
#include <QPushButton>
#include <QGroupBox>
#include <QSlider>
#include <QSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QFrame>
#include <QScrollArea>
#include <QSet>
#include <QShortcut>
#include <QKeySequence>
#include <algorithm>

namespace HighPro {

namespace {

// 复用 EffectPanel 风格的 label + slider + spinbox 行.
//   - onChange: 每次值变化都触发 (用于写 working effects + debounce 实时预览刷)
//   - onCommit: 仅 "拖动会话结束" 触发一次 (sliderReleased / spin editingFinished / 键盘步进),
//               用于在弹窗 undo 栈中 push 一个"会话结束"快照, 避免一次拖动产生 N 个快照
class SliderRow : public QWidget
{
public:
    SliderRow(const QString& label, int minv, int maxv, int defv,
              std::function<void(int)> onChange,
              std::function<void()>    onCommit,
              QWidget* parent = nullptr)
        : QWidget(parent), m_cb(std::move(onChange)), m_commit(std::move(onCommit))
    {
        auto* lay = new QHBoxLayout(this);
        lay->setContentsMargins(2, 1, 2, 1);
        lay->setSpacing(6);

        auto* lab = new QLabel(label, this);
        lab->setMinimumWidth(70);
        lab->setMaximumWidth(70);
        lay->addWidget(lab);

        m_slider = new QSlider(Qt::Horizontal, this);
        m_slider->setRange(minv, maxv);
        m_slider->setValue(defv);
        lay->addWidget(m_slider, 1);

        m_spin = new QSpinBox(this);
        m_spin->setRange(minv, maxv);
        m_spin->setValue(defv);
        m_spin->setFixedWidth(64);
        // spin 通过 Enter / 失焦 提交 — 阻止它每次按方向键都立即 push undo (与拖动语义一致)
        m_spin->setKeyboardTracking(true);
        lay->addWidget(m_spin);

        connect(m_slider, &QSlider::valueChanged, this, [this](int v){
            QSignalBlocker b(m_spin); m_spin->setValue(v);
            if (m_cb) m_cb(v);
        });
        connect(m_spin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int v){
            QSignalBlocker b(m_slider); m_slider->setValue(v);
            if (m_cb) m_cb(v);
        });

        // ===== "会话结束" 回调: 各类用户主动确认值的入口 =====
        // 1) 鼠标拖动结束
        connect(m_slider, &QSlider::sliderReleased, this, [this]{
            if (m_commit) m_commit();
        });
        // 2) 键盘/滚轮单次步进 (sliderPressed=false 路径) — actionTriggered 覆盖
        //    actionTriggered 在键盘/PageUp/PageDown/滚轮触发, 不在拖动中触发 (拖动走 sliderMoved)
        connect(m_slider, &QSlider::actionTriggered, this, [this](int action){
            if (action == QAbstractSlider::SliderMove) return;     // 拖动中, 不算提交
            // 单步动作 (按键/滚轮/PageStep/SingleStep) → 算一次提交
            if (m_commit) m_commit();
        });
        // 3) spinbox 编辑完成 (Enter / 失焦)
        connect(m_spin, &QSpinBox::editingFinished, this, [this]{
            if (m_commit) m_commit();
        });
    }
private:
    QSlider*  m_slider = nullptr;
    QSpinBox* m_spin   = nullptr;
    std::function<void(int)> m_cb;
    std::function<void()>    m_commit;
};

// 层的人类可读显示名 (与 LayerTreePanel 对齐): body / 00 / 01 / addon/01 ...
QString layerDisplay(const LayerData& l)
{
    if (!l.displayName.isEmpty()) return l.displayName;
    return l.key();
}

} // namespace

// ---------------------------------------------------------------------------

SchemeRefineDialog::SchemeRefineDialog(int schemeIdx, QWidget* parent)
    : QDialog(parent), m_schemeIdx(schemeIdx)
{
    setWindowTitle(QStringLiteral("❤️细化方案"));
    resize(615, 1055);
    setModal(true);             // 模态; exec() 期间画布仍接收 effectsChanged 实时刷新

    const auto& proj = ProjectController::instance().project();
    if (m_schemeIdx < 0 || m_schemeIdx >= proj.schemes.size()) return;
    const auto& sc = proj.schemes[m_schemeIdx];

    // 拷贝层 effects 到工作集 + 快照. 快照用来"取消"时整体回滚.
    for (const auto& l : proj.layers) {
        const QString k = l.key();
        auto it = sc.layerEffects.find(k);
        EffectStack es = (it == sc.layerEffects.end()) ? EffectStack{} : it.value();
        m_workingEffects.insert(k, es);
    }
    m_snapshotForReset = m_workingEffects;

    // debounce 节流: 高频拖滑块时, 30ms 内只 flush 一次到 Project, 避免 D3D 重烘卡顿
    m_applyDebounce.setSingleShot(true);
    m_applyDebounce.setInterval(30);
    connect(&m_applyDebounce, &QTimer::timeout, this, &SchemeRefineDialog::flushPendingToProject);

    // 弹窗内 Ctrl+Z / Ctrl+Y. 用 WidgetWithChildrenShortcut 让滑块/spin 拥有焦点时也触发.
    {
        auto* scUndo = new QShortcut(QKeySequence::Undo, this);     // Ctrl+Z
        scUndo->setContext(Qt::WidgetWithChildrenShortcut);
        connect(scUndo, &QShortcut::activated, this, &SchemeRefineDialog::localUndo);

        auto* scRedo = new QShortcut(QKeySequence("Ctrl+Y"), this);
        scRedo->setContext(Qt::WidgetWithChildrenShortcut);
        connect(scRedo, &QShortcut::activated, this, &SchemeRefineDialog::localRedo);

        // 额外: 某些键盘布局下用 Ctrl+Shift+Z 重做, 一并支持
        auto* scRedo2 = new QShortcut(QKeySequence("Ctrl+Shift+Z"), this);
        scRedo2->setContext(Qt::WidgetWithChildrenShortcut);
        connect(scRedo2, &QShortcut::activated, this, &SchemeRefineDialog::localRedo);
    }

    buildUi();
    populateLayerList();
}

void SchemeRefineDialog::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(8);

    // 顶部: 方案名标题
    {
        const auto& proj = ProjectController::instance().project();
        const QString schemeName = (m_schemeIdx >= 0 && m_schemeIdx < proj.schemes.size())
            ? proj.schemes[m_schemeIdx].name
            : QStringLiteral("(未知方案)");
        m_titleLbl = new QLabel(
            QStringLiteral("❤️ 细化方案: <b>%1</b>   <span style='color:#888;'>"
                           "(滑块实时刷新画布; 取消 = 回滚到打开前)</span>")
                .arg(schemeName.toHtmlEscaped()), this);
        m_titleLbl->setStyleSheet("font-size:14px;padding:4px 2px;");
        root->addWidget(m_titleLbl);
    }

    // 中部: 左 层列表 / 右 效果栈
    auto* mid = new QHBoxLayout();
    mid->setSpacing(8);

    m_list = new QListWidget(this);
    // 宽度回到 190; 行高目标 ~35px (字号略大于原始, padding 收紧)
    m_list->setFixedWidth(190);
    // 不省略文本, 避免 Qt 在选中/悬停时弹出省略文本浮层遮挡相邻行
    m_list->setTextElideMode(Qt::ElideNone);
    m_list->setUniformItemSizes(true);
    m_list->setStyleSheet(
        "QListWidget{background:#262626;color:#ddd;border:1px solid #3a3a3a;outline:0;"
        "font-size:15px;}"
        // 行高 ≈ 35px (font 15 + padding 7*2 + 边距)
        "QListWidget::item{padding:7px 10px;border:0;min-height:21px;}"
        "QListWidget::item:hover{background:#2f2f2f;}"
        // 选中: 加粗 + 黄色文字 (要求: 选中层文字变粗 + 黄色字体颜色)
        "QListWidget::item:selected{background:#3b6ea8;color:#ffd540;font-weight:bold;}"
        "QListWidget::item:selected:!active{background:#3b6ea8;color:#ffd540;font-weight:bold;}"
    );
    connect(m_list, &QListWidget::currentRowChanged, this, &SchemeRefineDialog::onLayerChanged);
    mid->addWidget(m_list, 0);

    m_stack = new QStackedWidget(this);
    m_stack->setStyleSheet("QStackedWidget{background:#2a2a2a;border:1px solid #3a3a3a;}");
    mid->addWidget(m_stack, 1);

    root->addLayout(mid, 1);

    // 底部按钮
    auto* btnRow = new QHBoxLayout();
    btnRow->setSpacing(8);

    auto* btnResetCur = new QPushButton(QStringLiteral("↺ 重置当前层细化"), this);
    btnResetCur->setToolTip(QStringLiteral("把当前层的 HSL / 亮度对比度 回到打开弹窗时的状态 (其他效果保留)"));
    connect(btnResetCur, &QPushButton::clicked, this, &SchemeRefineDialog::onResetCurrentLayer);
    btnRow->addWidget(btnResetCur);

    btnRow->addStretch(1);

    auto* btnCancel = new QPushButton(QStringLiteral("取消"), this);
    auto* btnSave   = new QPushButton(QStringLiteral("保存"), this);
    btnSave->setStyleSheet("QPushButton{background:#3b6ea8;color:#fff;padding:4px 16px;}"
                           "QPushButton:hover{background:#4a85c5;}");
    btnSave->setDefault(true);
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(btnSave,   &QPushButton::clicked, this, &QDialog::accept);
    btnRow->addWidget(btnCancel);
    btnRow->addWidget(btnSave);

    root->addLayout(btnRow);
}

void SchemeRefineDialog::populateLayerList()
{
    if (!m_list || !m_stack) return;
    m_list->clear();
    m_layerKeys.clear();
    while (m_stack->count() > 0) {
        QWidget* w = m_stack->widget(0);
        m_stack->removeWidget(w);
        w->deleteLater();
    }

    const auto& proj = ProjectController::instance().project();

    // 层级顺序对齐资源树: addon → numbered (高→低) → body.
    // (proj.layers 自身顺序为 body → numbered (低→高) → addon, 是反的.)
    QVector<const LayerData*> addonList;
    QVector<const LayerData*> numberedList;
    const LayerData* bodyLayer = nullptr;
    for (const auto& l : proj.layers) {
        if      (l.kind == LayerKind::Addon)    addonList.push_back(&l);
        else if (l.kind == LayerKind::Numbered) numberedList.push_back(&l);
        else if (l.kind == LayerKind::Body)     bodyLayer = &l;
    }
    std::sort(numberedList.begin(), numberedList.end(),
              [](const LayerData* a, const LayerData* b){
                  return a->numberedIdx > b->numberedIdx;
              });

    QVector<const LayerData*> ordered;
    ordered.reserve(addonList.size() + numberedList.size() + (bodyLayer ? 1 : 0));
    for (const auto* l : addonList)    ordered.push_back(l);
    for (const auto* l : numberedList) ordered.push_back(l);
    if (bodyLayer) ordered.push_back(bodyLayer);

    for (const auto* lp : ordered) {
        const auto& l = *lp;
        const QString k = l.key();
        auto* item = new QListWidgetItem(layerDisplay(l));
        item->setData(Qt::UserRole, k);
        m_list->addItem(item);
        m_layerKeys.append(k);
        m_stack->addWidget(buildEffectPanel(k));
    }
    if (m_list->count() > 0) m_list->setCurrentRow(0);
}

QWidget* SchemeRefineDialog::buildEffectPanel(const QString& layerKey)
{
    auto* page = new QWidget(m_stack);
    auto* outer = new QVBoxLayout(page);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto* scroll = new QScrollArea(page);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFrameShape(QFrame::NoFrame);

    auto* content = new QWidget(scroll);
    auto* lay = new QVBoxLayout(content);
    lay->setContentsMargins(8, 8, 8, 8);
    lay->setSpacing(8);

    EffectStack& es = workingFor(layerKey);

    // 1. 色相/饱和度
    {
        auto* box = new QGroupBox(QStringLiteral("色相 / 饱和度"), content);
        box->setCheckable(true);
        box->setChecked(es.enabled[EffectStack::EHsl]);

        auto* body = new QWidget(box);
        auto* l = new QVBoxLayout(body);
        l->setContentsMargins(0, 0, 0, 0); l->setSpacing(2);
        auto commit = [this]{ commitDragSession(); };
        l->addWidget(new SliderRow(QStringLiteral("色相"),   -180, 180, es.hsl.hue,
            [this, layerKey](int v){
                auto& s = workingFor(layerKey);
                s.hsl.hue = v; s.enabled[EffectStack::EHsl] = true;
                scheduleApplyToProject(layerKey);
            }, commit));
        l->addWidget(new SliderRow(QStringLiteral("饱和度"), -100, 100, es.hsl.saturation,
            [this, layerKey](int v){
                auto& s = workingFor(layerKey);
                s.hsl.saturation = v; s.enabled[EffectStack::EHsl] = true;
                scheduleApplyToProject(layerKey);
            }, commit));
        l->addWidget(new SliderRow(QStringLiteral("亮度"),   -100, 100, es.hsl.lightness,
            [this, layerKey](int v){
                auto& s = workingFor(layerKey);
                s.hsl.lightness = v; s.enabled[EffectStack::EHsl] = true;
                scheduleApplyToProject(layerKey);
            }, commit));

        auto* boxLay = new QVBoxLayout(box);
        boxLay->setContentsMargins(8, 8, 8, 8);
        boxLay->addWidget(body);
        body->setEnabled(box->isChecked());
        connect(box, &QGroupBox::toggled, this, [this, layerKey, body](bool on){
            auto& s = workingFor(layerKey);
            s.enabled[EffectStack::EHsl] = on;
            body->setEnabled(on);
            scheduleApplyToProject(layerKey);
            commitDragSession();        // 开关是离散动作, 立即提交一个 undo 步
        });
        lay->addWidget(box);
    }

    // 2. 亮度/对比度
    {
        auto* box = new QGroupBox(QStringLiteral("亮度 / 对比度"), content);
        box->setCheckable(true);
        box->setChecked(es.enabled[EffectStack::EBrtCtr]);

        auto* body = new QWidget(box);
        auto* l = new QVBoxLayout(body);
        l->setContentsMargins(0, 0, 0, 0); l->setSpacing(2);
        auto commit = [this]{ commitDragSession(); };
        l->addWidget(new SliderRow(QStringLiteral("亮度"),   -150, 150, es.brtCtr.brightness,
            [this, layerKey](int v){
                auto& s = workingFor(layerKey);
                s.brtCtr.brightness = v; s.enabled[EffectStack::EBrtCtr] = true;
                scheduleApplyToProject(layerKey);
            }, commit));
        l->addWidget(new SliderRow(QStringLiteral("对比度"), -100, 100, es.brtCtr.contrast,
            [this, layerKey](int v){
                auto& s = workingFor(layerKey);
                s.brtCtr.contrast = v; s.enabled[EffectStack::EBrtCtr] = true;
                scheduleApplyToProject(layerKey);
            }, commit));

        auto* boxLay = new QVBoxLayout(box);
        boxLay->setContentsMargins(8, 8, 8, 8);
        boxLay->addWidget(body);
        body->setEnabled(box->isChecked());
        connect(box, &QGroupBox::toggled, this, [this, layerKey, body](bool on){
            auto& s = workingFor(layerKey);
            s.enabled[EffectStack::EBrtCtr] = on;
            body->setEnabled(on);
            scheduleApplyToProject(layerKey);
            commitDragSession();
        });
        lay->addWidget(box);
    }

    // 3. 曲线
    {
        auto* box = new QGroupBox(QStringLiteral("3. 曲线"), content);
        box->setCheckable(true);
        box->setChecked(es.enabled[EffectStack::ECurves]);

        auto* body = new QWidget(box);
        auto* l = new QVBoxLayout(body);
        l->setContentsMargins(0, 0, 0, 0); l->setSpacing(4);

        auto* topRow = new QHBoxLayout();
        topRow->setSpacing(6);
        auto* combo = new QComboBox(body);
        combo->addItem(QStringLiteral("RGB"));
        combo->addItem(QStringLiteral("R"));
        combo->addItem(QStringLiteral("G"));
        combo->addItem(QStringLiteral("B"));
        combo->setFixedWidth(80);
        topRow->addWidget(new QLabel(QStringLiteral("通道:"), body));
        topRow->addWidget(combo);

        auto* btnResetCh  = new QPushButton(QStringLiteral("重置当前"), body);
        auto* btnResetAll = new QPushButton(QStringLiteral("重置全部"), body);
        topRow->addWidget(btnResetCh);
        topRow->addWidget(btnResetAll);
        topRow->addStretch(1);
        l->addLayout(topRow);

        auto* editor = new CurveEditor(body);
        editor->setCurves(es.curves);
        editor->setMinimumHeight(220);
        // 恢复"上次查看"的曲线通道 (Ctrl+Z/Ctrl+Y 还原 UI 上下文用)
        const int initCh = m_layerCurveCh.value(layerKey, 0);
        combo->setCurrentIndex(initCh);
        editor->setChannel(initCh);
        l->addWidget(editor, 1);

        connect(combo, qOverload<int>(&QComboBox::currentIndexChanged),
                this, [this, layerKey, editor](int ch){
                    m_layerCurveCh[layerKey] = ch;
                    editor->setChannel(ch);
                });

        // 曲线变化 → 写 working + 调度预览刷新 (高频, 走 debounce)
        connect(editor, &CurveEditor::curvesChanged, this, [this, layerKey, editor]{
            auto& s = workingFor(layerKey);
            s.curves = editor->curves();
            s.enabled[EffectStack::ECurves] = true;
            scheduleApplyToProject(layerKey);
        });
        // 鼠标释放 / 重置 / 加点 / 删点 → 提交一个 undo 步
        connect(editor, &CurveEditor::editingFinished, this, [this]{
            commitDragSession();
        });

        connect(btnResetCh,  &QPushButton::clicked, editor, &CurveEditor::resetCurrent);
        connect(btnResetAll, &QPushButton::clicked, editor, &CurveEditor::resetAll);

        auto* boxLay = new QVBoxLayout(box);
        boxLay->setContentsMargins(8, 8, 8, 8);
        boxLay->addWidget(body);
        body->setEnabled(box->isChecked());
        connect(box, &QGroupBox::toggled, this, [this, layerKey, body](bool on){
            auto& s = workingFor(layerKey);
            s.enabled[EffectStack::ECurves] = on;
            body->setEnabled(on);
            scheduleApplyToProject(layerKey);
            commitDragSession();
        });
        lay->addWidget(box);
    }

    // 4. 通道混合器
    {
        auto* box = new QGroupBox(QStringLiteral("4. 通道混合器"), content);
        box->setCheckable(true);
        box->setChecked(es.enabled[EffectStack::EChMix]);

        auto* body = new QWidget(box);
        auto* l = new QVBoxLayout(body);
        l->setContentsMargins(0, 0, 0, 0); l->setSpacing(2);
        auto commit = [this]{ commitDragSession(); };

        auto addCh = [&](const QString& label, std::function<int*(EffectStack&)> field) {
            EffectStack& s0 = workingFor(layerKey);
            l->addWidget(new SliderRow(label, -200, 200, *field(s0),
                [this, layerKey, field](int v){
                    auto& s = workingFor(layerKey);
                    *field(s) = v;
                    s.enabled[EffectStack::EChMix] = true;
                    scheduleApplyToProject(layerKey);
                }, commit));
        };

        // 主对角线 (默认 100)
        addCh(QStringLiteral("红 → 红"), [](EffectStack& s)->int*{ return &s.chMix.rr; });
        addCh(QStringLiteral("绿 → 绿"), [](EffectStack& s)->int*{ return &s.chMix.gg; });
        addCh(QStringLiteral("蓝 → 蓝"), [](EffectStack& s)->int*{ return &s.chMix.bb; });
        // 副对角 (默认 0)
        addCh(QStringLiteral("红 → 绿"), [](EffectStack& s)->int*{ return &s.chMix.rg; });
        addCh(QStringLiteral("红 → 蓝"), [](EffectStack& s)->int*{ return &s.chMix.rb; });
        addCh(QStringLiteral("绿 → 红"), [](EffectStack& s)->int*{ return &s.chMix.gr; });
        addCh(QStringLiteral("绿 → 蓝"), [](EffectStack& s)->int*{ return &s.chMix.gb; });
        addCh(QStringLiteral("蓝 → 红"), [](EffectStack& s)->int*{ return &s.chMix.br; });
        addCh(QStringLiteral("蓝 → 绿"), [](EffectStack& s)->int*{ return &s.chMix.bg; });

        auto* mono = new QCheckBox(QStringLiteral("单色"), body);
        mono->setChecked(es.chMix.monochrome);
        connect(mono, &QCheckBox::toggled, this, [this, layerKey](bool on){
            auto& s = workingFor(layerKey);
            s.chMix.monochrome = on;
            s.enabled[EffectStack::EChMix] = true;
            scheduleApplyToProject(layerKey);
            commitDragSession();
        });
        l->addWidget(mono);

        auto* boxLay = new QVBoxLayout(box);
        boxLay->setContentsMargins(8, 8, 8, 8);
        boxLay->addWidget(body);
        body->setEnabled(box->isChecked());
        connect(box, &QGroupBox::toggled, this, [this, layerKey, body](bool on){
            auto& s = workingFor(layerKey);
            s.enabled[EffectStack::EChMix] = on;
            body->setEnabled(on);
            scheduleApplyToProject(layerKey);
            commitDragSession();
        });
        lay->addWidget(box);
    }

    // 提示
    {
        auto* hint = new QLabel(content);
        hint->setText(QStringLiteral("说明: 此处可调整 色相/饱和度、亮度/对比度、曲线、通道混合器.\n"
                                     "其他效果 (颜色平衡 / 照片滤镜 / 自然饱和度) 保留原值, 保存时不会丢失."));
        hint->setStyleSheet("color:#888;padding:6px 2px;");
        hint->setWordWrap(true);
        lay->addWidget(hint);
    }

    lay->addStretch(1);
    scroll->setWidget(content);
    outer->addWidget(scroll);
    return page;
}

EffectStack& SchemeRefineDialog::workingFor(const QString& layerKey)
{
    return m_workingEffects[layerKey];
}

void SchemeRefineDialog::scheduleApplyToProject(const QString& layerKey)
{
    // 第一次发生值变化 → 启动"拖动会话", 记录起点快照 (供 commit 时入 undo 栈)
    if (!m_sessionActive) {
        auto& proj = ProjectController::instance().project();
        if (m_schemeIdx >= 0 && m_schemeIdx < proj.schemes.size()) {
            const Scheme& sc = proj.schemes[m_schemeIdx];
            m_sessionStartSnapshot.clear();
            for (auto it = sc.layerEffects.constBegin(); it != sc.layerEffects.constEnd(); ++it) {
                m_sessionStartSnapshot.insert(it.key(), it.value());
            }
        }
        // UI 上下文: Ctrl+Z 时把"被撤销操作发生时所处的层 + 各层曲线通道"也还原
        m_sessionStartLayerKey = m_list ? m_layerKeys.value(m_list->currentRow()) : QString();
        m_sessionStartCurveCh  = m_layerCurveCh;
        m_sessionActive = true;
    }

    m_dirtyLayers.insert(layerKey);
    if (!m_applyDebounce.isActive()) m_applyDebounce.start();
}

void SchemeRefineDialog::flushPendingToProject()
{
    auto& ctl = ProjectController::instance();
    auto& proj = ctl.projectMut();
    if (m_schemeIdx < 0 || m_schemeIdx >= proj.schemes.size()) {
        m_dirtyLayers.clear();
        return;
    }
    Scheme& sc = proj.schemes[m_schemeIdx];
    if (sc.isBuiltin || sc.isBaked) {       // 不应到这一步, 防御
        m_dirtyLayers.clear();
        return;
    }

    // 注意: 这里不 push undo, 单次拖动会有 N 个 flush, undo 由 commitDragSession 统一做.
    for (const QString& k : m_dirtyLayers) {
        auto it = m_workingEffects.find(k);
        if (it == m_workingEffects.end()) continue;
        sc.layerEffects[k] = it.value();
    }
    m_dirtyLayers.clear();
    // 实时预览: 只发 effectsChanged (不标 dirty, 不动缩略图), 等"保存"才正式落地.
    ctl.emitPreviewRefresh();
}

void SchemeRefineDialog::commitDragSession()
{
    if (!m_sessionActive) return;

    // 把会话最后一次 pending 立即同步到 project (确保下次比较一致)
    if (m_applyDebounce.isActive()) m_applyDebounce.stop();
    if (!m_dirtyLayers.isEmpty()) flushPendingToProject();

    // 只在确实"改变了状态"时入栈, 避免点滑块没动也产生空 undo
    auto& proj = ProjectController::instance().project();
    if (m_schemeIdx >= 0 && m_schemeIdx < proj.schemes.size()) {
        const Scheme& sc = proj.schemes[m_schemeIdx];
        bool changed = false;
        for (auto it = m_sessionStartSnapshot.constBegin();
             it != m_sessionStartSnapshot.constEnd() && !changed; ++it) {
            auto cur = sc.layerEffects.find(it.key());
            if (cur == sc.layerEffects.end()) { changed = true; break; }
            // EffectStack 没重载 operator==, 这里做"是否相等"的廉价比较: 序列化几字段对比
            const EffectStack& a = it.value();
            const EffectStack& b = cur.value();
            if (a.hsl.hue != b.hsl.hue || a.hsl.saturation != b.hsl.saturation
             || a.hsl.lightness != b.hsl.lightness
             || a.brtCtr.brightness != b.brtCtr.brightness
             || a.brtCtr.contrast != b.brtCtr.contrast
             || a.enabled[EffectStack::EHsl]    != b.enabled[EffectStack::EHsl]
             || a.enabled[EffectStack::EBrtCtr] != b.enabled[EffectStack::EBrtCtr]
             || a.enabled[EffectStack::ECurves] != b.enabled[EffectStack::ECurves]
             || a.enabled[EffectStack::EChMix]  != b.enabled[EffectStack::EChMix]
             // ChMix 12 个权重 + monochrome
             || a.chMix.rr != b.chMix.rr || a.chMix.rg != b.chMix.rg || a.chMix.rb != b.chMix.rb
             || a.chMix.gr != b.chMix.gr || a.chMix.gg != b.chMix.gg || a.chMix.gb != b.chMix.gb
             || a.chMix.br != b.chMix.br || a.chMix.bg != b.chMix.bg || a.chMix.bb != b.chMix.bb
             || a.chMix.r_const != b.chMix.r_const
             || a.chMix.g_const != b.chMix.g_const
             || a.chMix.b_const != b.chMix.b_const
             || a.chMix.monochrome != b.chMix.monochrome
             // Curves 4 通道控制点
             || a.curves.master != b.curves.master
             || a.curves.r != b.curves.r
             || a.curves.g != b.curves.g
             || a.curves.b != b.curves.b) {
                changed = true;
            }
        }
        if (changed) {
            LocalUndoStep step;
            step.effects        = std::move(m_sessionStartSnapshot);
            step.activeLayerKey = m_sessionStartLayerKey;
            step.curveChannel   = m_sessionStartCurveCh;
            m_localUndo.push_back(std::move(step));
            while (m_localUndo.size() > kLocalUndoLimit) m_localUndo.removeFirst();
            m_localRedo.clear();
        }
    }
    m_sessionStartSnapshot.clear();
    m_sessionStartLayerKey.clear();
    m_sessionStartCurveCh.clear();
    m_sessionActive = false;
}

void SchemeRefineDialog::onLayerChanged(int row)
{
    if (!m_stack) return;
    if (row < 0 || row >= m_stack->count()) return;
    m_stack->setCurrentIndex(row);
}

void SchemeRefineDialog::rebuildCurrentLayerPanel()
{
    if (!m_list || !m_stack) return;
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_layerKeys.size()) return;
    const QString k = m_layerKeys[row];
    auto* old = m_stack->widget(row);
    m_stack->removeWidget(old);
    if (old) old->deleteLater();
    QWidget* page = buildEffectPanel(k);
    m_stack->insertWidget(row, page);
    m_stack->setCurrentIndex(row);
}

void SchemeRefineDialog::localUndo()
{
    if (m_localUndo.isEmpty()) return;
    auto& ctl = ProjectController::instance();
    auto& proj = ctl.projectMut();
    if (m_schemeIdx < 0 || m_schemeIdx >= proj.schemes.size()) return;
    Scheme& sc = proj.schemes[m_schemeIdx];
    if (sc.isBuiltin || sc.isBaked) return;

    // pending 先提交一次 (会顺带合并到栈 → 用户拖到一半 Ctrl+Z 时, 拖到的中间值不会丢)
    commitDragSession();

    // 当前 → redo 栈 (含当前 UI 上下文)
    LocalUndoStep cur;
    for (auto it = sc.layerEffects.constBegin(); it != sc.layerEffects.constEnd(); ++it) {
        cur.effects.insert(it.key(), it.value());
    }
    cur.activeLayerKey = m_list ? m_layerKeys.value(m_list->currentRow()) : QString();
    cur.curveChannel   = m_layerCurveCh;
    m_localRedo.push_back(std::move(cur));
    while (m_localRedo.size() > kLocalUndoLimit) m_localRedo.removeFirst();

    // 上一帧 ← undo 栈, 写回 working + project + UI 上下文
    LocalUndoStep prev = m_localUndo.takeLast();
    for (auto it = prev.effects.constBegin(); it != prev.effects.constEnd(); ++it) {
        m_workingEffects[it.key()] = it.value();
        sc.layerEffects[it.key()]  = it.value();
    }
    m_layerCurveCh = prev.curveChannel;

    // 切回到被撤销操作所处的层
    if (m_list && !prev.activeLayerKey.isEmpty()) {
        const int row = m_layerKeys.indexOf(prev.activeLayerKey);
        if (row >= 0 && row != m_list->currentRow()) {
            QSignalBlocker bl(m_list);
            m_list->setCurrentRow(row);
            m_stack->setCurrentIndex(row);
        }
    }
    ctl.emitPreviewRefresh();
    rebuildCurrentLayerPanel();   // panel 内会读 m_layerCurveCh 恢复 combo + CurveEditor 通道
}

void SchemeRefineDialog::localRedo()
{
    if (m_localRedo.isEmpty()) return;
    auto& ctl = ProjectController::instance();
    auto& proj = ctl.projectMut();
    if (m_schemeIdx < 0 || m_schemeIdx >= proj.schemes.size()) return;
    Scheme& sc = proj.schemes[m_schemeIdx];
    if (sc.isBuiltin || sc.isBaked) return;

    commitDragSession();

    LocalUndoStep cur;
    for (auto it = sc.layerEffects.constBegin(); it != sc.layerEffects.constEnd(); ++it) {
        cur.effects.insert(it.key(), it.value());
    }
    cur.activeLayerKey = m_list ? m_layerKeys.value(m_list->currentRow()) : QString();
    cur.curveChannel   = m_layerCurveCh;
    m_localUndo.push_back(std::move(cur));
    while (m_localUndo.size() > kLocalUndoLimit) m_localUndo.removeFirst();

    LocalUndoStep next = m_localRedo.takeLast();
    for (auto it = next.effects.constBegin(); it != next.effects.constEnd(); ++it) {
        m_workingEffects[it.key()] = it.value();
        sc.layerEffects[it.key()]  = it.value();
    }
    m_layerCurveCh = next.curveChannel;

    if (m_list && !next.activeLayerKey.isEmpty()) {
        const int row = m_layerKeys.indexOf(next.activeLayerKey);
        if (row >= 0 && row != m_list->currentRow()) {
            QSignalBlocker bl(m_list);
            m_list->setCurrentRow(row);
            m_stack->setCurrentIndex(row);
        }
    }
    ctl.emitPreviewRefresh();
    rebuildCurrentLayerPanel();
}

void SchemeRefineDialog::onResetCurrentLayer()
{
    const int row = m_list ? m_list->currentRow() : -1;
    if (row < 0 || row >= m_layerKeys.size()) return;
    const QString k = m_layerKeys[row];

    auto it = m_snapshotForReset.find(k);
    EffectStack snap = (it == m_snapshotForReset.end()) ? EffectStack{} : it.value();

    auto& cur = m_workingEffects[k];
    cur.hsl    = snap.hsl;
    cur.brtCtr = snap.brtCtr;
    cur.curves = snap.curves;
    cur.chMix  = snap.chMix;
    cur.enabled[EffectStack::EHsl]    = snap.enabled[EffectStack::EHsl];
    cur.enabled[EffectStack::EBrtCtr] = snap.enabled[EffectStack::EBrtCtr];
    cur.enabled[EffectStack::ECurves] = snap.enabled[EffectStack::ECurves];
    cur.enabled[EffectStack::EChMix]  = snap.enabled[EffectStack::EChMix];

    // 重建当前层 panel 反映回退后的值
    auto* old = m_stack->widget(row);
    m_stack->removeWidget(old);
    if (old) old->deleteLater();
    QWidget* page = buildEffectPanel(k);
    m_stack->insertWidget(row, page);
    m_stack->setCurrentIndex(row);

    // 立即同步到 Project, 不走 debounce (用户点重置就期望立即看到效果) + 提交 undo 步
    scheduleApplyToProject(k);
    commitDragSession();
}

void SchemeRefineDialog::accept()
{
    // 保存: 把 pending 会话提交 (含未 flush 的 dirty) + 标记 dirty + 触发缩略图重烘
    commitDragSession();

    auto& ctl = ProjectController::instance();
    QString err;
    if (!ctl.applyRefinedLayerEffects(m_schemeIdx, m_workingEffects, &err)) {
        QMessageBox::warning(this, QStringLiteral("保存失败"),
            err.isEmpty() ? QStringLiteral("未知错误") : err);
        return;
    }
    QDialog::accept();
}

void SchemeRefineDialog::reject()
{
    // 取消: 把 snapshot 直接写回 Project, 整体回滚预览改动
    if (m_applyDebounce.isActive()) m_applyDebounce.stop();
    m_dirtyLayers.clear();
    m_sessionActive = false;
    m_sessionStartSnapshot.clear();

    auto& ctl = ProjectController::instance();
    auto& proj = ctl.projectMut();
    if (m_schemeIdx >= 0 && m_schemeIdx < proj.schemes.size()) {
        Scheme& sc = proj.schemes[m_schemeIdx];
        if (!sc.isBuiltin && !sc.isBaked) {
            for (auto it = m_snapshotForReset.constBegin(); it != m_snapshotForReset.constEnd(); ++it) {
                sc.layerEffects[it.key()] = it.value();
            }
            // 回滚: 实时预览快速刷, 不标 dirty (取消等于啥都没发生)
            ctl.emitPreviewRefresh();
        }
    }
    QDialog::reject();
}

} // namespace HighPro
