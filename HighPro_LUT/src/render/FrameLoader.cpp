#include "FrameLoader.h"
#include "core/PathUtil.h"

#include <QDebug>
#include <QMutexLocker>

extern "C" unsigned char* stbi_load_from_memory(
    const unsigned char* buffer, int len, int* x, int* y, int* channels_in_file, int desired_channels);
extern "C" void stbi_image_free(void* retval_from_stbi_load);

namespace HighPro {

class FrameLoader::DecodeThread : public QThread
{
public:
    explicit DecodeThread(FrameLoader* parent) : m_parent(parent) {}

protected:
    void run() override
    {
        forever {
            QString path;
            {
                QMutexLocker lk(&m_parent->m_inMu);
                while (!m_parent->m_quit && m_parent->m_inQueue.isEmpty()) {
                    m_parent->m_inCv.wait(&m_parent->m_inMu);
                }
                if (m_parent->m_quit) return;
                path = m_parent->m_inQueue.takeFirst();
            }
            if (path.isEmpty()) continue;

            // 解码 (CPU 重活)
            Decoded d;
            d.path = path;
            QByteArray buf = PathUtil::readAll(path);
            if (buf.isEmpty()) {
                d.failed = true;
            } else {
                int w = 0, h = 0, comp = 0;
                unsigned char* px = stbi_load_from_memory(
                    (const unsigned char*)buf.constData(), buf.size(),
                    &w, &h, &comp, 4);
                if (!px) {
                    d.failed = true;
                } else {
                    // 素材是 Straight Alpha (实测 ~77% 半透明像素 rgb>a, 与 AE/游戏一致).
                    // 不做 CPU 预乘 — 直接上传 straight RGBA, GPU 用
                    //   BlendState: Src=SRC_ALPHA, Dst=INV_SRC_ALPHA
                    //   PS:        return float4(rgb, a)  (不预乘)
                    // 与 AE/游戏引擎对齐.
                    d.rgba = QByteArray((const char*)px, w * h * 4);
                    d.width  = w;
                    d.height = h;
                    stbi_image_free(px);
                }
            }
            // 推回主线
            QMutexLocker lk(&m_parent->m_outMu);
            m_parent->m_outQueue.push_back(std::move(d));
        }
    }

private:
    FrameLoader* m_parent;
};

FrameLoader& FrameLoader::instance()
{
    static FrameLoader f;
    return f;
}

FrameLoader::FrameLoader()
{
    m_thread = new DecodeThread(this);
    m_thread->start(QThread::LowPriority);
}

FrameLoader::~FrameLoader()
{
    {
        QMutexLocker lk(&m_inMu);
        m_quit = true;
        m_inCv.wakeAll();
    }
    if (m_thread) {
        m_thread->wait();
        delete m_thread;
        m_thread = nullptr;
    }
}

void FrameLoader::touch(const QString& path)
{
    auto it = m_cache.find(path);
    if (it == m_cache.end()) return;
    m_lru.splice(m_lru.begin(), m_lru, it.value());
}

void FrameLoader::insertHot(const QString& path, std::shared_ptr<D3D11Texture> tex)
{
    auto it = m_cache.find(path);
    if (it != m_cache.end()) {
        it.value()->tex = tex;
        m_lru.splice(m_lru.begin(), m_lru, it.value());
        return;
    }
    Entry e{ path, tex };
    m_lru.push_front(std::move(e));
    m_cache.insert(path, m_lru.begin());
    evictIfFull();
}

void FrameLoader::evictIfFull()
{
    while ((int)m_cache.size() > m_maxEntries && !m_lru.empty()) {
        const QString p = m_lru.back().path;
        m_lru.pop_back();
        m_cache.remove(p);
    }
}

std::shared_ptr<D3D11Texture> FrameLoader::get(const QString& path)
{
    if (path.isEmpty()) return {};

    auto it = m_cache.find(path);
    if (it != m_cache.end()) {
        m_lru.splice(m_lru.begin(), m_lru, it.value());      // touch
        return it.value()->tex;
    }

    // 兜底: 主线同步解码 + 上传 (与 M2 一致行为, 防止画面黑帧)
    auto tex = std::make_shared<D3D11Texture>();
    QString err;
    // 帧 TGA 是 Straight Alpha (与 AE/游戏一致), 不预乘 — 仅原样上传.
    if (!tex->loadFromPath(path, &err, /*premultiply=*/false)) {
        qWarning() << "[FrameLoader sync]" << path << err;
        return {};
    }
    insertHot(path, tex);
    {
        QMutexLocker lk(&m_inMu);
        m_inflight.remove(path);   // 它走主线了, 不再需要后台跑
    }
    return tex;
}

void FrameLoader::prefetch(const QStringList& paths)
{
    QMutexLocker lk(&m_inMu);
    for (const QString& p : paths) {
        if (p.isEmpty()) continue;
        if (m_cache.contains(p)) continue;
        if (m_inflight.value(p, false)) continue;
        m_inflight.insert(p, true);
        m_inQueue.append(p);
    }
    if (!m_inQueue.isEmpty()) m_inCv.wakeAll();
}

void FrameLoader::pump(int maxUploads)
{
    int up = 0;
    while (up < maxUploads) {
        Decoded d;
        {
            QMutexLocker lk(&m_outMu);
            if (m_outQueue.empty()) return;
            d = std::move(m_outQueue.front());
            m_outQueue.pop_front();
        }

        // 标记不再 inflight
        {
            QMutexLocker lk(&m_inMu);
            m_inflight.remove(d.path);
        }

        if (d.failed || d.rgba.isEmpty()) {
            qWarning() << "[FrameLoader async] decode failed:" << d.path;
            continue;
        }

        // 主线 GPU 上传 (D3D11 device 非线程安全)
        auto tex = std::make_shared<D3D11Texture>();
        QString err;
        if (!tex->createFromRGBA((const uint8_t*)d.rgba.constData(),
                                  d.width, d.height,
                                  DXGI_FORMAT_R8G8B8A8_UNORM, &err)) {
            qWarning() << "[FrameLoader async] upload failed:" << d.path << err;
            continue;
        }
        insertHot(d.path, tex);
        ++up;
    }
}

void FrameLoader::clear()
{
    m_lru.clear();
    m_cache.clear();
    {
        QMutexLocker lk(&m_inMu);
        m_inQueue.clear();
        m_inflight.clear();
    }
    {
        QMutexLocker lk(&m_outMu);
        m_outQueue.clear();
    }
}

} // namespace HighPro
