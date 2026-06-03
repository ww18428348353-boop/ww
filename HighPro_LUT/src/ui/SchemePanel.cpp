#include "SchemePanel.h"
#include "SchemeRefineDialog.h"
#include "app/ProjectController.h"
#include "app/ThumbnailWorker.h"
#include "render/LutBaker.h"
#include "core/PathUtil.h"
#include "core/HaldClut.h"
#include "core/LayerData.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QListWidgetItem>
#include <QSignalBlocker>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QInputDialog>
#include <QMessageBox>
#include <QFile>
#include <QImageReader>
#include <QPainter>
#include <QPushButton>
#include <QApplication>
#include <QContextMenuEvent>
#include <QScrollBar>
#include <QRegularExpression>
#include <QSet>
#include <array>

// stb_image 解 TGA (实现已在 D3D11Texture.cpp 里 STB_IMAGE_IMPLEMENTATION)
extern "C" unsigned char* stbi_load_from_memory(
    const unsigned char* buffer, int len, int* x, int* y, int* channels_in_file, int desired_channels);
extern "C" void stbi_image_free(void* retval_from_stbi_load);

namespace HighPro {

namespace {

// QListWidget 子类: 点击空白处时清空选择 (画廊"无选中方案"); Delete 快捷删除当前
class GalleryList : public QListWidget
{
public:
    using QListWidget::QListWidget;
protected:
    void mousePressEvent(QMouseEvent* e) override {
        QListWidgetItem* it = itemAt(e->pos());
        if (!it && e->button() == Qt::LeftButton) {
            setCurrentRow(-1);
            clearSelection();
            e->accept();
            return;
        }
        QListWidget::mousePressEvent(e);
    }
    void keyPressEvent(QKeyEvent* e) override {
        if (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) {
            int row = currentRow();
            auto& ctl = ProjectController::instance();
            if (row >= 0 && row < ctl.schemeCount()) {
                // 不能删本体 (idx=0)
                if (row == 0) {
                    e->accept();
                    return;
                }
                ctl.removeScheme(row);
                e->accept();
                return;
            }
        }
        QListWidget::keyPressEvent(e);
    }
};

// 行 overlay 容器:
//   - 整行 setItemWidget 用. 自身布局把 🔒 按钮 push 到右侧.
//   - 中间空白区被点击时, 把鼠标事件"转发"给 QListWidget 的 viewport, 让 list 正常
//     处理选中 / 双击 (= 重命名). 锁按钮在 widget 子层级, 由 Qt 优先 hit-test, 不影响.
//   - 不用 WA_TransparentForMouseEvents (那会让子按钮一同失效).
class RowContainer : public QWidget
{
public:
    explicit RowContainer(QListWidget* list, QListWidgetItem* item)
        : QWidget(list->viewport()), m_list(list), m_item(item) {}

protected:
    void mousePressEvent(QMouseEvent* e) override {
        if (forwardToList(e)) e->accept();
        else QWidget::mousePressEvent(e);
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
        if (forwardToList(e)) e->accept();
        else QWidget::mouseReleaseEvent(e);
    }
    void mouseDoubleClickEvent(QMouseEvent* e) override {
        // 把双击事件直送 list, list 会发 itemDoubleClicked 信号 → 弹改名对话框
        if (!m_list || !m_item) { QWidget::mouseDoubleClickEvent(e); return; }
        if (e->button() == Qt::LeftButton) {
            // 选中行 (确保 currentRow 同步) 后, 主动发 itemDoubleClicked
            m_list->setCurrentItem(m_item);
            emit static_cast<QListWidget*>(m_list)->itemDoubleClicked(m_item);
            e->accept();
            return;
        }
        QWidget::mouseDoubleClickEvent(e);
    }
    void contextMenuEvent(QContextMenuEvent* e) override {
        // 右键 → 让 list 走 customContextMenuRequested
        if (m_list) {
            const QPoint inList = m_list->viewport()->mapFromGlobal(e->globalPos());
            QContextMenuEvent fwd(e->reason(), inList, e->globalPos(), e->modifiers());
            QApplication::sendEvent(m_list->viewport(), &fwd);
            e->accept();
            return;
        }
        QWidget::contextMenuEvent(e);
    }

private:
    bool forwardToList(QMouseEvent* e) {
        if (!m_list || !m_item) return false;
        // 选中本行 (即使空白区也算选中, 与 QListWidget 默认一致)
        m_list->setCurrentItem(m_item);
        return true;
    }
    QListWidget*     m_list = nullptr;
    QListWidgetItem* m_item = nullptr;
};

// 取 body 第 0 方向 第 0 帧 路径 (如不存在用 layers[0] 第一可用帧).
QString findThumbnailSourcePath(const Project& proj)
{
    if (proj.layers.isEmpty()) return {};

    auto pickFromLayer = [&](const LayerData& l) -> QString {
        const QString preferAct = proj.currentAction.isEmpty() ? "stand" : proj.currentAction;
        const Action* a = l.action(preferAct);
        if (!a) {
            // 任何动作
            for (auto it = l.actions.begin(); it != l.actions.end(); ++it) {
                a = &it.value(); break;
            }
        }
        if (!a || a->framesByDir.isEmpty()) return {};
        // 任何方向第 0 帧
        const auto& fl = a->framesByDir.first();
        return fl.isEmpty() ? QString() : fl.first();
    };

    // 优先 body 层
    for (const auto& l : proj.layers) {
        if (l.kind == LayerKind::Body) {
            QString p = pickFromLayer(l);
            if (!p.isEmpty()) return p;
        }
    }
    // 兜底: layers[0]
    return pickFromLayer(proj.layers.first());
}

// 把 lut 字节加上 alpha=255 (HALD-CLUT 通常无 alpha 用途)
void fillAlpha255(std::array<uint8_t, 256*16*4>& a)
{
    for (size_t i = 3; i < a.size(); i += 4) a[i] = 255;
}

// 加载恒等 LUT (颜色图.png → bytes)
bool loadIdentityLut(std::array<uint8_t, 256*16*4>& outBytes)
{
    QFile f(":/lut/default.png");
    if (!f.open(QIODevice::ReadOnly)) return false;
    QByteArray buf = f.readAll();
    int w = 0, h = 0, comp = 0;
    unsigned char* px = stbi_load_from_memory(
        (const unsigned char*)buf.constData(), buf.size(), &w, &h, &comp, 4);
    if (!px || w != 256 || h != 16) {
        if (px) stbi_image_free(px);
        return false;
    }
    memcpy(outBytes.data(), px, outBytes.size());
    stbi_image_free(px);
    return true;
}

// 从 PNG 文件加载 LUT 字节
bool loadLutFromPng(const QString& path, std::array<uint8_t, 256*16*4>& outBytes)
{
    QByteArray buf = PathUtil::readAll(path);
    if (buf.isEmpty()) return false;
    int w = 0, h = 0, comp = 0;
    unsigned char* px = stbi_load_from_memory(
        (const unsigned char*)buf.constData(), buf.size(), &w, &h, &comp, 4);
    if (!px || w != 256 || h != 16) {
        if (px) stbi_image_free(px);
        return false;
    }
    memcpy(outBytes.data(), px, outBytes.size());
    stbi_image_free(px);
    return true;
}

// 构 QListWidgetItem 文本 (含 tag). 锁状态由行尾 inline 按钮表示, 不再写文本里.
QString itemTextFor(const Scheme& s)
{
    QString tag;
    if (s.isBuiltin) tag = "  [本体]";
    else if (s.isBaked) tag = "  [已烘焙]";
    else tag = "  [可编辑]";
    return s.name + tag;
}

QPixmap makePlaceholderPixmap()
{
    QPixmap pm(ThumbnailWorker::kThumbW, ThumbnailWorker::kThumbH);
    pm.fill(QColor(50, 50, 50));
    QPainter p(&pm);
    p.setPen(QColor(100, 100, 100));
    p.drawRect(0, 0, pm.width() - 1, pm.height() - 1);
    p.setPen(QColor(140, 140, 140));
    p.drawText(pm.rect(), Qt::AlignCenter, "...");
    return pm;
}

} // namespace

SchemePanel::SchemePanel(QWidget* parent) : QWidget(parent)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_list = new GalleryList(this);
    m_list->setViewMode(QListView::ListMode);
    m_list->setIconSize(QSize(ThumbnailWorker::kThumbW, ThumbnailWorker::kThumbH));
    m_list->setSpacing(2);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_list->setStyleSheet(
        "QListWidget{background:#262626;color:#ddd;border:none;}"
        "QListWidget::item{padding:4px 6px;}"
        "QListWidget::item:selected{background:#3b6ea8;color:#fff;}"
    );
    root->addWidget(m_list);

