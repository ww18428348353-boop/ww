#include "LayerTreePanel.h"
#include "app/ProjectController.h"

#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QSignalBlocker>
#include <QMouseEvent>
#include <QSet>
#include <QMenu>
#include <QAction>

namespace HighPro {

namespace {
constexpr int RoleLayerKey   = Qt::UserRole + 1;
constexpr int RoleIsAddonSub = Qt::UserRole + 2;
constexpr int RoleIsAddonRoot= Qt::UserRole + 3;
}

// AE 风格拖扫显隐: 在 QTreeWidget viewport 上按下左键,
// 根据起点行原状态决定 mode (toggle off/on), 鼠标移过的每行只 toggle 一次.
class TreeDragToggleFilter : public QObject
{
public:
    TreeDragToggleFilter(QTreeWidget* tree, LayerTreePanel* panel)
        : QObject(tree), m_tree(tree), m_panel(panel) {}

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override
    {
        if (obj != m_tree->viewport()) return false;

        switch (ev->type()) {
        case QEvent::MouseButtonPress: {
            auto* me = static_cast<QMouseEvent*>(ev);
            if (me->button() != Qt::LeftButton) break;

            auto* it = m_tree->itemAt(me->pos());
            if (!it) break;
            if (!(it->flags() & Qt::ItemIsUserCheckable)) break;

            // 仅在指示器 (checkbox) 区域内触发拖扫. 该判定: x 坐标 < 树缩进 + 指示器宽
            const QRect rect = m_tree->visualItemRect(it);
            // 不强制限制点击在指示器, 让用户随便点中行也能拖
            // 但忽略 expander 三角的左边距
            (void)rect;

            // 起点决定 mode: 起点是否要变成 checked
            const bool nowChecked = (it->checkState(0) == Qt::Checked);
            m_dragging  = true;
            m_targetState = nowChecked ? Qt::Unchecked : Qt::Checked;
            m_visited.clear();

            // 直接 toggle 起点
            it->setCheckState(0, m_targetState);
            m_visited.insert(it);
            // 让 Qt 也别再独立处理一遍 click → 防 double toggle
            return true;
        }
        case QEvent::MouseMove: {
            if (!m_dragging) break;
            auto* me = static_cast<QMouseEvent*>(ev);
            if (!(me->buttons() & Qt::LeftButton)) break;

            auto* it = m_tree->itemAt(me->pos());
            if (!it) break;
            if (m_visited.contains(it)) break;
            if (!(it->flags() & Qt::ItemIsUserCheckable)) break;

            it->setCheckState(0, m_targetState);
            m_visited.insert(it);
            return true;
        }
        case QEvent::MouseButtonRelease: {
            if (!m_dragging) break;
            m_dragging = false;
            m_visited.clear();
            return true;
        }
        default: break;
        }
        return false;
    }

private:
    QTreeWidget*    m_tree;
    LayerTreePanel* m_panel;
    bool            m_dragging = false;
    Qt::CheckState  m_targetState = Qt::Unchecked;
    QSet<QTreeWidgetItem*> m_visited;
};


LayerTreePanel::LayerTreePanel(QWidget* parent) : QWidget(parent)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(2, 2, 2, 2);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabel(QStringLiteral("层级"));
    m_tree->setColumnCount(1);
    m_tree->header()->setStretchLastSection(true);
    m_tree->setRootIsDecorated(true);
    m_tree->setExpandsOnDoubleClick(true);
    m_tree->setAlternatingRowColors(true);
    m_tree->setStyleSheet(
        "QTreeWidget { font-size: 13pt; }"
        "QTreeWidget::item { padding: 4px 2px; min-height: 28px; }"
        "QTreeWidget::indicator { width: 22px; height: 22px; }"
    );
    lay->addWidget(m_tree);

    // 安装 AE 风格拖扫 filter
    m_tree->viewport()->installEventFilter(new TreeDragToggleFilter(m_tree, this));

    connect(m_tree, &QTreeWidget::itemChanged,  this, &LayerTreePanel::onItemChanged);
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &LayerTreePanel::onItemSelected);

    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tree, &QTreeWidget::customContextMenuRequested,
            this, &LayerTreePanel::onContextMenu);

    auto& ctl = ProjectController::instance();
    connect(&ctl, &ProjectController::projectLoaded,
            this, &LayerTreePanel::refresh);
    connect(&ctl, &ProjectController::visibilityChanged,
            this, &LayerTreePanel::syncCheckStates);
}

