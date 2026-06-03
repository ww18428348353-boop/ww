#include "GifExporter.h"
#include "D3D11Context.h"

#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QDebug>
#include <vector>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <array>

#define GIF_TEMP_MALLOC malloc
#define GIF_TEMP_FREE   free
#define GIF_MALLOC      malloc
#define GIF_FREE        free
#include "gif.h"

// stb_image_write 的 stbi_write_png_to_mem (实现已在 DebugDumper.cpp)
extern "C" unsigned char* stbi_write_png_to_mem(
    const unsigned char* pixels, int stride_bytes,
    int x, int y, int n, int* out_len);

namespace HighPro {

using Microsoft::WRL::ComPtr;

namespace {

// ================================================================
// Wu 颜色量化器 (Xiaolin Wu, 1991)
//   - 32×32×32 RGB 直方图 + 累积矩 (3D 前缀和)
//   - 按"切分后总方差减少最多"贪心切 box (而非按方差大)
//   - 结果优于 median-cut, 接近 PIL libimagequant 质量
//   - 5-bit 直方图量化误差通过最近色查找补偿
// ================================================================

struct Palette256 {
    uint8_t r[256]{};
    uint8_t g[256]{};
    uint8_t b[256]{};
    int     count = 0;
};

// Wu 量化器内部数据
namespace WuQ {
    constexpr int kBits = 5;
    constexpr int kSide = 1 << kBits;        // 32
    constexpr int kSize = kSide * kSide * kSide;  // 32768

    inline int idx(int r, int g, int b) {
        // r,g,b 范围 [0, 32], 索引偏移 1 (0 留作累积和起点)
        return (r * (kSide + 1) + g) * (kSide + 1) + b;
    }

    struct Box {
        int r0, r1, g0, g1, b0, b1;  // 半开区间 (r0, r1] etc.
        int vol;
    };

    // 全局直方图 (累积矩 — 3D 前缀和)
    struct Hist {
        std::vector<int64_t> wt;   // 像素数
        std::vector<int64_t> mr;   // R 累积
        std::vector<int64_t> mg;   // G 累积
        std::vector<int64_t> mb;   // B 累积
        std::vector<double>  m2;   // 平方累积 (用于方差)

