#include "SchemeRefineDialog.h"
#include "app/ProjectController.h"
#include "core/Project.h"
#include "core/LayerData.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QListWidgetItem>
#include <QStackedWidget>
#include <QLabel>
#include <QPushButton>
#include <QGroupBox>
#include <QSlider>
#include <QSpinBox>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QFrame>
#include <QScrollArea>
#include <QSet>
#include <QShortcut>
#include <QKeySequence>

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
    resize(720, 520);
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
    m_list->setFixedWidth(180);
    m_list->setStyleSheet(
        "QListWidget{background:#262626;color:#ddd;border:1px solid #3a3a3a;}"
        "QListWidget::item{padding:6px 8px;}"
        "QListWidget::item:selected{background:#3b6ea8;color:#fff;}"
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
    for (const auto& l : proj.layers) {
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

    // 提示
    {
        auto* hint = new QLabel(content);
        hint->setText(QStringLiteral("说明: 此处只调整 色相/饱和度 与 亮度/对比度.\n"
                                     "其他效果 (曲线 / 通道混合 / 颜色平衡 / 照片滤镜 / 自然饱和度) 保留原值, 保存时不会丢失."));
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
             || a.enabled[EffectStack::EHsl] != b.enabled[EffectStack::EHsl]
             || a.enabled[EffectStack::EBrtCtr] != b.enabled[EffectStack::EBrtCtr]) {
                changed = true;
            }
        }
        if (changed) {
            m_localUndo.push_back(std::move(m_sessionStartSnapshot));
            while (m_localUndo.size() > kLocalUndoLimit) m_localUndo.removeFirst();
            m_localRedo.clear();
        }
    }
    m_sessionStartSnapshot.clear();
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

    // 当前 → redo 栈
    QHash<QString, EffectStack> cur;
    for (auto it = sc.layerEffects.constBegin(); it != sc.layerEffects.constEnd(); ++it) {
        cur.insert(it.key(), it.value());
    }
    m_localRedo.push_back(std::move(cur));
    while (m_localRedo.size() > kLocalUndoLimit) m_localRedo.removeFirst();

    // 上一帧 ← undo 栈, 写回 working + project
    QHash<QString, EffectStack> prev = m_localUndo.takeLast();
    for (auto it = prev.constBegin(); it != prev.constEnd(); ++it) {
        m_workingEffects[it.key()] = it.value();
        sc.layerEffects[it.key()] = it.value();
    }
    ctl.emitPreviewRefresh();
    rebuildCurrentLayerPanel();
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

    QHash<QString, EffectStack> cur;
    for (auto it = sc.layerEffects.constBegin(); it != sc.layerEffects.constEnd(); ++it) {
        cur.insert(it.key(), it.value());
    }
    m_localUndo.push_back(std::move(cur));
    while (m_localUndo.size() > kLocalUndoLimit) m_localUndo.removeFirst();

    QHash<QString, EffectStack> next = m_localRedo.takeLast();
    for (auto it = next.constBegin(); it != next.constEnd(); ++it) {
        m_workingEffects[it.key()] = it.value();
        sc.layerEffects[it.key()] = it.value();
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
    cur.enabled[EffectStack::EHsl]    = snap.enabled[EffectStack::EHsl];
    cur.enabled[EffectStack::EBrtCtr] = snap.enabled[EffectStack::EBrtCtr];

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