    // 底部 50px 留白 (避免最后一个方案紧贴 dock 边沿, 视觉透气)
    auto* bottomSpacer = new QWidget(this);
    bottomSpacer->setFixedHeight(50);
    bottomSpacer->setStyleSheet("background:#262626;");  // 跟列表同色, 一气呵成
    root->addWidget(bottomSpacer);

    auto& ctl = ProjectController::instance();
    connect(&ctl, &ProjectController::projectLoaded, this, [this]{
        initThumbnailSource();
        rebuild();
        requestAllThumbnails();
    });
    connect(&ctl, &ProjectController::schemesChanged, this, [this]{
        rebuild();
        requestAllThumbnails();
    });
    connect(&ctl, &ProjectController::currentSchemeChanged, this, &SchemePanel::rebuild);
    connect(&ctl, &ProjectController::effectsChanged, this, [this]{
        // 当前编辑方案的滑块改变 → 只重烘当前方案缩略图
        const int idx = ProjectController::instance().currentSchemeIndex();
        if (idx >= 0) requestThumbnail(idx);
    });

    connect(m_list, &QListWidget::currentRowChanged,
            this, &SchemePanel::onCurrentRowChanged);
    connect(m_list, &QListWidget::itemDoubleClicked,
            this, &SchemePanel::onItemDoubleClicked);
    connect(m_list, &QListWidget::itemChanged,
            this, &SchemePanel::onItemChanged);
    connect(m_list, &QListWidget::customContextMenuRequested,
            this, &SchemePanel::onContextMenu);

