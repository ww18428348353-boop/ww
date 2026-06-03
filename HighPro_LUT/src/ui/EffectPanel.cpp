#include "EffectPanel.h"
#include "app/ProjectController.h"
#include "core/ColorEffect.h"
#include "widgets/CurveEditor.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QCheckBox>
#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QPushButton>
#include <QComboBox>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTimer>
#include <QMessageBox>
#include <QFrame>

namespace HighPro {

namespace {

// 一行: label + slider + spinbox. 滑块和 spin 双向同步.
class SliderRow : public QWidget
{
public:
    SliderRow(const QString& label, int minv, int maxv, int defv,
              std::function<void(int)> onChange, QWidget* parent = nullptr)
        : QWidget(parent), m_cb(std::move(onChange))
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
        m_slider->setTickPosition(QSlider::NoTicks);
        lay->addWidget(m_slider, 1);

        m_spin = new QSpinBox(this);
        m_spin->setRange(minv, maxv);
        m_spin->setValue(defv);
        m_spin->setFixedWidth(64);
        lay->addWidget(m_spin);

        connect(m_slider, &QSlider::valueChanged, this, [this](int v){
            QSignalBlocker b(m_spin); m_spin->setValue(v);
            if (m_cb) m_cb(v);
        });
        connect(m_spin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int v){
            QSignalBlocker b(m_slider); m_slider->setValue(v);
            if (m_cb) m_cb(v);
        });
    }

    void setValueSilently(int v) {
        QSignalBlocker b1(m_slider), b2(m_spin);
        m_slider->setValue(v);
        m_spin->setValue(v);
    }

private:
    QSlider*  m_slider;
    QSpinBox* m_spin;
    std::function<void(int)> m_cb;
};

// 取当前编辑方案下当前层的 EffectStack (没有则插入). 本体方案返回 nullptr — 不可编辑.
EffectStack* currentStack()
{
    auto& proj = ProjectController::instance().projectMut();
    if (proj.currentLayerKey.isEmpty()) return nullptr;
    auto* sc = proj.currentScheme();
    if (!sc || sc->isBuiltin) return nullptr;
    if (!sc->layerEffects.contains(proj.currentLayerKey))
        sc->layerEffects.insert(proj.currentLayerKey, EffectStack{});
    return &sc->layerEffects[proj.currentLayerKey];
}

void notifyChanged()
{
    ProjectController::instance().notifyEffectsChanged();
}

} // namespace


EffectPanel::EffectPanel(QWidget* parent) : QWidget(parent)
{
    buildUi();
    connect(&ProjectController::instance(), &ProjectController::currentLayerKeyChanged,
            this, &EffectPanel::onCurrentLayerChanged);
    connect(&ProjectController::instance(), &ProjectController::projectLoaded,
            this, &EffectPanel::rebuildForCurrentLayer);
    // M5: 切方案 → 重建面板 (反映新方案的滑块值)
    connect(&ProjectController::instance(), &ProjectController::currentSchemeChanged,
            this, &EffectPanel::rebuildForCurrentLayer);
    rebuildForCurrentLayer();
}

