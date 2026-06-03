#include "ThumbnailWorker.h"

#include <QMutexLocker>
#include <cmath>
#include <cstring>
#include <algorithm>

namespace HighPro {

namespace {

inline uint8_t clamp255(int v) { return (uint8_t)std::clamp(v, 0, 255); }

// CPU HALD-CLUT 三线性查找 (与 recolor.hlsl SampleHald 算法一致).
//   lut[256×16×4], 输入 (r,g,b) 0..255 → 输出 (or,og,ob) 0..255
void cpuLut(const uint8_t* lut, uint8_t r, uint8_t g, uint8_t b,
            uint8_t& or_, uint8_t& og, uint8_t& ob)
{
    constexpr int W = 256, H = 16;
    const float fR = r / 255.0f * 15.0f;
    const float fG = g / 255.0f * 15.0f;
    const float fB = b / 255.0f * 15.0f;

    int b0 = (int)std::floor(fB);
    int b1 = std::min(b0 + 1, 15);
    float bf = fB - b0;

    auto sample2 = [&](int slice, int gi, float ri,
                       float& or2, float& og2, float& ob2) {
        int xi0 = slice * 16 + (int)std::floor(ri);
        int xi1 = std::min(xi0 + 1, slice * 16 + 15);
        float xf = ri - std::floor(ri);
        const uint8_t* p0 = lut + (gi * W + xi0) * 4;
        const uint8_t* p1 = lut + (gi * W + xi1) * 4;
        or2 = p0[0] + (p1[0] - p0[0]) * xf;
        og2 = p0[1] + (p1[1] - p0[1]) * xf;
        ob2 = p0[2] + (p1[2] - p0[2]) * xf;
    };

    int gi0 = (int)std::floor(fG);
    int gi1 = std::min(gi0 + 1, 15);
    float gf = fG - gi0;

    auto sample3 = [&](int slice, float gi, float ri,
                       float& or3, float& og3, float& ob3) {
        float a0r, a0g, a0b, a1r, a1g, a1b;
        sample2(slice, (int)std::floor(gi), ri, a0r, a0g, a0b);
        sample2(slice, std::min((int)std::floor(gi) + 1, 15), ri, a1r, a1g, a1b);
        float yf = gi - std::floor(gi);
        or3 = a0r + (a1r - a0r) * yf;
        og3 = a0g + (a1g - a0g) * yf;
        ob3 = a0b + (a1b - a0b) * yf;
    };

    float r0, g0, blue0, r1, g1, blue1;
    sample3(b0, fG, fR, r0, g0, blue0);
    sample3(b1, fG, fR, r1, g1, blue1);
    or_ = clamp255((int)std::lround(r0 + (r1 - r0) * bf));
    og  = clamp255((int)std::lround(g0 + (g1 - g0) * bf));
    ob  = clamp255((int)std::lround(blue0 + (blue1 - blue0) * bf));
}

// 找源图非透明像素 BBox. 没有 alpha>=8 的像素则返回 (0,0,w,h).
QRect findOpaqueBBox(const uint8_t* src, int w, int h)
{
    int minX = w, minY = h, maxX = -1, maxY = -1;
    for (int y = 0; y < h; ++y) {
        const uint8_t* row = src + (size_t)y * w * 4;
        for (int x = 0; x < w; ++x) {
            if (row[x * 4 + 3] >= 8) {
                if (x < minX) minX = x;
                if (x > maxX) maxX = x;
                if (y < minY) minY = y;
                if (y > maxY) maxY = y;
            }
        }
    }
    if (maxX < 0) return QRect(0, 0, w, h);
    // 留 1px 边
    minX = std::max(0, minX - 1); minY = std::max(0, minY - 1);
    maxX = std::min(w - 1, maxX + 1); maxY = std::min(h - 1, maxY + 1);
    return QRect(minX, minY, maxX - minX + 1, maxY - minY + 1);
}

// 双线性下采样: 把 src 的 BBox 缩到 dst, 同时应用 LUT.
void downsampleAndApplyLut(const uint8_t* src, int srcW, int srcH,
                           const uint8_t* lut,
                           QImage& dst,
                           const QRect& srcBBox)
{
    const int dW = dst.width();
    const int dH = dst.height();
    const int bbW = srcBBox.width();
    const int bbH = srcBBox.height();

    // 按 BBox 比例居中 fit
    float scale = std::min(float(dW) / bbW, float(dH) / bbH);
    int useW = (int)std::lround(bbW * scale);
    int useH = (int)std::lround(bbH * scale);
    int xOff = (dW - useW) / 2;
    int yOff = (dH - useH) / 2;

    dst.fill(QColor(60, 60, 60).rgba());
    for (int y = 0; y < useH; ++y) {
        const int sy = (int)std::lround((y + 0.5f) / scale - 0.5f) + srcBBox.y();
        const int sy_c = std::clamp(sy, 0, srcH - 1);
        QRgb* dstRow = (QRgb*)dst.scanLine(yOff + y) + xOff;
        const uint8_t* srcRow = src + (size_t)sy_c * srcW * 4;
        for (int x = 0; x < useW; ++x) {
            const int sx = (int)std::lround((x + 0.5f) / scale - 0.5f) + srcBBox.x();
            const int sx_c = std::clamp(sx, 0, srcW - 1);
            const uint8_t* p = srcRow + sx_c * 4;
            uint8_t r = p[0], g = p[1], b = p[2], a = p[3];
            if (a < 8) continue;
            uint8_t or_, og, ob;
            cpuLut(lut, r, g, b, or_, og, ob);
            const float fa = a / 255.0f;
            uint8_t fr = (uint8_t)std::lround(or_ * fa + 60 * (1 - fa));
            uint8_t fg = (uint8_t)std::lround(og * fa + 60 * (1 - fa));
            uint8_t fb = (uint8_t)std::lround(ob * fa + 60 * (1 - fa));
            dstRow[x] = qRgb(fr, fg, fb);
        }
    }
}

} // namespace

ThumbnailWorker::ThumbnailWorker(QObject* parent) : QThread(parent) {}

ThumbnailWorker::~ThumbnailWorker()
{
    stop();
    wait();
}

void ThumbnailWorker::stop()
{
    QMutexLocker lk(&m_mu);
    m_quit = true;
    m_cv.wakeAll();
}

void ThumbnailWorker::setSource(const uint8_t* rgba, int w, int h)
{
    QRect bbox = findOpaqueBBox(rgba, w, h);
    QMutexLocker lk(&m_mu);
    m_srcRgba = QByteArray((const char*)rgba, w * h * 4);
    m_srcW = w;
    m_srcH = h;
    m_srcBBox = bbox;
}

void ThumbnailWorker::enqueue(int schemeIdx,
                              const std::array<uint8_t, 256*16*4>& lutBytes)
{
    QMutexLocker lk(&m_mu);
    // 合并: 找到同 idx 旧请求, 替换 lut
    bool replaced = false;
    for (auto& j : m_jobs) {
        if (j.idx == schemeIdx) { j.lut = lutBytes; replaced = true; break; }
    }
    if (!replaced) m_jobs.enqueue({ schemeIdx, lutBytes });
    m_cv.wakeOne();
}

void ThumbnailWorker::run()
{
    forever {
        Job job;
        bool hasJob = false;
        {
            QMutexLocker lk(&m_mu);
            while (!m_quit && m_jobs.isEmpty()) m_cv.wait(&m_mu);
            if (m_quit) return;
            if (!m_jobs.isEmpty()) { job = m_jobs.dequeue(); hasJob = true; }
        }
        if (!hasJob) continue;

        QImage img = processOne(job.idx, job.lut);
        if (!img.isNull()) emit thumbnailReady(job.idx, img);
    }
}

QImage ThumbnailWorker::processOne(int schemeIdx,
                                   const std::array<uint8_t, 256*16*4>& lutBytes)
{
    QByteArray src;
    int sw, sh;
    QRect bbox;
    {
        QMutexLocker lk(&m_mu);
        if (m_srcRgba.isEmpty() || m_srcW <= 0 || m_srcH <= 0) return {};
        src = m_srcRgba;
        sw = m_srcW;
        sh = m_srcH;
        bbox = m_srcBBox;
    }
    if (bbox.isEmpty()) bbox = QRect(0, 0, sw, sh);

    QImage out(kThumbW, kThumbH, QImage::Format_ARGB32);
    downsampleAndApplyLut((const uint8_t*)src.constData(), sw, sh,
                          lutBytes.data(), out, bbox);
    return out;
}

} // namespace HighPro