    m_worker = std::make_unique<ThumbnailWorker>();
    connect(m_worker.get(), &ThumbnailWorker::thumbnailReady,
            this, &SchemePanel::onThumbnailReady, Qt::QueuedConnection);
    m_worker->start(QThread::LowPriority);

    m_baker = std::make_unique<LutBaker>();
    // baker 延迟 init (要 D3D 设备就绪)

    rebuild();
}

SchemePanel::~SchemePanel()
{
    if (m_worker) {
        m_worker->stop();
        m_worker->wait();
        m_worker.reset();
    }
}

void SchemePanel::initThumbnailSource()
{
    const auto& proj = ProjectController::instance().project();
    const QString p = findThumbnailSourcePath(proj);
    if (p.isEmpty() || !m_worker) return;

    // 用 stb 解 TGA → RGBA
    QByteArray buf = PathUtil::readAll(p);
    if (buf.isEmpty()) return;
    int w = 0, h = 0, comp = 0;
    unsigned char* px = stbi_load_from_memory(
        (const unsigned char*)buf.constData(), buf.size(), &w, &h, &comp, 4);
    if (!px) return;
    m_worker->setSource(px, w, h);
    stbi_image_free(px);
    m_thumbCache.clear();
}

void SchemePanel::rebuild()
{
    if (!m_list) return;
    QSignalBlocker b(m_list);
    m_list->clear();

    const auto& proj = ProjectController::instance().project();
    QPixmap placeholder = makePlaceholderPixmap();
    for (int i = 0; i < proj.schemes.size(); ++i) {
        auto* it = new QListWidgetItem(itemTextFor(proj.schemes[i]));
        QPixmap pm = m_thumbCache.value(i);
        if (pm.isNull()) pm = placeholder;
        it->setIcon(QIcon(pm));
        it->setSizeHint(QSize(0, ThumbnailWorker::kThumbH + 6));
        // 仅非本体可重命名 (双击)
        if (!proj.schemes[i].isBuiltin) {
            it->setFlags(it->flags() | Qt::ItemIsEditable);
        }
        // 锁住的方案: 整行底板色 #823c18 (暖棕色, 易与未锁/选中区分)
        if (proj.schemes[i].locked && !proj.schemes[i].isBuiltin) {
            it->setBackground(QColor(0x82, 0x3c, 0x18));
            it->setForeground(QColor(0xff, 0xff, 0xff));
        }
        m_list->addItem(it);

        // 行尾嵌入 🔒 按钮 (本体/已烘焙不挂)
        if (!proj.schemes[i].isBuiltin) {
            auto* container = new RowContainer(m_list, it);
            // container 高度严格匹配 sizeHint, 否则 setItemWidget 会让 item 可视高度按
            // widget 自身 sizeHint 计算 → 与 sizeHint(0, kThumbH+6) 不一致 → 滚动/高亮错位
            // (复现: 选 25/26/27 高亮显示到 22/23/24).
            container->setFixedHeight(ThumbnailWorker::kThumbH + 6);
            auto* lay = new QHBoxLayout(container);
            lay->setContentsMargins(0, 0, 8, 0);
            lay->addStretch(1);
            auto* btn = new QPushButton(container);
            btn->setCheckable(true);
            btn->setChecked(proj.schemes[i].locked);
            btn->setFixedSize(34, 32);
            btn->setFlat(true);
            btn->setCursor(Qt::PointingHandCursor);
            btn->setFocusPolicy(Qt::NoFocus);
            btn->setText(proj.schemes[i].locked
                ? QStringLiteral("🔒") : QStringLiteral("🔓"));
            btn->setToolTip(proj.schemes[i].locked
                ? QStringLiteral("已锁定 — 随机变色将跳过此方案 (点击解锁)")
                : QStringLiteral("未锁定 — 点击锁定后, 随机变色不再修改此方案"));
            // 无边框透明背景, 仅图标. hover 高亮, 不画边.
            btn->setStyleSheet(
                "QPushButton{border:none;background:transparent;color:#ddd;font-size:22px;padding:0;}"
                "QPushButton:hover{color:#fff;}"
                "QPushButton:checked{color:#ffcc55;}"
            );
            const int rowIdx = i;
            connect(btn, &QPushButton::clicked, this, [rowIdx](bool checked){
                ProjectController::instance().setSchemeLocked(rowIdx, checked);
            });
            lay->addWidget(btn);
            m_list->setItemWidget(it, container);
        }
    }
    // setCurrentRow 默认 autoScroll 会强制把选中行滚到可见区 (含居中),
    // 用户选 27 时画廊会跳到 "3~27" 区段, 但用户希望看到的是 "0~24"
    // (= 屏幕没动) — 所以这里临时关 autoScroll, 同时保留 scrollbar 位置.
    const int savedScroll = m_list->verticalScrollBar()->value();
    const bool savedAutoScroll = m_list->hasAutoScroll();
    m_list->setAutoScroll(false);
    if (proj.currentSchemeIndex >= 0 && proj.currentSchemeIndex < proj.schemes.size()) {
        m_list->setCurrentRow(proj.currentSchemeIndex);
    } else {
        m_list->setCurrentRow(-1);
        m_list->clearSelection();
    }
    m_list->verticalScrollBar()->setValue(savedScroll);
    m_list->setAutoScroll(savedAutoScroll);
}