        Hist() {
            int n = (kSide + 1) * (kSide + 1) * (kSide + 1);
            wt.assign(n, 0); mr.assign(n, 0); mg.assign(n, 0);
            mb.assign(n, 0); m2.assign(n, 0.0);
        }
    };
}


// 暴力最近色查找 (无感知加权, 纯 R²+G²+B²)
static inline int findNearestColor(const Palette256& pal, int r, int g, int b)
{
    int bestIdx = 0;
    int bestDist = INT_MAX;
    for (int i = 0; i < pal.count; ++i) {
        int dr = r - (int)pal.r[i];
        int dg = g - (int)pal.g[i];
        int db = b - (int)pal.b[i];
        int dist = dr*dr + dg*dg + db*db;
        if (dist < bestDist) {
            bestDist = dist;
            bestIdx = i;
        }
    }
    return bestIdx;
}

// ============================================================
// Wu 量化器: 构建直方图 → 3D 前缀和 → 贪心切分 → 提取调色板
// ============================================================

// 1. 构建 5-bit 直方图
static void wuBuildHist(const uint8_t* rgba, int numPixels, WuQ::Hist& h)
{
    for (int i = 0; i < numPixels; ++i) {
        int r = rgba[i * 4 + 0];
        int g = rgba[i * 4 + 1];
        int b = rgba[i * 4 + 2];
        // 量化到 [1..32] (偏移 1, 让 0 作为累积和基准)
        int ri = (r >> 3) + 1;
        int gi = (g >> 3) + 1;
        int bi = (b >> 3) + 1;
        int k = WuQ::idx(ri, gi, bi);
        h.wt[k] += 1;
        h.mr[k] += r;
        h.mg[k] += g;
        h.mb[k] += b;
        h.m2[k] += (double)(r*r + g*g + b*b);
    }
}

// 2. 3D 累积前缀和
static void wuCumulate(WuQ::Hist& h)
{
    constexpr int S = WuQ::kSide;
    for (int r = 1; r <= S; ++r) {
        std::vector<int64_t> area_wt((S+1), 0), area_mr((S+1), 0),
                              area_mg((S+1), 0), area_mb((S+1), 0);
        std::vector<double> area_m2((S+1), 0.0);
        for (int g = 1; g <= S; ++g) {
            int64_t line_wt = 0, line_mr = 0, line_mg = 0, line_mb = 0;
            double line_m2 = 0;
            for (int b = 1; b <= S; ++b) {
                int k = WuQ::idx(r, g, b);
                line_wt += h.wt[k];
                line_mr += h.mr[k];
                line_mg += h.mg[k];
                line_mb += h.mb[k];
                line_m2 += h.m2[k];
                area_wt[b] += line_wt; area_mr[b] += line_mr;
                area_mg[b] += line_mg; area_mb[b] += line_mb;
                area_m2[b] += line_m2;
                int kp = WuQ::idx(r - 1, g, b);
                h.wt[k] = h.wt[kp] + area_wt[b];
                h.mr[k] = h.mr[kp] + area_mr[b];
                h.mg[k] = h.mg[kp] + area_mg[b];
                h.mb[k] = h.mb[kp] + area_mb[b];
                h.m2[k] = h.m2[kp] + area_m2[b];
            }
        }
    }
}

// 3. box 体积查询 (3D 前缀和 inclusion-exclusion)
#define WU_VOL(h, c, b) \
    ( (h).c[WuQ::idx(b.r1, b.g1, b.b1)] \
    - (h).c[WuQ::idx(b.r1, b.g1, b.b0)] \
    - (h).c[WuQ::idx(b.r1, b.g0, b.b1)] \
    + (h).c[WuQ::idx(b.r1, b.g0, b.b0)] \
    - (h).c[WuQ::idx(b.r0, b.g1, b.b1)] \
    + (h).c[WuQ::idx(b.r0, b.g1, b.b0)] \
    + (h).c[WuQ::idx(b.r0, b.g0, b.b1)] \
    - (h).c[WuQ::idx(b.r0, b.g0, b.b0)] )

static double wuVariance(const WuQ::Hist& h, const WuQ::Box& b)
{
    int64_t w  = WU_VOL(h, wt, b);
    if (w <= 0) return 0.0;
    int64_t sr = WU_VOL(h, mr, b);
    int64_t sg = WU_VOL(h, mg, b);
    int64_t sb = WU_VOL(h, mb, b);
    double  s2 = WU_VOL(h, m2, b);
    return s2 - (double)(sr*sr + sg*sg + sb*sb) / (double)w;
}

// 4. 找某个 box 在某个维度的最优切分位置 (maximize variance reduction)
//    直接用 WU_VOL 构造子 box 查询, 简单可靠.
static bool wuCut(const WuQ::Hist& h, const WuQ::Box& box, int dir,
                  int lo, int hi, int& cutPos, double& maxGain)
{
    int64_t wholeW  = WU_VOL(h, wt, box);
    if (wholeW <= 1) return false;
    int64_t wholeR  = WU_VOL(h, mr, box);
    int64_t wholeG  = WU_VOL(h, mg, box);
    int64_t wholeB  = WU_VOL(h, mb, box);

    maxGain = 0.0;
    bool found = false;

    for (int i = lo; i < hi; ++i) {
        // 构造 bottom sub-box [box.lo..i]
        WuQ::Box bot = box;
        switch (dir) {
        case 0: bot.r1 = i; break;
        case 1: bot.g1 = i; break;
        case 2: bot.b1 = i; break;
        }
        int64_t halfW = WU_VOL(h, wt, bot);
        if (halfW <= 0) continue;
        int64_t halfW2 = wholeW - halfW;
        if (halfW2 <= 0) continue;

        int64_t halfR = WU_VOL(h, mr, bot);
        int64_t halfG = WU_VOL(h, mg, bot);
        int64_t halfB = WU_VOL(h, mb, bot);

        int64_t halfR2 = wholeR - halfR;
        int64_t halfG2 = wholeG - halfG;
        int64_t halfB2 = wholeB - halfB;

        double t1 = (double)(halfR*halfR + halfG*halfG + halfB*halfB) / (double)halfW;
        double t2 = (double)(halfR2*halfR2 + halfG2*halfG2 + halfB2*halfB2) / (double)halfW2;
        double gain = t1 + t2;

        if (gain > maxGain) {
            maxGain = gain;
            cutPos = i;
            found = true;
        }
    }
    return found;
}

// 5. 主量化函数
static Palette256 wuQuantize(const uint8_t* rgba, int numPixels, int maxColors = 256)
{
    Palette256 pal;
    WuQ::Hist h;
    wuBuildHist(rgba, numPixels, h);
    wuCumulate(h);

    std::vector<WuQ::Box> boxes(maxColors);
    std::vector<double> vv(maxColors, 0.0);

    // 初始 box 覆盖全部
    boxes[0].r0 = boxes[0].g0 = boxes[0].b0 = 0;
    boxes[0].r1 = boxes[0].g1 = boxes[0].b1 = WuQ::kSide;
    int next = 1;

    vv[0] = wuVariance(h, boxes[0]);

    for (int i = 1; i < maxColors; ++i) {
        // 找方差最大的 box
        int bestBox = -1;
        double bestVar = 0.0;
        for (int j = 0; j < next; ++j) {
            if (vv[j] > bestVar) { bestVar = vv[j]; bestBox = j; }
        }
        if (bestBox < 0 || bestVar <= 0.0) break;

        WuQ::Box& b = boxes[bestBox];

        // 尝试 3 个轴切分, 取增益最大的
        int cutR = -1, cutG = -1, cutB = -1;
        double gainR = 0, gainG = 0, gainB = 0;
        bool okR = wuCut(h, b, 0, b.r0 + 1, b.r1, cutR, gainR);
        bool okG = wuCut(h, b, 1, b.g0 + 1, b.g1, cutG, gainG);
        bool okB = wuCut(h, b, 2, b.b0 + 1, b.b1, cutB, gainB);

        int bestDir = -1;
        if (okR && gainR >= gainG && gainR >= gainB) bestDir = 0;
        else if (okG && gainG >= gainR && gainG >= gainB) bestDir = 1;
        else if (okB) bestDir = 2;

        if (bestDir < 0) { vv[bestBox] = 0; --i; continue; }

        int cut = (bestDir == 0) ? cutR : (bestDir == 1) ? cutG : cutB;

        // 切分
        WuQ::Box b2 = b;
        switch (bestDir) {
        case 0: b.r1 = cut; b2.r0 = cut; break;
        case 1: b.g1 = cut; b2.g0 = cut; break;
        case 2: b.b1 = cut; b2.b0 = cut; break;
        }
        boxes[next] = b2;
        vv[bestBox] = wuVariance(h, boxes[bestBox]);
        vv[next] = wuVariance(h, boxes[next]);
        ++next;
    }

    // 从每个 box 提取平均色
    pal.count = next;
    for (int i = 0; i < next; ++i) {
        int64_t w = WU_VOL(h, wt, boxes[i]);
        if (w > 0) {
            pal.r[i] = (uint8_t)qBound(0LL, (WU_VOL(h, mr, boxes[i]) + w/2) / w, 255LL);
            pal.g[i] = (uint8_t)qBound(0LL, (WU_VOL(h, mg, boxes[i]) + w/2) / w, 255LL);
            pal.b[i] = (uint8_t)qBound(0LL, (WU_VOL(h, mb, boxes[i]) + w/2) / w, 255LL);
        }
    }
    for (int i = next; i < 256; ++i) pal.r[i] = pal.g[i] = pal.b[i] = 0;
    if (pal.count < 256) pal.count = 256;
    return pal;
}

#undef WU_VOL

// ============================================================
// 逐帧生成独立调色板 (Wu 量化)
// ============================================================
static Palette256 buildFramePaletteHQ(const uint8_t* rgba, int w, int h)
{
    return wuQuantize(rgba, w * h);
}

// ============================================================
// 无抖动直接量化 (精确最近色)
// ============================================================
static void quantizeNoDitherHQ(const uint8_t* rgba, uint8_t* outFrame,
                               uint32_t width, uint32_t height,
                               const Palette256& pal)
{
    uint32_t numPixels = width * height;
    for (uint32_t i = 0; i < numPixels; ++i) {
        int r = rgba[i*4+0], g = rgba[i*4+1], b = rgba[i*4+2];
        int idx = findNearestColor(pal, r, g, b);
        outFrame[i*4+0] = pal.r[idx];
        outFrame[i*4+1] = pal.g[idx];
        outFrame[i*4+2] = pal.b[idx];
        outFrame[i*4+3] = (uint8_t)idx;
    }
}

// ============================================================
// Floyd-Steinberg 抖动 (可选, 默认关闭)
// ============================================================
static void ditherImageFSHQ(const uint8_t* nextFrame, uint8_t* outFrame,
                             uint32_t width, uint32_t height,
                             const Palette256& pal, float strength)
{
    const int numPixels = (int)(width * height);
    strength = qBound(0.0f, strength, 1.0f);

    std::vector<int32_t> qp(numPixels * 4);
    for (int i = 0; i < numPixels * 4; ++i)
        qp[i] = (int32_t)(nextFrame[i]) * 256;

    for (uint32_t yy = 0; yy < height; ++yy) {
        for (uint32_t xx = 0; xx < width; ++xx) {
            int32_t* px = qp.data() + 4 * (yy * width + xx);

            int rr = qBound(0, (int)((px[0] + 127) / 256), 255);
            int gg = qBound(0, (int)((px[1] + 127) / 256), 255);
            int bb = qBound(0, (int)((px[2] + 127) / 256), 255);

            int idx = findNearestColor(pal, rr, gg, bb);

            int32_t r_err = (int32_t)((px[0] - (int32_t)(pal.r[idx]) * 256) * strength);
            int32_t g_err = (int32_t)((px[1] - (int32_t)(pal.g[idx]) * 256) * strength);
            int32_t b_err = (int32_t)((px[2] - (int32_t)(pal.b[idx]) * 256) * strength);

            px[0] = pal.r[idx]; px[1] = pal.g[idx]; px[2] = pal.b[idx]; px[3] = idx;

            auto spread = [&](int dx, int dy, int w_) {
                int nx = (int)xx + dx, ny = (int)yy + dy;
                if (nx >= 0 && nx < (int)width && ny >= 0 && ny < (int)height) {
                    int32_t* p = qp.data() + 4 * (ny * width + nx);
                    p[0] += r_err * w_ / 16; p[1] += g_err * w_ / 16; p[2] += b_err * w_ / 16;
                }
            };
            spread(1, 0, 7); spread(-1, 1, 3); spread(0, 1, 5); spread(1, 1, 1);
        }
    }
    for (int i = 0; i < numPixels * 4; ++i)
        outFrame[i] = (uint8_t)qBound(0, (int)qp[i], 255);
}

// ============================================================
// 对一帧执行量化
// ============================================================
static void quantizeFrameHQ(const uint8_t* rgba, uint8_t* outBuf,
                             uint32_t w, uint32_t h,
                             const Palette256& pal,
                             GifExporter::DitherMode dMode, float ditherStr)
{
    if (dMode == GifExporter::NoDither || ditherStr < 0.01f) {
        quantizeNoDitherHQ(rgba, outBuf, w, h, pal);
    } else {
        ditherImageFSHQ(rgba, outBuf, w, h, pal, ditherStr);
    }
}

// ============================================================
// Palette256 → GifPalette 转换 (LZW 编码仍用 gif.h 的写入器)
// ============================================================
static GifPalette toGifPalette(const Palette256& pal)
{
    GifPalette gp{};
    gp.bitDepth = 8;
    for (int i = 0; i < 256; ++i) {
        gp.r[i] = pal.r[i];
        gp.g[i] = pal.g[i];
        gp.b[i] = pal.b[i];
    }
    return gp;
}

// ============================================================
// 写 GIF 文件头 — 最小 GCT 占位 + NETSCAPE 循环块
// ============================================================
static bool writeGifHeaderMinimal(FILE* f, uint32_t w, uint32_t h, uint32_t delay)
{
    fputs("GIF89a", f);
    fputc(w & 0xff, f);  fputc((w >> 8) & 0xff, f);
    fputc(h & 0xff, f);  fputc((h >> 8) & 0xff, f);
    fputc(0xf0, f);   // GCT=1, res=7, sort=0, size=0 (2色占位)
    fputc(0, f);       // bg color index
    fputc(0, f);       // pixel aspect

    // 最小 GCT: 2 项
    fputc(0, f); fputc(0, f); fputc(0, f);
    fputc(0, f); fputc(0, f); fputc(0, f);

    if (delay != 0) {
        fputc(0x21, f); fputc(0xff, f); fputc(11, f);
        fputs("NETSCAPE2.0", f);
        fputc(3, f); fputc(1, f);
        fputc(0, f); fputc(0, f); fputc(0, f);
    }
    return true;
}

// ============================================================
// LZW 编码核心 (读 indexedFrame[i*4+3] 的 palette index)
// ============================================================
static void writeLzwData(FILE* f, const uint8_t* indexedFrame,
                         uint32_t w, uint32_t h, int bitDepth)
{
    const int minCodeSize = bitDepth;
    const uint32_t clearCode = 1u << bitDepth;
    fputc(minCodeSize, f);

    GifLzwNode* codetree = (GifLzwNode*)GIF_TEMP_MALLOC(sizeof(GifLzwNode) * 4096);
    memset(codetree, 0, sizeof(GifLzwNode) * 4096);
    int32_t curCode = -1;
    uint32_t codeSize = (uint32_t)minCodeSize + 1;
    uint32_t maxCode = clearCode + 1;

    GifBitStatus stat;
    stat.byte = 0; stat.bitIndex = 0; stat.chunkIndex = 0;

    GifWriteCode(f, &stat, clearCode, codeSize);

    for (uint32_t yy = 0; yy < h; ++yy) {
        for (uint32_t xx = 0; xx < w; ++xx) {
            uint8_t nextValue = indexedFrame[(yy * w + xx) * 4 + 3];
            if (curCode < 0) {
                curCode = nextValue;
            } else if (codetree[curCode].m_next[nextValue]) {
                curCode = codetree[curCode].m_next[nextValue];
            } else {
                GifWriteCode(f, &stat, (uint32_t)curCode, codeSize);
                codetree[curCode].m_next[nextValue] = (uint16_t)(++maxCode);
                if (maxCode >= (1ul << codeSize)) ++codeSize;
                if (maxCode == 4095) {
                    GifWriteCode(f, &stat, clearCode, codeSize);
                    memset(codetree, 0, sizeof(GifLzwNode) * 4096);
                    codeSize = (uint32_t)(minCodeSize + 1);
                    maxCode = clearCode + 1;
                }
                curCode = nextValue;
            }
        }
    }
    GifWriteCode(f, &stat, (uint32_t)curCode, codeSize);
    GifWriteCode(f, &stat, clearCode, codeSize);
    GifWriteCode(f, &stat, clearCode + 1, (uint32_t)minCodeSize + 1);

    while (stat.bitIndex) GifWriteBit(&stat, 0);
    if (stat.chunkIndex) GifWriteChunk(f, &stat);

    fputc(0, f);
    GIF_TEMP_FREE(codetree);
}

// ============================================================
// 写一帧 — 带 LCT, disposal=1, 无透明, 不做帧差分
// ============================================================
static void writeFrameWithLCT(FILE* f, const uint8_t* indexedFrame,
                               uint32_t w, uint32_t h, uint32_t delay,
                               const Palette256& pal)
{
    // GCE — disposal=1, no transparency
    fputc(0x21, f); fputc(0xf9, f); fputc(0x04, f);
    fputc(0x04, f);  // disposal=1 (do not dispose)
    fputc(delay & 0xff, f); fputc((delay >> 8) & 0xff, f);
    fputc(0, f); fputc(0, f);

    // Image Descriptor — 带 LCT
    fputc(0x2c, f);
    fputc(0, f); fputc(0, f); fputc(0, f); fputc(0, f);
    fputc(w & 0xff, f); fputc((w >> 8) & 0xff, f);
    fputc(h & 0xff, f); fputc((h >> 8) & 0xff, f);
    fputc(0x87, f);  // LCT=1, interlace=0, sort=0, size=7 (256色)

    // LCT: 256 × 3
    for (int i = 0; i < 256; ++i) {
        fputc(pal.r[i], f); fputc(pal.g[i], f); fputc(pal.b[i], f);
    }

    writeLzwData(f, indexedFrame, w, h, 8);
}

} // anon namespace

GifExporter::Result GifExporter::exportGif(const Options& opts,
                                           int frameCount,
                                           const SetFrameFn&  setFrame,
                                           const RenderRtvFn& renderOnce)
{
    Result r;
    if (opts.outPath.isEmpty()) { r.error = "输出路径为空"; return r; }
    if (opts.width <= 0 || opts.height <= 0) { r.error = "GIF 尺寸无效"; return r; }
    if (frameCount <= 0) { r.error = "帧数 <= 0"; return r; }
    auto* dev = D3D11Context::instance().device();
    auto* dc  = D3D11Context::instance().context();
    if (!dev || !dc) { r.error = "D3D11 设备未就绪"; return r; }

    // === 1) 创建离屏 RTV ===
    ComPtr<ID3D11Texture2D>           rtvTex;
    ComPtr<ID3D11RenderTargetView>    rtv;
    ComPtr<ID3D11Texture2D>           staging;
    {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width      = (UINT)opts.width;
        desc.Height     = (UINT)opts.height;
        desc.MipLevels  = 1;
        desc.ArraySize  = 1;
        desc.Format     = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage      = D3D11_USAGE_DEFAULT;
        desc.BindFlags  = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(dev->CreateTexture2D(&desc, nullptr, rtvTex.GetAddressOf()))) {
            r.error = "CreateTexture2D (offscreen RT) 失败"; return r;
        }
        D3D11_RENDER_TARGET_VIEW_DESC rd{};
        rd.Format        = DXGI_FORMAT_B8G8R8A8_UNORM;
        rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        if (FAILED(dev->CreateRenderTargetView(rtvTex.Get(), &rd, rtv.GetAddressOf()))) {
            r.error = "CreateRenderTargetView 失败"; return r;
        }
        D3D11_TEXTURE2D_DESC sd = desc;
        sd.Usage          = D3D11_USAGE_STAGING;
        sd.BindFlags      = 0;
        sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(dev->CreateTexture2D(&sd, nullptr, staging.GetAddressOf()))) {
            r.error = "CreateTexture2D (staging) 失败"; return r;
        }
    }

    const size_t framePx = (size_t)opts.width * (size_t)opts.height;
    const size_t frameBytes = framePx * 4;

    // === 2) 渲染所有帧 → RGBA 缓存 ===
    const qint64 totalRamMB = (qint64)frameBytes * frameCount / (1024 * 1024);
    if (totalRamMB > 2048) {
        r.error = QString("RAM 需 %1 MB > 2GB, 拒绝执行").arg(totalRamMB);
        return r;
    }

    std::vector<std::vector<uint8_t>> frames;
    frames.reserve(frameCount);
    for (int f = 0; f < frameCount; ++f) {
        setFrame(f);
        renderOnce(rtv.Get(), opts.width, opts.height);

        dc->CopyResource(staging.Get(), rtvTex.Get());
        D3D11_MAPPED_SUBRESOURCE m{};
        if (FAILED(dc->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m))) {
            r.error = QString("Map staging 失败 @ frame %1").arg(f);
            return r;
        }
        std::vector<uint8_t> rgba(frameBytes);
        const uint8_t* src = (const uint8_t*)m.pData;
        for (int y = 0; y < opts.height; ++y) {
            const uint8_t* sr = src + y * m.RowPitch;
            uint8_t*       dr = rgba.data() + (size_t)y * opts.width * 4;
            for (int x = 0; x < opts.width; ++x) {
                dr[x*4 + 0] = sr[x*4 + 2];     // R (BGRA→RGBA)
                dr[x*4 + 1] = sr[x*4 + 1];     // G
                dr[x*4 + 2] = sr[x*4 + 0];     // B
                dr[x*4 + 3] = 255;              // A 不透明
            }
        }
        dc->Unmap(staging.Get(), 0);
        frames.push_back(std::move(rgba));
    }

    // === 3) 抖动参数 ===
    float ditherStr = opts.ditherStrength;
    DitherMode dMode = opts.ditherMode;

    // === 4) 打开临时文件 ===
    const QString tmpEnglish = QDir::tempPath() + "/hplut_gif_tmp.gif";
    FILE* fp = nullptr;