void LayerTreePanel::refresh()
{
    QSignalBlocker bl(m_tree);
    m_suppressSignal = true;
    m_tree->clear();

    const auto& proj = ProjectController::instance().project();
    if (proj.layers.isEmpty()) {
        auto* p = new QTreeWidgetItem(m_tree);
        p->setText(0, QStringLiteral("(请通过 [文件] -> [打开源目录] 加载资源)"));
        p->setFlags(Qt::NoItemFlags);
        m_suppressSignal = false;
        return;
    }

    // === UI 显示顺序: addon → numbered (高→低) → body ===
    QVector<const LayerData*> addonList;
    QVector<const LayerData*> numberedList;
    const LayerData* bodyLayer = nullptr;

    for (const auto& l : proj.layers) {
        if (l.kind == LayerKind::Addon) addonList.push_back(&l);
        else if (l.kind == LayerKind::Numbered) numberedList.push_back(&l);
        else if (l.kind == LayerKind::Body)     bodyLayer = &l;
    }

    std::sort(numberedList.begin(), numberedList.end(),
              [](const LayerData* a, const LayerData* b) {
                  return a->numberedIdx > b->numberedIdx;
              });

    // 1) addon 多选 (与 numbered/body 一致). 起初仅显示 currentAddonKey 那一项.
    if (!addonList.isEmpty()) {
        auto* root = new QTreeWidgetItem(m_tree);
        root->setText(0, QStringLiteral("addon"));
        root->setFlags(root->flags() & ~Qt::ItemIsUserCheckable);
        root->setData(0, RoleIsAddonRoot, true);
        root->setExpanded(true);

        for (const auto* l : addonList) {
            auto* it = new QTreeWidgetItem(root);
            it->setText(0, l->displayName.section('/', 1));
            it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
            const bool visible = !proj.hiddenLayerKeys.contains(l->key());
            it->setCheckState(0, visible ? Qt::Checked : Qt::Unchecked);
            it->setData(0, RoleLayerKey, l->key());
            it->setData(0, RoleIsAddonSub, true);
            it->setToolTip(0, l->rootDir);
        }
    }

    // 2) numbered 倒序
    for (const auto* l : numberedList) {
        auto* it = new QTreeWidgetItem(m_tree);
        it->setText(0, l->displayName);
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        const bool visible = !proj.hiddenLayerKeys.contains(l->key());
        it->setCheckState(0, visible ? Qt::Checked : Qt::Unchecked);
        it->setData(0, RoleLayerKey, l->key());
        it->setData(0, RoleIsAddonSub, false);
        it->setToolTip(0, QString("%1\n动作: %2")
                       .arg(l->rootDir).arg(l->actionNames().join(", ")));
    }

    // 3) body
    if (bodyLayer) {
        auto* it = new QTreeWidgetItem(m_tree);
        it->setText(0, bodyLayer->displayName);
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        const bool visible = !proj.hiddenLayerKeys.contains(bodyLayer->key());
        it->setCheckState(0, visible ? Qt::Checked : Qt::Unchecked);
        it->setData(0, RoleLayerKey, bodyLayer->key());
        it->setData(0, RoleIsAddonSub, false);
        it->setToolTip(0, QString("%1\n动作: %2")
                       .arg(bodyLayer->rootDir).arg(bodyLayer->actionNames().join(", ")));
    }

    m_tree->expandAll();
    m_suppressSignal = false;

    // 同步肤色显示 (右键菜单标记的)
    syncCheckStates();
}

void LayerTreePanel::syncCheckStates()
{
    if (!m_tree) return;
    QSignalBlocker bl(m_tree);
    m_suppressSignal = true;

    const auto& proj = ProjectController::instance().project();

    auto applyOne = [&](QTreeWidgetItem* node) {
        const QString k = node->data(0, RoleLayerKey).toString();
        if (k.isEmpty()) return;
        const bool visible = !proj.hiddenLayerKeys.contains(k);
        if ((node->checkState(0) == Qt::Checked) != visible) {
            node->setCheckState(0, visible ? Qt::Checked : Qt::Unchecked);
        }

        // P0: slot emoji 前缀.
        //   1) 先剥掉所有可能的旧 emoji 前缀, 防多次 sync 累加.
        //   2) Skin (含旧 skinSafeLayerKeys + 新 slot=Skin) → 🛡 优先 + 黄字.
        //   3) 其他 slot → 对应 emoji.
        //   4) Unknown 不加前缀, 保持原始 displayName 简洁.
        QString base = stripLayerSlotPrefix(node->text(0));

        // 找到对应 LayerData (为了 slotFor 启发式)
        const LayerData* layer = nullptr;
        for (const auto& l : proj.layers) {
            if (l.key() == k) { layer = &l; break; }
        }

        const bool skin = proj.skinSafeLayerKeys.contains(k);
        LayerSlot slot  = LayerSlot::Unknown;
        if (layer) slot = proj.slotFor(*layer);

        QString newText = base;
        if (skin) {
            newText = layerSlotEmoji(LayerSlot::Skin) + QStringLiteral(" ") + base;
        } else if (slot != LayerSlot::Unknown) {
            newText = layerSlotEmoji(slot) + QStringLiteral(" ") + base;
        }

        if (node->text(0) != newText) node->setText(0, newText);
        node->setForeground(0, skin ? QBrush(QColor(255, 200, 80)) : QBrush());
    };

    const int topN = m_tree->topLevelItemCount();
    for (int i = 0; i < topN; ++i) {
        auto* top = m_tree->topLevelItem(i);
        if (top->data(0, RoleIsAddonRoot).toBool()) {
            const int childN = top->childCount();
            for (int c = 0; c < childN; ++c) applyOne(top->child(c));
            continue;
        }
        applyOne(top);
    }
    m_suppressSignal = false;
}