void SchemePanel::onCurrentRowChanged(int row)
{
    auto& ctl = ProjectController::instance();
    if (row < 0) {
        if (ctl.currentSchemeIndex() != -1) ctl.setCurrentSchemeIndex(-1);
        return;
    }
    if (row >= ctl.schemeCount()) return;
    if (row != ctl.currentSchemeIndex()) {
        ctl.setCurrentSchemeIndex(row);
    }
}

void SchemePanel::onItemDoubleClicked(QListWidgetItem* item)
{
    // 注意: 整行上挂了 🔒 按钮 (setItemWidget), QListWidget 默认的 editItem
    //       因 widget 覆盖文本区会失效. 这里直接弹对话框收名字.
    if (!item) return;
    if (!(item->flags() & Qt::ItemIsEditable)) return;     // 本体不可改名
    int row = m_list->row(item);
    auto& ctl = ProjectController::instance();
    if (row < 0 || row >= ctl.schemeCount()) return;
    const auto& sc = ctl.project().schemes[row];

    // 用户输入: 只让 ta 改 "方案 N - " 之后的后缀部分, 前缀强制保留 (与 onItemChanged 一致)
    const QString prefix = QString("方案 %1 - ").arg(row);
    QString currentName = sc.name;
    QString currentSuffix = currentName.startsWith(prefix)
        ? currentName.mid(prefix.size())
        : currentName;
    bool ok = false;
    QString suffix = QInputDialog::getText(this,
        QStringLiteral("重命名方案"),
        QStringLiteral("方案名 (前缀 [%1] 自动保留):").arg(prefix.trimmed()),
        QLineEdit::Normal, currentSuffix, &ok);
    if (!ok) return;
    suffix = suffix.trimmed();
    if (suffix.isEmpty()) return;
    // 剥可能残留的"方案 X -" 模式 (用户复制粘贴时)
    QRegularExpression re("^方案\\s*\\d+\\s*-\\s*");
    suffix.remove(re);
    ctl.renameScheme(row, prefix + suffix);
}

