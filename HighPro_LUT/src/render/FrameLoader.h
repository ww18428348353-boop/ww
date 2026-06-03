#pragma once

#include "render/D3D11Texture.h"
#include <QString>
#include <QHash>
#include <QStringList>
#include <QMutex>
#include <QWaitCondition>
#include <QThread>
#include <list>
#include <memory>

namespace HighPro {

// 帧加载器: 主线 LRU 命中即返回; 后台预读线程异步 stb_image 解码到 RGBA buffer,
// 主线 pump() 拿出已解码 buffer, 调用 D3D 上传 GPU (D3D11 设备非线程安全, 上传必须主线).
//
// API:
//   get(path)        : 同步取. 命中缓存即返回; 未命中 → 阻塞主线解码上传 (兜底).
//   prefetch(paths)  : 异步预读 (后台线程跑 stb_image), 主线下次 pump() 时上传 GPU.
//   pump()           : 主线每帧调用, 把后台已解码的 buffer 上传到 GPU 加入缓存.
//   clear()          : 清空 (主线调).
class FrameLoader : public QObject
{
    Q_OBJECT
public:
    static FrameLoader& instance();

    // 同步取 (主线). 命中即返回; 未命中 → 主线阻塞解码 (与 M2 行为一致).
    std::shared_ptr<D3D11Texture> get(const QString& utf8Path);

    // 异步预读 (主线调用, 后台线程跑解码).
    // 已在缓存或正在解码的会跳过. 旧的未消费请求会被新一批替换 (避免堆积).
    void prefetch(const QStringList& paths);

    // 主线每帧 pump 一次, 拿出后台已解码的缓冲 → GPU 上传 → 加入缓存.
    // 单次最多上传 maxUploads 张, 防止主线一次卡顿.
    void pump(int maxUploads = 4);

    void clear();
    void setMaxEntries(int n) { m_maxEntries = qMax(1, n); }
    int  cacheSize() const { return (int)m_cache.size(); }

    // 析构 / 关闭
    ~FrameLoader() override;

private:
    FrameLoader();

    // === 主线 LRU ===
    struct Entry {
        QString path;
        std::shared_ptr<D3D11Texture> tex;
    };
    std::list<Entry> m_lru;                                  // 头=最近, 尾=最旧
    QHash<QString, std::list<Entry>::iterator> m_cache;
    int  m_maxEntries = 512;

    void touch(const QString& path);                         // 命中 → 移到头
    void insertHot(const QString& path, std::shared_ptr<D3D11Texture> tex);
    void evictIfFull();

    // === 后台解码 ===
    class DecodeThread;
    DecodeThread* m_thread = nullptr;

    // 主→后: 待解码队列 (path 列表)
    QMutex          m_inMu;
    QStringList     m_inQueue;
    QWaitCondition  m_inCv;
    bool            m_quit = false;

    // 后→主: 已解码 buffer 队列
    struct Decoded {
        QString path;
        QByteArray rgba;        // RGBA8 top-down
        int  width = 0;
        int  height = 0;
        bool failed = false;
    };
    QMutex          m_outMu;
    std::list<Decoded> m_outQueue;

    // 标记 path 是否在 inQueue 或后台跑中, 避免重复入队
    QHash<QString, bool> m_inflight;

    // 让 DecodeThread 用
    friend class DecodeThread;
};

} // namespace HighPro
