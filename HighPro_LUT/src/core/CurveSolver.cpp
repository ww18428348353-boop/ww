#include "CurveSolver.h"
#include <algorithm>
#include <cmath>

namespace HighPro {

namespace {

inline uint8_t clamp255(double v)
{
    if (v < 0)   v = 0;
    if (v > 255) v = 255;
    return (uint8_t)std::lround(v);
}

// Fritsch-Carlson 单调三次 Hermite
// 输入: 排序后的 (x, y) 控制点 (x ∈ [0,255]).
// 输出: 256 字节 LUT.
void hermiteFC(const std::vector<std::pair<double,double>>& pts,
               std::array<uint8_t, 256>& out)
{
    const int N = (int)pts.size();
    if (N == 0) {
        for (int i = 0; i < 256; ++i) out[i] = (uint8_t)i;
        return;
    }
    if (N == 1) {
        for (int i = 0; i < 256; ++i) out[i] = clamp255(pts[0].second);
        return;
    }

    // 段斜率
    std::vector<double> d(N - 1);
    for (int k = 0; k < N - 1; ++k) {
        const double dx = pts[k+1].first - pts[k].first;
        d[k] = (dx <= 0) ? 0 : (pts[k+1].second - pts[k].second) / dx;
    }

    // 切线 m
    std::vector<double> m(N);
    m[0] = d[0];
    m[N-1] = d[N-2];
    for (int k = 1; k < N - 1; ++k) {
        if (d[k-1] * d[k] <= 0) {
            m[k] = 0;          // 极值点切平
        } else {
            m[k] = (d[k-1] + d[k]) * 0.5;
        }
    }

    // 单调性修正 (Fritsch-Carlson)
    for (int k = 0; k < N - 1; ++k) {
        if (d[k] == 0) {
            m[k] = 0; m[k+1] = 0; continue;
        }
        const double a = m[k]   / d[k];
        const double b = m[k+1] / d[k];
        const double s = a*a + b*b;
        if (s > 9.0) {
            const double t = 3.0 / std::sqrt(s);
            m[k]   = t * a * d[k];
            m[k+1] = t * b * d[k];
        }
    }

    // 采样 0..255
    int seg = 0;
    for (int x = 0; x < 256; ++x) {
        // 端点夹紧
        if (x <= pts[0].first)        { out[x] = clamp255(pts[0].second); continue; }
        if (x >= pts[N-1].first)      { out[x] = clamp255(pts[N-1].second); continue; }
        // 找段
        while (seg < N - 1 && pts[seg+1].first < x) ++seg;
        const double x0 = pts[seg].first;
        const double x1 = pts[seg+1].first;
        const double y0 = pts[seg].second;
        const double y1 = pts[seg+1].second;
        const double h  = x1 - x0;
        const double t  = (x - x0) / h;
        const double t2 = t * t;
        const double t3 = t2 * t;
        const double h00 =  2*t3 - 3*t2 + 1;
        const double h10 =      t3 - 2*t2 + t;
        const double h01 = -2*t3 + 3*t2;
        const double h11 =      t3 -   t2;
        const double y = h00*y0 + h10*h*m[seg] + h01*y1 + h11*h*m[seg+1];
        out[x] = clamp255(y);
    }
}

} // namespace

void CurveSolver::buildSingleLut(const CurveParams::Pts& ptsIn,
                                 std::array<uint8_t, kSize>& out)
{
    std::vector<std::pair<double,double>> pts;
    pts.reserve(ptsIn.size());
    for (const auto& p : ptsIn) pts.emplace_back((double)p.first, (double)p.second);
    std::sort(pts.begin(), pts.end(),
              [](const auto& a, const auto& b){ return a.first < b.first; });
    // 端点强制夹紧
    if (!pts.empty()) {
        if (pts.front().first > 0)
            pts.insert(pts.begin(), { 0.0, pts.front().second });
        if (pts.back().first < 255)
            pts.push_back({ 255.0, pts.back().second });
    }
    hermiteFC(pts, out);
}

void CurveSolver::buildLut(const CurveParams& p, std::array<uint8_t, kSize * 4>& out)
{
    std::array<uint8_t, 256> lutM, lutR, lutG, lutB;
    buildSingleLut(p.master, lutM);
    buildSingleLut(p.r,      lutR);
    buildSingleLut(p.g,      lutG);
    buildSingleLut(p.b,      lutB);
    for (int i = 0; i < 256; ++i) {
        out[i*4 + 0] = lutM[i];   // R 通道存 master
        out[i*4 + 1] = lutR[i];
        out[i*4 + 2] = lutG[i];
        out[i*4 + 3] = lutB[i];
    }
}

} // namespace HighPro
