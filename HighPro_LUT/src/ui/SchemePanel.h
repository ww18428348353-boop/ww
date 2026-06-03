#pragma once

#include <QWidget>
#include <QHash>
#include <QPixmap>
#include <memory>

class QListWidget;
class QListWidgetItem;
class QAction;

namespace HighPro {

class ThumbnailWorker;
class LutBaker;

// 方案画廊 dock 内容:
//   - 显示 64×80 缩略图 + 名字
//   - 点击切换 currentSchemeIndex; 点击空白 → -1 (无选中)
//   - 双击重命名; 右键: 复制 / 删除 / 重命名
//   - 缩略图后台烘焙 (ThumbnailWorker), 主线只调度
class SchemePanel : public QWidget
{
    Q_OBJECT
public:
    explicit SchemePanel(QWidget* parent = nullptr);
    ~SchemePanel() override;

private slots:
    void rebuild();
    void onCurrentRowChanged(int row);
    void onItemDoubleClicked(QListWidgetItem* item);
    void onItemChanged(QListWidgetItem* item);
    void onContextMenu(const QPoint& pos);
    void onThumbnailReady(int schemeIdx, QImage image);

private:
    void requestThumbnail(int schemeIdx);
    void requestAllThumbnails();
    void initThumbnailSource();          // projectLoaded 时把 body 第一帧 → worker

    QListWidget* m_list{ nullptr };
    bool         m_blockEditEmit = false;

    std::unique_ptr<ThumbnailWorker> m_worker;
    std::unique_ptr<LutBaker>        m_baker;        // 主线烘 lutBytes 用

    QHash<int, QPixmap> m_thumbCache;                // schemeIdx -> 缩略图
};

} // namespace HighPro