void LayerTreePanel::onItemChanged(QTreeWidgetItem* it, int)
{
    if (m_suppressSignal || !it) return;
    const QString key = it->data(0, RoleLayerKey).toString();
    if (key.isEmpty()) return;

    // 全部层 (含 addon) 都用 hiddenLayerKeys 控制显隐 (与 numbered/body 一致)
    const bool checked = (it->checkState(0) == Qt::Checked);
    ProjectController::instance().setLayerVisible(key, checked);
}

void LayerTreePanel::onItemSelected()
{
    if (m_suppressSignal) return;
    const auto items = m_tree->selectedItems();
    if (items.isEmpty()) return;
    const QString key = items.first()->data(0, RoleLayerKey).toString();
    if (!key.isEmpty()) emit currentLayerChanged(key);
}

void LayerTreePanel::onContextMenu(const QPoint& pos)
{
    auto* it = m_tree->itemAt(pos);
    if (!it) return;
    const QString key = it->data(0, RoleLayerKey).toString();
    if (key.isEmpty()) return;

    auto& ctl = ProjectController::instance();
    const auto& proj = ctl.project();
    const bool skin = proj.skinSafeLayerKeys.contains(key);
    const int  curN = proj.skinSafeLayerKeys.size();

    // 当前层对应 LayerData (取当前 slot 用于打勾)
    const LayerData* layer = nullptr;
    for (const auto& l : proj.layers) {
        if (l.key() == key) { layer = &l; break; }
    }
    const LayerSlot currentSlot = layer ? proj.slotFor(*layer) : LayerSlot::Unknown;
    const bool      hasManual   = proj.layerSlots.contains(key)
                                  && proj.layerSlots.value(key) != LayerSlot::Unknown;

    QMenu menu(this);

    // --- 1. 旧肤色保护入口 (保留, 跟 slot=Skin 双向同步) ---
    QAction* aSkin = nullptr;
    if (skin) {
        aSkin = menu.addAction(QStringLiteral("取消肤色保护 (当前 %1 层)").arg(curN));
    } else {
        aSkin = menu.addAction(QStringLiteral("标为肤色保护 (当前 %1 层)").arg(curN));
    }
    aSkin->setToolTip(QStringLiteral("肤色层在所有方案下都不参与变色, 永远保持本体"));

    menu.addSeparator();

    // --- 2. P0: 设置层语义 → 子菜单 (9 项) ---
    auto* subSlot = menu.addMenu(QStringLiteral("设置层语义 (智能随机用)"));
    subSlot->setToolTip(QStringLiteral(
        "决定智能随机时该层用哪种参数策略.\n"
        "选 [自动] 走启发式 (body→肤色, num_00→服装, num_01→裙摆 ...).\n"
        "选 [肤色] 等同肤色保护, 任何方案下都不变色."));

    auto addSlotItem = [&](LayerSlot s, bool isAuto) {
        const QString label = isAuto
            ? QStringLiteral("%1 自动 (推断: %2)")
              .arg(layerSlotEmoji(LayerSlot::Unknown))
              .arg(layer ? layerSlotDisplayName(proj.defaultSlotFor(*layer))
                         : layerSlotDisplayName(LayerSlot::Unknown))
            : QStringLiteral("%1 %2")
              .arg(layerSlotEmoji(s))
              .arg(layerSlotDisplayName(s));
        QAction* a = subSlot->addAction(label);
        a->setCheckable(true);
        // 打勾: 自动项 = 当前没手动指定且非 skin; 具体 slot = 已手动指定为该值 (skin 单独处理)
        if (isAuto) {
            a->setChecked(!hasManual && !skin);
        } else if (s == LayerSlot::Skin) {
            a->setChecked(skin);
        } else {
            a->setChecked(hasManual && currentSlot == s && !skin);
        }
        a->setData(static_cast<int>(s));
        return a;
    };

    QAction* aAuto = addSlotItem(LayerSlot::Unknown, true);
    subSlot->addSeparator();
    addSlotItem(LayerSlot::Skin, false);
    addSlotItem(LayerSlot::Hair, false);
    addSlotItem(LayerSlot::Clothing, false);
    addSlotItem(LayerSlot::Skirt, false);
    addSlotItem(LayerSlot::Decor01, false);
    addSlotItem(LayerSlot::Decor02, false);
    addSlotItem(LayerSlot::WeaponMetal, false);
    addSlotItem(LayerSlot::WeaponNonMetal, false);

    QAction* sel = menu.exec(m_tree->viewport()->mapToGlobal(pos));
    if (!sel) return;

    if (sel == aSkin) {
        ctl.setLayerSkinSafe(key, !skin);
        return;
    }

    // slot 子菜单触发
    if (sel == aAuto) {
        ctl.setLayerSlot(key, LayerSlot::Unknown);
        return;
    }
    // 子菜单中其他项 data 存了 LayerSlot int
    if (qobject_cast<QMenu*>(sel->parent()) == subSlot) {
        const int iv = sel->data().toInt();
        ctl.setLayerSlot(key, static_cast<LayerSlot>(iv));
    }
}

} // namespace HighPro