#if defined(_MSC_VER) && (_MSC_VER >= 1400)
    fopen_s(&fp, tmpEnglish.toLocal8Bit().constData(), "wb");
#else
    fp = fopen(tmpEnglish.toLocal8Bit().constData(), "wb");
#endif
    if (!fp) { r.error = QString("打开临时文件失败: %1").arg(tmpEnglish); return r; }

    const uint32_t delayCs = qBound(1, (int)std::round(100.0 / qMax(1, opts.fps)), 100);

    // === DEBUG: 把第一帧渲染结果直接写 PNG, 用于排查 (渲染问题 vs 量化问题) ===
    {
        int outLen = 0;
        unsigned char* png = stbi_write_png_to_mem(
            frames[0].data(), opts.width * 4,
            opts.width, opts.height, 4, &outLen);
        if (png && outLen > 0) {
            QString dbgPath = QDir::tempPath() + "/hplut_gif_dbg_frame0.png";
            QFile f(dbgPath);
            if (f.open(QIODevice::WriteOnly)) {
                f.write((const char*)png, outLen);
                f.close();
                qDebug() << "[GifExporter] DEBUG frame0 PNG dumped:" << dbgPath;
            }
            free(png);
        }
    }


    // === 5) 逐帧独立调色板 + LCT 写入 ===
    //   每帧: medianCutPalette(HQ) → quantizeNoDither(暴力最近色) → writeFrameWithLCT
    //   disposal=1, optimize=false, 无帧差分
    //   完全对齐 PIL: fr.convert('P', palette=Image.ADAPTIVE, colors=256)
    //                  .save(disposal=1, optimize=False)
    writeGifHeaderMinimal(fp, (uint32_t)opts.width, (uint32_t)opts.height, delayCs);

    std::vector<uint8_t> outBuf(frameBytes);
    for (int f = 0; f < frameCount; ++f) {
        // 逐帧独立生成高质量自适应调色板
        Palette256 framePal = buildFramePaletteHQ(frames[f].data(), opts.width, opts.height);

        // 量化 (默认无抖动直接最近色, 可选 FS)
        quantizeFrameHQ(frames[f].data(), outBuf.data(),
                        (uint32_t)opts.width, (uint32_t)opts.height,
                        framePal, dMode, ditherStr);

        // 写帧
        writeFrameWithLCT(fp, outBuf.data(),
                          (uint32_t)opts.width, (uint32_t)opts.height,
                          delayCs, framePal);
        ++r.framesWritten;
    }

    fputc(0x3b, fp);   // GIF trailer
    fclose(fp);

    // === 6) 中转复制到中文路径 ===
    if (QFile::exists(opts.outPath)) QFile::remove(opts.outPath);
    if (!QFile::copy(tmpEnglish, opts.outPath)) {
        r.error = QString("拷贝到目标失败: %1 → %2").arg(tmpEnglish, opts.outPath);
        QFile::remove(tmpEnglish);
        return r;
    }
    QFile::remove(tmpEnglish);

    // === 7) 循环次数控制 ===
    if (opts.loopCount != 0) {
        QFile gf(opts.outPath);
        if (gf.open(QIODevice::ReadWrite)) {
            QByteArray buf = gf.readAll();
            int idx = buf.indexOf("NETSCAPE2.0");
            if (idx >= 0 && idx + 16 < buf.size()) {
                int loopLoPos = idx + 13;
                int loopHiPos = idx + 14;
                int n = qBound(0, opts.loopCount, 0xFFFF);
                buf[loopLoPos] = (char)(n & 0xFF);
                buf[loopHiPos] = (char)((n >> 8) & 0xFF);
                gf.seek(0);
                gf.write(buf);
            }
            gf.close();
        }
    }

    QFileInfo done(opts.outPath);
    r.bytesWritten = done.size();
    r.ok = true;
    return r;
}

} // namespace HighPro
