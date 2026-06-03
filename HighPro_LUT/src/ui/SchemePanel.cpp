#include "SchemePanel.h"
#include "app/ProjectController.h"
#include "app/ThumbnailWorker.h"
#include "render/LutBaker.h"
#include "core/PathUtil.h"
#include "core/HaldClut.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QListWidgetItem>
#include <QSignalBlocker>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QInputDialog>
#include <QFile>
#include <QImageReader>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
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
            auto* container = new QWidget(m_list);
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
    if (proj.currentSchemeIndex >= 0 && proj.currentSchemeIndex < proj.schemes.size()) {
        m_list->setCurrentRow(proj.currentSchemeIndex);
    } else {
        m_list->setCurrentRow(-1);
        m_list->clearSelection();
    }
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
    if (!item) return;
    if (item->flags() & Qt::ItemIsEditable) {
        m_list->editItem(item);
    }
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
    bool currLocked = hasRow && proj.schemes[row].locked;
    auto* aLock = menu.addAction(currLocked
        ? QStringLiteral("🔓 解锁此方案")
        : QStringLiteral("🔒 锁定此方案 (随机变色将跳过)"));
    aLock->setEnabled(hasRow && !isBuiltin);
    menu.addSeparator();
    auto* aDel    = menu.addAction(QStringLiteral("删除"));

    aRename->setEnabled(hasRow && !isBuiltin);
    aDup->setEnabled(hasRow);
    aDel->setEnabled(hasRow && !isBuiltin && ctl.schemeCount() > 1);

    QAction* sel = menu.exec(m_list->mapToGlobal(pos));
    if (!sel) return;
    if (sel == aRename) {
        if (hasRow) m_list->editItem(it);
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
    } else if (sel == aDel) {
        if (hasRow) ctl.removeScheme(row);
    }
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