void SchemePanel::onItemChanged(QListWidgetItem* item)
{
    if (m_blockEditEmit || !item) return;
    int row = m_list->row(item);
    auto& ctl = ProjectController::instance();
    if (row < 0 || row >= ctl.schemeCount()) return;

    // 提取用户输入 (剥离 tag 后缀  "  [..]")
    QString text = item->text();
    int tagIdx = text.indexOf("  [");
    if (tagIdx >= 0) text = text.left(tagIdx);
    text = text.trimmed();

    // 锁前缀 "方案 N - ": 不论用户输入啥, 强制保留 "方案 N - " 开头.
    const QString prefix = QString("方案 %1 - ").arg(row);
    QString suffix;
    if (text.startsWith(prefix)) {
        suffix = text.mid(prefix.size()).trimmed();
    } else {
        // 用户编辑掉了前缀, 把整段当后缀
        suffix = text.trimmed();
        // 再剥可能残留的"方案 X -" 模式
        QRegularExpression re("^方案\\s*\\d+\\s*-\\s*");
        suffix.remove(re);
    }
    if (suffix.isEmpty()) {
        // 撤回原名
        QSignalBlocker b(m_list);
        item->setText(itemTextFor(ctl.project().schemes[row]));
        return;
    }
    const QString finalName = prefix + suffix;
    ctl.renameScheme(row, finalName);
}

void SchemePanel::onContextMenu(const QPoint& pos)
{
    QListWidgetItem* it = m_list->itemAt(pos);
    int row = it ? m_list->row(it) : -1;
    auto& ctl = ProjectController::instance();
    const auto& proj = ctl.project();

    QMenu menu(this);
    auto* aRename = menu.addAction(QStringLiteral("重命名"));
    auto* aDup    = menu.addAction(QStringLiteral("复制为新方案"));
    menu.addSeparator();
    bool hasRow = (row >= 0 && row < proj.schemes.size());
    bool isBuiltin = hasRow && proj.schemes[row].isBuiltin;
    bool isBaked   = hasRow && proj.schemes[row].isBaked;
    bool currLocked = hasRow && proj.schemes[row].locked;
    auto* aLock = menu.addAction(currLocked
        ? QStringLiteral("🔓 解锁此方案")
        : QStringLiteral("🔒 锁定此方案 (随机变色将跳过)"));
    aLock->setEnabled(hasRow && !isBuiltin);
    menu.addSeparator();
    // ❤️细化方案: 本体禁用; 已烘焙允许 (打开时确认降级)
    auto* aRefine = menu.addAction(QStringLiteral("❤️ 细化方案"));
    aRefine->setEnabled(hasRow && !isBuiltin);
    // 🔷配色方案转移: 子菜单 (本体禁用); 子菜单内容动态构造
    QMenu* transferMenu = nullptr;
    if (hasRow && !isBuiltin) {
        transferMenu = buildTransferMenu(row, &menu);
    }
    QAction* aTransferPlaceholder = nullptr;
    if (transferMenu) {
        transferMenu->setTitle(QStringLiteral("🔷 配色方案转移"));
        menu.addMenu(transferMenu);
    } else {
        aTransferPlaceholder = menu.addAction(QStringLiteral("🔷 配色方案转移"));
        aTransferPlaceholder->setEnabled(false);
    }
    menu.addSeparator();
    auto* aDel    = menu.addAction(QStringLiteral("删除"));

    aRename->setEnabled(hasRow && !isBuiltin);
    aDup->setEnabled(hasRow);
    aDel->setEnabled(hasRow && !isBuiltin && ctl.schemeCount() > 1);

    QAction* sel = menu.exec(m_list->mapToGlobal(pos));
    if (!sel) return;
    if (sel == aRename) {
        if (hasRow) onItemDoubleClicked(it);    // 复用同一段对话框逻辑
    } else if (sel == aDup) {
        if (!hasRow) return;
        // 复制: 新建一个空方案, 拷 effects (本体复制 = 新空方案)
        int newIdx = ctl.addScheme(proj.schemes[row].name + " 副本");
        auto& dst = ctl.projectMut().schemes[newIdx];
        dst.layerEffects = proj.schemes[row].layerEffects;
        dst.isBaked = false;        // 复制后总是用户可编辑
        dst.layerLutPath.clear();
        ctl.setCurrentSchemeIndex(newIdx);
    } else if (sel == aLock) {
        if (hasRow && !isBuiltin) ctl.setSchemeLocked(row, !currLocked);
    } else if (sel == aRefine) {
        if (hasRow && !isBuiltin) openRefineDialog(row);
    } else if (sel == aDel) {
        if (hasRow) ctl.removeScheme(row);
    }
    // 转移子菜单选项: 走自己的 lambda, 不进 if-else 链
    (void)isBaked;
}