void EffectPanel::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_content = new QWidget(m_scroll);
    m_contentLayout = new QVBoxLayout(m_content);
    m_contentLayout->setContentsMargins(4, 4, 4, 4);
    m_contentLayout->setSpacing(4);
    m_scroll->setWidget(m_content);
    root->addWidget(m_scroll);

    // 底部按钮 (四行, 与截图四完全对齐):
    //   行 0: 智能+随机 (单独一个, 居左)
    //   行 1: 智能 × 4   (当前层 / 所有层 / 可编辑 / 全部)
    //   行 2: 随机 × 4
    //   行 3: 重置 × 4
    auto* row0 = new QHBoxLayout();
    row0->setContentsMargins(6, 6, 6, 0);
    row0->setSpacing(8);

    auto* btnSmartCur     = new QPushButton(QStringLiteral("🎨 智能当前层"), this);
    btnSmartCur->setToolTip(QStringLiteral(
        "当前方案 - 当前层 按 LayerSlot 智能随机.\n"
        "复用当前方案 palette (没有则自动生成), 不动其他层."));
    auto* btnSmartAll     = new QPushButton(QStringLiteral("🎨 智能所有层"), this);
    btnSmartAll->setToolTip(QStringLiteral(
        "当前方案换一套完整 palette (色相统一), 所有可见非肤色层按 slot 重算."));
    auto* btnSmartEditAll = new QPushButton(QStringLiteral("🎨 智能可编辑"), this);
    btnSmartEditAll->setToolTip(QStringLiteral(
        "所有可编辑方案 (排除本体/已烘焙) 按 idx 取 27 风格槽生成 palette, 各层按 slot 重算."));
    auto* btnSmartEvery   = new QPushButton(QStringLiteral("🎨 智能全部 "), this);
    btnSmartEvery->setToolTip(QStringLiteral(
        "所有非本体方案 (含已烘焙) 全部智能随机.\n"
        "注意: 已烘焙方案会降级为可编辑, 原 add_lut PNG 引用会丢失"));
    // P0 v12: 智能+随机 混合按钮 (效果控件 50% 智能 + 50% 旧随机插值)
    auto* btnMixEvery     = new QPushButton(QStringLiteral("🎨🎲 智能+随机"), this);
    btnMixEvery->setToolTip(QStringLiteral(
        "所有非本体方案 (含已烘焙) 每个效果控件参数取智能值 × 50% + 旧随机值 × 50%.\n"
        "得到介于「智能」和「旧随机」之间的中间态.\n"
        "注意: 已烘焙方案会降级为可编辑, 原 add_lut PNG 引用会丢失"));

    // 智能按钮蓝色边样式区分
    const QString kSmartStyle = QStringLiteral(
        "QPushButton { border: 1px solid #4a8fcf; background: #2d3e52; }"
        "QPushButton:hover { background: #3a5070; }");
    // 混合按钮紫色边
    const QString kMixStyle = QStringLiteral(
        "QPushButton { border: 1px solid #a060c0; background: #3d2a48; }"
        "QPushButton:hover { background: #4f3860; }");

    // 全部按钮统一 95×30 (整齐排列 + 同间距, 与设计稿对齐)
    const QSize kBtnSize(95, 30);
    for (auto* b : { btnSmartCur, btnSmartAll, btnSmartEditAll, btnSmartEvery }) {
        b->setFixedSize(kBtnSize);
        b->setStyleSheet(kSmartStyle);
    }
    btnMixEvery->setFixedSize(kBtnSize);
    btnMixEvery->setStyleSheet(kMixStyle);

    // 第 0 行: 智能 + 智能+随机. 与下面两行统一: 6 个槽位 (空+智能×4 + 智能+随机),
    //          截图四里第 0 行是单独一个 "智能+随机" 居左.
    row0->addWidget(btnMixEvery);
    row0->addStretch(1);

    // 第 1 行: 智能 × 4 (当前层 / 所有层 / 可编辑 / 全部)
    auto* rowSmart = new QHBoxLayout();
    rowSmart->setContentsMargins(6, 6, 6, 0);
    rowSmart->setSpacing(8);
    rowSmart->addWidget(btnSmartCur);
    rowSmart->addWidget(btnSmartAll);
    rowSmart->addWidget(btnSmartEditAll);
    rowSmart->addWidget(btnSmartEvery);
    rowSmart->addStretch(1);

    // 第 2 行: 随机 × 4
    auto* row1 = new QHBoxLayout();
    row1->setContentsMargins(6, 6, 6, 0);
    row1->setSpacing(8);

    auto* btnRandomCur     = new QPushButton(QStringLiteral("🎲 随机当前层"), this);
    btnRandomCur->setToolTip(QStringLiteral("当前方案 - 当前层 应用一组写实风随机效果"));
    auto* btnRandomAll     = new QPushButton(QStringLiteral("🎲 随机所有层"), this);
    btnRandomAll->setToolTip(QStringLiteral("当前方案下所有可见层 (除肤色保护层) 各自独立随机"));
    auto* btnRandomEditAll = new QPushButton(QStringLiteral("🎲 随机可编辑"), this);
    btnRandomEditAll->setToolTip(QStringLiteral("所有可编辑方案 (排除本体和已烘焙) 的所有层全部独立随机"));
    auto* btnRandomEvery   = new QPushButton(QStringLiteral("🎲 随机全部 "), this);
    btnRandomEvery->setToolTip(QStringLiteral("所有非本体方案 (含已烘焙) 全部独立随机.\n注意: 已烘焙方案会降级为可编辑, 原 add_lut PNG 引用会丢失"));

    auto* row2 = new QHBoxLayout();
    row2->setContentsMargins(6, 4, 6, 6);
    row2->setSpacing(8);

    auto* btnReset    = new QPushButton(QStringLiteral("↺ 重置当前层"), this);
    auto* btnResetAll = new QPushButton(QStringLiteral("↺ 重置所有层"), this);
    btnResetAll->setToolTip(QStringLiteral("把当前方案下所有层的 7 个效果重置回默认 (本体)"));
    auto* btnResetEditAll = new QPushButton(QStringLiteral("↺ 重置可编辑"), this);
    btnResetEditAll->setToolTip(QStringLiteral("所有可编辑方案 (排除本体和已烘焙) 的所有层全部重置"));
    auto* btnResetEvery   = new QPushButton(QStringLiteral("↺ 重置全部 "), this);
    btnResetEvery->setToolTip(QStringLiteral("所有非本体方案 (含已烘焙) 全部重置.\n注意: 已烘焙方案会降级为可编辑, 原 add_lut PNG 引用会丢失"));
    auto* btnCopyAll  = new QPushButton(QStringLiteral("📋 复制到所有层"), this);

    for (auto* b : { btnRandomCur, btnRandomAll, btnRandomEditAll, btnRandomEvery,
                     btnReset, btnResetAll, btnResetEditAll, btnResetEvery, btnCopyAll }) {
        b->setFixedSize(kBtnSize);
    }

    row1->addWidget(btnRandomCur);
    row1->addWidget(btnRandomAll);
    row1->addWidget(btnRandomEditAll);
    row1->addWidget(btnRandomEvery);
    row1->addStretch(1);

    row2->addWidget(btnReset);
    row2->addWidget(btnResetAll);
    row2->addWidget(btnResetEditAll);
    row2->addWidget(btnResetEvery);
    // "复制到所有层" 按钮按需求隐藏 (代码保留供后续复用)
    btnCopyAll->hide();
    row2->addStretch(1);

    // 按钮容器: 整块区域加大 ~30% 高度, 留更多视觉空间
    //   按钮自身仍 28px, 容器高度 ≈ 28*2 + spacing + padding ≈ 86px
    auto* btnFrame = new QFrame(this);
    btnFrame->setObjectName("EffectBtnBar");
    btnFrame->setStyleSheet(
        "QFrame#EffectBtnBar{ background:#2a2a2a; border-top:1px solid #3a3a3a; }");
    auto* btnLay = new QVBoxLayout(btnFrame);
    btnLay->setContentsMargins(2, 8, 2, 8);   // 上下 8px 留白 → 比之前的 4px 多 30%
    btnLay->setSpacing(6);
    btnLay->addLayout(row0);            // 智能+随机 (单独 1 个)
    btnLay->addLayout(rowSmart);        // 智能 × 4
    btnLay->addLayout(row1);            // 随机 × 4
    btnLay->addLayout(row2);            // 重置 × 4
    btnFrame->setMinimumHeight(160);    // 4 行 × 30 + 行间距 6 + padding ≈ 160

    root->addWidget(btnFrame);

    // P0 智能随机 (新) 接 smart* 方法
    connect(btnSmartCur, &QPushButton::clicked, this, [this]{
        ProjectController::instance().smartRandomizeCurrentLayer();
        rebuildForCurrentLayer();
    });
    connect(btnSmartAll, &QPushButton::clicked, this, [this]{
        ProjectController::instance().smartRandomizeAllLayers();
        rebuildForCurrentLayer();
    });
    connect(btnSmartEditAll, &QPushButton::clicked, this, [this]{
        ProjectController::instance().smartRandomizeAllSchemes(false);
        rebuildForCurrentLayer();
    });
    connect(btnSmartEvery, &QPushButton::clicked, this, [this]{
        // "智能全部" 会让已烘焙方案丢失原 LUT, 给个 1 次确认
        auto& proj = ProjectController::instance().project();
        int bakedN = 0;
        for (int i = 1; i < proj.schemes.size(); ++i) {
            if (proj.schemes[i].isBaked) ++bakedN;
        }
        if (bakedN > 0) {
            auto rc = QMessageBox::question(this, QStringLiteral("智能全部"),
                QStringLiteral("此操作会把 %1 个已烘焙方案降级为可编辑, 原 add_lut PNG 引用会丢失.\n继续?").arg(bakedN),
                QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel);
            if (rc != QMessageBox::Ok) return;
        }
        ProjectController::instance().smartRandomizeAllSchemes(true);
        rebuildForCurrentLayer();
    });
    connect(btnMixEvery, &QPushButton::clicked, this, [this]{
        // 智能+随机 混合: 同样会让已烘焙降级
        auto& proj = ProjectController::instance().project();
        int bakedN = 0;
        for (int i = 1; i < proj.schemes.size(); ++i) {
            if (proj.schemes[i].isBaked) ++bakedN;
        }
        if (bakedN > 0) {
            auto rc = QMessageBox::question(this, QStringLiteral("智能+随机"),
                QStringLiteral("此操作会把 %1 个已烘焙方案降级为可编辑, 原 add_lut PNG 引用会丢失.\n继续?").arg(bakedN),
                QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel);
            if (rc != QMessageBox::Ok) return;
        }
        ProjectController::instance().mixRandomizeAllSchemes(true);
        rebuildForCurrentLayer();
    });

    connect(btnRandomCur, &QPushButton::clicked, this, [this]{
        ProjectController::instance().randomizeCurrentLayer();
        rebuildForCurrentLayer();
    });
    connect(btnRandomAll, &QPushButton::clicked, this, [this]{
        ProjectController::instance().randomizeAllLayers(false);
        rebuildForCurrentLayer();
    });
    connect(btnRandomEditAll, &QPushButton::clicked, this, [this]{
        ProjectController::instance().randomizeAllSchemes(false);
        rebuildForCurrentLayer();
    });
    connect(btnRandomEvery, &QPushButton::clicked, this, [this]{
        // "随机全部"会让已烘焙方案丢失原 LUT, 给个 1 次确认
        auto& proj = ProjectController::instance().project();
        int bakedN = 0;
        for (int i = 1; i < proj.schemes.size(); ++i) {
            if (proj.schemes[i].isBaked) ++bakedN;
        }
        if (bakedN > 0) {
            auto rc = QMessageBox::question(this, QStringLiteral("随机全部"),
                QStringLiteral("此操作会把 %1 个已烘焙方案降级为可编辑, 原 add_lut PNG 引用会丢失.\n继续?").arg(bakedN),
                QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel);
            if (rc != QMessageBox::Ok) return;
        }
        ProjectController::instance().randomizeAllSchemes(true);
        rebuildForCurrentLayer();
    });
    connect(btnReset, &QPushButton::clicked, this, []{
        ProjectController::instance().resetCurrentLayerEffects();
    });
    connect(btnResetAll, &QPushButton::clicked, this, []{
        ProjectController::instance().resetAllLayerEffects();
    });
    connect(btnResetEditAll, &QPushButton::clicked, this, [this]{
        ProjectController::instance().resetAllSchemesEffects(false);
        rebuildForCurrentLayer();
    });
    connect(btnResetEvery, &QPushButton::clicked, this, [this]{
        // 已烘焙降级警告
        auto& proj = ProjectController::instance().project();
        int bakedN = 0;
        for (int i = 1; i < proj.schemes.size(); ++i) {
            if (proj.schemes[i].isBaked) ++bakedN;
        }
        if (bakedN > 0) {
            auto rc = QMessageBox::question(this, QStringLiteral("重置全部"),
                QStringLiteral("此操作会把 %1 个已烘焙方案降级为可编辑并清空效果, 原 add_lut PNG 引用会丢失.\n继续?").arg(bakedN),
                QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel);
            if (rc != QMessageBox::Ok) return;
        }
        ProjectController::instance().resetAllSchemesEffects(true);
        rebuildForCurrentLayer();
    });
    connect(btnCopyAll, &QPushButton::clicked, this, []{
        ProjectController::instance().copyCurrentLayerEffectsToAll();
    });
}

