#pragma once

#include <QObject>
#include <QImage>
#include <QString>
#include <QMutex>
#include <QQueue>
#include <QRect>
#include <QWaitCondition>
#include <QThread>
#include <array>
#include <cstdint>

namespace HighPro {

// 后台缩略图烘焙器:
//   主线给一份"源图 RGBA" (body 第一帧, 已用 stb_image 解码) + LUT 字节 → 计算缩略图.
//   缩略图算法 = CPU HALD-CLUT 三线性 (与 recolor.hlsl 等价); 每帧只在像素级跑一次, 64×48 ≈ 3000 像素, 耗时 ms 级.
//   每个 schemeIdx 对应一个 latest 请求 (新请求覆盖旧的, 旧的中途丢弃).
class ThumbnailWorker : public QThread
{
    Q_OBJECT
public:
    explicit ThumbnailWorker(QObject* parent = nullptr);
    ~ThumbnailWorker() override;

    // 设置/更新源图 (body 帧). 调用方传 RGBA 像素 (top-down). worker 内部拷贝.
    void setSource(const uint8_t* rgba, int w, int h);

    // 派发一个缩略图请求. lutBytes 长度必须 = 256×16×4.
    // 同 schemeIdx 后续请求会覆盖前一个 (合并机制).
    void enqueue(int schemeIdx, const std::array<uint8_t, 256*16*4>& lutBytes);

    // 关闭 worker (析构会自动调).
    void stop();

    static constexpr int kThumbW = 64;
    static constexpr int kThumbH = 40;        // 单方案行高紧凑

signals:
    // 烘焙完成. image 是 ARGB32 (Qt 标准), schemeIdx 与 enqueue 一致.
    void thumbnailReady(int schemeIdx, QImage image);

protected:
    void run() override;

private:
    QImage processOne(int schemeIdx,
                      const std::array<uint8_t, 256*16*4>& lutBytes);

    QMutex          m_mu;
    QWaitCondition  m_cv;
    bool            m_quit = false;

    // 源图: 主线设置一次 (loadSource 时), 后续不变. worker 内只读.
    QByteArray      m_srcRgba;        // 完整 RGBA (top-down)
    int             m_srcW = 0, m_srcH = 0;
    QRect           m_srcBBox;        // 非透明像素 BBox (源像素坐标)

    // 请求队列: schemeIdx -> latest lutBytes
    struct Job { int idx; std::array<uint8_t, 256*16*4> lut; };
    QQueue<Job>     m_jobs;
};

} // namespace HighPro