void SchemePanel::openRefineDialog(int schemeIdx)
{
    auto& ctl = ProjectController::instance();
    const auto& proj = ctl.project();
    if (schemeIdx < 0 || schemeIdx >= proj.schemes.size()) return;
    const auto& sc = proj.schemes[schemeIdx];
    if (sc.isBuiltin) return;

    // 已烘焙方案需要先确认降级
    if (sc.isBaked) {
        auto rc = QMessageBox::question(this,
            QStringLiteral("细化方案"),
            QStringLiteral("方案 [%1] 当前为已烘焙状态.\n"
                           "继续细化会把它降级为可编辑方案, 原 add_lut PNG 引用会丢失.\n继续?")
                .arg(sc.name),
            QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel);
        if (rc != QMessageBox::Ok) return;
        QString err;
        if (!ctl.ensureSchemeEditable(schemeIdx, true, &err)) {
            QMessageBox::warning(this, QStringLiteral("细化方案"),
                QStringLiteral("降级失败: %1").arg(err.isEmpty() ? QStringLiteral("未知错误") : err));
            return;
        }
    }

    SchemeRefineDialog dlg(schemeIdx, this);
    dlg.exec();
}

QMenu* SchemePanel::buildTransferMenu(int sourceSchemeIdx, QMenu* parentMenu)
{
    auto& ctl = ProjectController::instance();
    const auto& proj = ctl.project();
    if (sourceSchemeIdx < 0 || sourceSchemeIdx >= proj.schemes.size()) return nullptr;

    // 收集源方案"有内容"的分组 — 本体/缺失层 不显示
    //   body         → 当 layers 含 Body
    //   num:NN       → 当 layers 含 Numbered 且 numberedIdx==NN
    //   addon        → 当 layers 至少 1 个 Addon
    QSet<int>  numberedSet;
    bool hasBody = false, hasAddon = false;
    for (const auto& l : proj.layers) {
        if (l.kind == LayerKind::Body)  hasBody = true;
        if (l.kind == LayerKind::Addon) hasAddon = true;
        if (l.kind == LayerKind::Numbered) numberedSet.insert(l.numberedIdx);
    }
    if (!hasBody && !hasAddon && numberedSet.isEmpty()) return nullptr;

    auto* root = new QMenu(parentMenu);

    // 构 [groupKey, displayName] 列表, 顺序: addon → num(升序) → body
    // (与截图二一致: 先列 addon, 数字层中间, body 最后)
    struct GroupEntry { QString key; QString display; };
    QVector<GroupEntry> groups;
    if (hasAddon)   groups.push_back({ QStringLiteral("addon"), QStringLiteral("addon") });
    QList<int> nums = numberedSet.values();
    std::sort(nums.begin(), nums.end());
    for (int n : nums) {
        groups.push_back({
            QString("num:%1").arg(n, 2, 10, QChar('0')),
            QString("%1").arg(n, 2, 10, QChar('0')),
        });
    }
    if (hasBody)    groups.push_back({ QStringLiteral("body"), QStringLiteral("body") });

    for (const auto& g : groups) {
        QMenu* gMenu = root->addMenu(g.display);
        // 子菜单: 列出所有 (非本体, 非源自身) 方案 ID
        bool anyTarget = false;
        for (int i = 0; i < proj.schemes.size(); ++i) {
            const auto& sc = proj.schemes[i];
            if (sc.isBuiltin) continue;        // 不允许写本体
            if (i == sourceSchemeIdx) continue; // 自己→自己 跳过
            // 显示: 方案 ID + 锁/烘焙角标 (与截图二的紧凑布局对齐)
            QString label = QString::number(i);
            if (sc.locked)  label += QStringLiteral("   🔒");
            if (sc.isBaked) label += QStringLiteral("   [已烘焙]");
            QAction* act = gMenu->addAction(label);
            const int targetIdx = i;
            const QString groupKey = g.key;
            connect(act, &QAction::triggered, this, [this, sourceSchemeIdx, targetIdx, groupKey]{
                auto& c = ProjectController::instance();
                // 目标已锁定: 二次确认
                if (c.isSchemeLocked(targetIdx)) {
                    auto rc = QMessageBox::question(this,
                        QStringLiteral("配色方案转移"),
                        QStringLiteral("目标方案 [%1] 已锁定, 仍要写入?")
                            .arg(c.project().schemes[targetIdx].name),
                        QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel);
                    if (rc != QMessageBox::Ok) return;
                }
                QString err;
                if (!c.transferSchemeColorGroup(sourceSchemeIdx, targetIdx, groupKey, &err)) {
                    QMessageBox::warning(this,
                        QStringLiteral("配色方案转移失败"),
                        err.isEmpty() ? QStringLiteral("未知错误") : err);
                }
            });
            anyTarget = true;
        }
        if (!anyTarget) {
            QAction* empty = gMenu->addAction(QStringLiteral("(没有可用目标方案)"));
            empty->setEnabled(false);
        }
    }
    return root;
}