QWidget* EffectPanel::buildSection(const QString& title, int idx, QWidget* body)
{
    auto* box = new QGroupBox(title, m_content);
    box->setCheckable(true);

    EffectStack* st = currentStack();
    box->setChecked(st && st->enabled[idx]);

    auto* lay = new QVBoxLayout(box);
    lay->setContentsMargins(8, 8, 8, 8);
    lay->setSpacing(2);
    lay->addWidget(body);

    body->setEnabled(box->isChecked());

    connect(box, &QGroupBox::toggled, this, [this, idx, body](bool on){
        if (auto* s = currentStack()) {
            s->enabled[idx] = on;
            body->setEnabled(on);
            notifyChanged();
        }
    });
    return box;
}

void EffectPanel::onCurrentLayerChanged()
{
    rebuildForCurrentLayer();
}

void EffectPanel::rebuildForCurrentLayer()
{
    // 清空旧内容
    while (auto* item = m_contentLayout->takeAt(0)) {
        if (auto* w = item->widget()) w->deleteLater();
        delete item;
    }
    m_sections.clear();

    auto& proj = ProjectController::instance().project();
    if (proj.currentLayerKey.isEmpty()) {
        auto* hint = new QLabel(QStringLiteral("(未选择层)"), m_content);
        hint->setStyleSheet("color:#888");
        m_contentLayout->addWidget(hint);
        m_contentLayout->addStretch(1);
        return;
    }
    EffectStack* st = currentStack();
    if (!st) {
        auto& p = ProjectController::instance().project();
        const auto* sc = p.currentScheme();
        QString hint = sc && sc->isBuiltin
            ? QStringLiteral("(本体方案不可编辑, 请在画廊新增/选中其他方案)")
            : QStringLiteral("(无可编辑方案)");
        auto* lbl = new QLabel(hint, m_content);
        lbl->setStyleSheet("color:#888");
        lbl->setWordWrap(true);
        m_contentLayout->addWidget(lbl);
        m_contentLayout->addStretch(1);
        return;
    }

    // === 1. 色相饱和度 ===
    {
        auto* body = new QWidget(m_content);
        auto* l = new QVBoxLayout(body);
        l->setContentsMargins(0, 0, 0, 0); l->setSpacing(2);
        l->addWidget(new SliderRow("色相",   -180, 180, st->hsl.hue,
            [](int v){ if(auto* s=currentStack()){ s->hsl.hue=v; notifyChanged(); } }));
        l->addWidget(new SliderRow("饱和度", -100, 100, st->hsl.saturation,
            [](int v){ if(auto* s=currentStack()){ s->hsl.saturation=v; notifyChanged(); } }));
        l->addWidget(new SliderRow("亮度",   -100, 100, st->hsl.lightness,
            [](int v){ if(auto* s=currentStack()){ s->hsl.lightness=v; notifyChanged(); } }));
        m_contentLayout->addWidget(buildSection(QStringLiteral("1. 色相/饱和度"), EffectStack::EHsl, body));
    }
    // === 2. 亮度对比度 ===
    {
        auto* body = new QWidget(m_content);
        auto* l = new QVBoxLayout(body);
        l->setContentsMargins(0, 0, 0, 0); l->setSpacing(2);
        l->addWidget(new SliderRow("亮度",   -150, 150, st->brtCtr.brightness,
            [](int v){ if(auto* s=currentStack()){ s->brtCtr.brightness=v; notifyChanged(); } }));
        l->addWidget(new SliderRow("对比度", -100, 100, st->brtCtr.contrast,
            [](int v){ if(auto* s=currentStack()){ s->brtCtr.contrast=v; notifyChanged(); } }));
        m_contentLayout->addWidget(buildSection(QStringLiteral("2. 亮度和对比度"), EffectStack::EBrtCtr, body));
    }
    // === 3. 曲线 (M6 完整 UI: 通道选择 + 256×256 自绘) ===
    {
        auto* body = new QWidget(m_content);
        auto* l = new QVBoxLayout(body);
        l->setContentsMargins(0, 0, 0, 0); l->setSpacing(4);

        auto* row = new QHBoxLayout();
        row->setSpacing(6);
        auto* chBox = new QComboBox(body);
        chBox->addItem(QStringLiteral("Master"));
        chBox->addItem(QStringLiteral("R"));
        chBox->addItem(QStringLiteral("G"));
        chBox->addItem(QStringLiteral("B"));
        chBox->setFixedWidth(80);
        row->addWidget(new QLabel(QStringLiteral("通道:"), body));
        row->addWidget(chBox);

        auto* btnReset = new QPushButton(QStringLiteral("重置当前"), body);
        auto* btnResetAll = new QPushButton(QStringLiteral("重置全部"), body);
        btnReset->setFixedWidth(78);
        btnResetAll->setFixedWidth(78);
        row->addWidget(btnReset);
        row->addWidget(btnResetAll);
        row->addStretch(1);
        l->addLayout(row);

        auto* editor = new CurveEditor(body);
        editor->setMinimumHeight(220);
        editor->setCurves(st->curves);
        l->addWidget(editor);

        connect(chBox, qOverload<int>(&QComboBox::currentIndexChanged),
                editor, &CurveEditor::setChannel);
        connect(btnReset, &QPushButton::clicked, editor, &CurveEditor::resetCurrent);
        connect(btnResetAll, &QPushButton::clicked, editor, &CurveEditor::resetAll);

        connect(editor, &CurveEditor::curvesChanged, this, [editor]{
            if (auto* s = currentStack()) {
                s->curves = editor->curves();
                notifyChanged();
            }
        });

        m_contentLayout->addWidget(buildSection(QStringLiteral("3. 曲线"), EffectStack::ECurves, body));
    }
    // === 4. 通道混合器 ===
    {
        auto* body = new QWidget(m_content);
        auto* g = new QGridLayout(body);
        g->setContentsMargins(0, 0, 0, 0); g->setHorizontalSpacing(8); g->setVerticalSpacing(2);

        auto add = [&](int row, const QString& lbl, int* val, std::function<void()> set){
            g->addWidget(new SliderRow(lbl, -200, 200, *val,
                [val,set](int v){ *val = v; set(); }), row, 0, 1, 4);
        };
        // 仅暴露主对角 + 单色复选 (副对角 M6 完整)
        add(0, "红 → 红",  &st->chMix.rr, []{ if(currentStack()) notifyChanged(); });
        add(1, "绿 → 绿",  &st->chMix.gg, []{ if(currentStack()) notifyChanged(); });
        add(2, "蓝 → 蓝",  &st->chMix.bb, []{ if(currentStack()) notifyChanged(); });
        add(3, "红 → 绿",  &st->chMix.rg, []{ if(currentStack()) notifyChanged(); });
        add(4, "红 → 蓝",  &st->chMix.rb, []{ if(currentStack()) notifyChanged(); });
        add(5, "绿 → 红",  &st->chMix.gr, []{ if(currentStack()) notifyChanged(); });
        add(6, "绿 → 蓝",  &st->chMix.gb, []{ if(currentStack()) notifyChanged(); });
        add(7, "蓝 → 红",  &st->chMix.br, []{ if(currentStack()) notifyChanged(); });
        add(8, "蓝 → 绿",  &st->chMix.bg, []{ if(currentStack()) notifyChanged(); });

        auto* mono = new QCheckBox(QStringLiteral("单色"), body);
        mono->setChecked(st->chMix.monochrome);
        connect(mono, &QCheckBox::toggled, this, [](bool on){
            if (auto* s = currentStack()) { s->chMix.monochrome = on; notifyChanged(); }
        });
        g->addWidget(mono, 9, 0);

        m_contentLayout->addWidget(buildSection(QStringLiteral("4. 通道混合器"), EffectStack::EChMix, body));
    }
    // === 5. 颜色平衡 ===
    {
        auto* body = new QWidget(m_content);
        auto* l = new QVBoxLayout(body);
        l->setContentsMargins(0, 0, 0, 0); l->setSpacing(2);

        l->addWidget(new QLabel(QStringLiteral("【阴影】"), body));
        l->addWidget(new SliderRow("青 ↔ 红", -100, 100, st->colorBal.sR,
            [](int v){ if(auto* s=currentStack()){ s->colorBal.sR=v; notifyChanged(); } }));
        l->addWidget(new SliderRow("品 ↔ 绿", -100, 100, st->colorBal.sG,
            [](int v){ if(auto* s=currentStack()){ s->colorBal.sG=v; notifyChanged(); } }));
        l->addWidget(new SliderRow("黄 ↔ 蓝", -100, 100, st->colorBal.sB,
            [](int v){ if(auto* s=currentStack()){ s->colorBal.sB=v; notifyChanged(); } }));
        l->addWidget(new QLabel(QStringLiteral("【中间调】"), body));
        l->addWidget(new SliderRow("青 ↔ 红", -100, 100, st->colorBal.mR,
            [](int v){ if(auto* s=currentStack()){ s->colorBal.mR=v; notifyChanged(); } }));
        l->addWidget(new SliderRow("品 ↔ 绿", -100, 100, st->colorBal.mG,
            [](int v){ if(auto* s=currentStack()){ s->colorBal.mG=v; notifyChanged(); } }));
        l->addWidget(new SliderRow("黄 ↔ 蓝", -100, 100, st->colorBal.mB,
            [](int v){ if(auto* s=currentStack()){ s->colorBal.mB=v; notifyChanged(); } }));
        l->addWidget(new QLabel(QStringLiteral("【高光】"), body));
        l->addWidget(new SliderRow("青 ↔ 红", -100, 100, st->colorBal.hR,
            [](int v){ if(auto* s=currentStack()){ s->colorBal.hR=v; notifyChanged(); } }));
        l->addWidget(new SliderRow("品 ↔ 绿", -100, 100, st->colorBal.hG,
            [](int v){ if(auto* s=currentStack()){ s->colorBal.hG=v; notifyChanged(); } }));
        l->addWidget(new SliderRow("黄 ↔ 蓝", -100, 100, st->colorBal.hB,
            [](int v){ if(auto* s=currentStack()){ s->colorBal.hB=v; notifyChanged(); } }));

        auto* keep = new QCheckBox(QStringLiteral("保持亮度"), body);
        keep->setChecked(st->colorBal.preserveLuma);
        connect(keep, &QCheckBox::toggled, this, [](bool on){
            if (auto* s = currentStack()) { s->colorBal.preserveLuma = on; notifyChanged(); }
        });
        l->addWidget(keep);

        m_contentLayout->addWidget(buildSection(QStringLiteral("5. 颜色平衡"), EffectStack::EColorBal, body));
    }
    // === 6. 照片滤镜 ===
    {
        auto* body = new QWidget(m_content);
        auto* l = new QVBoxLayout(body);
        l->setContentsMargins(0, 0, 0, 0); l->setSpacing(2);

        auto* row1 = new QHBoxLayout();
        row1->addWidget(new QLabel(QStringLiteral("滤镜:"), body));
        auto* combo = new QComboBox(body);
        for (int i = 0; i < kPhotoFilterPresetCount; ++i)
            combo->addItem(QString::fromUtf8(kPhotoFilterPresets[i].name));
        combo->setCurrentIndex(qBound(0, st->photoFilter.preset, kPhotoFilterPresetCount-1));
        connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this, [](int i){
            if (auto* s = currentStack()) {
                if (i >= 0 && i < kPhotoFilterPresetCount) {
                    s->photoFilter.preset  = i;
                    s->photoFilter.filterR = kPhotoFilterPresets[i].r;
                    s->photoFilter.filterG = kPhotoFilterPresets[i].g;
                    s->photoFilter.filterB = kPhotoFilterPresets[i].b;
                    notifyChanged();
                }
            }
        });
        row1->addWidget(combo, 1);
        l->addLayout(row1);

        l->addWidget(new SliderRow("浓度 %", 0, 100, st->photoFilter.density,
            [](int v){ if(auto* s=currentStack()){ s->photoFilter.density=v; notifyChanged(); } }));

        auto* keep = new QCheckBox(QStringLiteral("保持亮度"), body);
        keep->setChecked(st->photoFilter.preserveLuma);
        connect(keep, &QCheckBox::toggled, this, [](bool on){
            if (auto* s = currentStack()) { s->photoFilter.preserveLuma = on; notifyChanged(); }
        });
        l->addWidget(keep);

        m_contentLayout->addWidget(buildSection(QStringLiteral("6. 照片滤镜"), EffectStack::EPhotoFilter, body));
    }
    // === 7. 自然饱和度 ===
    {
        auto* body = new QWidget(m_content);
        auto* l = new QVBoxLayout(body);
        l->setContentsMargins(0, 0, 0, 0); l->setSpacing(2);
        l->addWidget(new SliderRow("自然饱和度", -100, 100, st->vibrance.vibrance,
            [](int v){ if(auto* s=currentStack()){ s->vibrance.vibrance=v; notifyChanged(); } }));
        l->addWidget(new SliderRow("饱和度",     -100, 100, st->vibrance.saturation,
            [](int v){ if(auto* s=currentStack()){ s->vibrance.saturation=v; notifyChanged(); } }));
        m_contentLayout->addWidget(buildSection(QStringLiteral("7. 自然饱和度"), EffectStack::EVibrance, body));
    }

    // === 影子保护 (M7, 不在 7 效果开关之列, 一直生效) ===
    {
        auto* gb = new QGroupBox(QStringLiteral("影子保护"), m_content);
        gb->setCheckable(false);
        gb->setToolTip(QStringLiteral("亮度低于阈值的像素 → 软过渡保留本体, 防止暗部偏色"));
        auto* l = new QVBoxLayout(gb);
        l->setContentsMargins(8, 8, 8, 8); l->setSpacing(2);
        l->addWidget(new SliderRow("阈值 (0..32)", 0, 32, st->shadowProtectThreshold,
            [](int v){ if(auto* s=currentStack()){ s->shadowProtectThreshold = v; notifyChanged(); } }));
        m_contentLayout->addWidget(gb);
    }

    m_contentLayout->addStretch(1);
}

} // namespace HighPro