// === 缩略图请求 ===

void SchemePanel::requestAllThumbnails()
{
    const auto& proj = ProjectController::instance().project();
    for (int i = 0; i < proj.schemes.size(); ++i) {
        requestThumbnail(i);
    }
}

void SchemePanel::requestThumbnail(int schemeIdx)
{
    if (!m_worker) return;
    const auto& proj = ProjectController::instance().project();
    if (schemeIdx < 0 || schemeIdx >= proj.schemes.size()) return;
    const auto& sc = proj.schemes[schemeIdx];

    std::array<uint8_t, 256*16*4> lut{};

    if (sc.isBuiltin) {
        // 本体 → 恒等 LUT (颜色图.png)
        if (!loadIdentityLut(lut)) return;
    } else if (sc.isBaked) {
        // 已烘焙 → 用任一层的 add_lut PNG (优先 body 层)
        QString pickedPath;
        for (const auto& l : proj.layers) {
            if (l.kind == LayerKind::Body) {
                pickedPath = sc.layerLutPath.value(l.key());
                if (!pickedPath.isEmpty()) break;
            }
        }
        if (pickedPath.isEmpty()) {
            for (const auto& l : proj.layers) {
                pickedPath = sc.layerLutPath.value(l.key());
                if (!pickedPath.isEmpty()) break;
            }
        }
        if (pickedPath.isEmpty()) {
            if (!loadIdentityLut(lut)) return;
        } else {
            if (!loadLutFromPng(pickedPath, lut)) return;
        }
    } else {
        // 用户方案 → 主线烘焙 body 层的 EffectStack (代表性, 简化 — 不是全层精确预览)
        if (!m_baker->isReady()) {
            QString err;
            if (!m_baker->init(&err)) return;
        }
        // 找 body 层的 EffectStack
        EffectStack toUse;
        bool found = false;
        for (const auto& l : proj.layers) {
            if (l.kind == LayerKind::Body) {
                if (auto* st = proj.effectsForIn(sc, l)) {
                    toUse = *st; found = true;
                }
                break;
            }
        }
        if (!found) {
            // body 没找到 → 用第一层
            for (const auto& l : proj.layers) {
                if (auto* st = proj.effectsForIn(sc, l)) {
                    toUse = *st; found = true; break;
                }
            }
        }
        QString err;
        if (!m_baker->bake(toUse, &err)) return;
        memcpy(lut.data(), m_baker->bytes().data(), lut.size());
    }
    fillAlpha255(lut);
    m_worker->enqueue(schemeIdx, lut);
}

void SchemePanel::onThumbnailReady(int schemeIdx, QImage image)
{
    if (image.isNull() || !m_list) return;
    QPixmap pm = QPixmap::fromImage(image);
    m_thumbCache.insert(schemeIdx, pm);
    if (schemeIdx >= 0 && schemeIdx < m_list->count()) {
        m_list->item(schemeIdx)->setIcon(QIcon(pm));
    }
}

} // namespace HighPro
