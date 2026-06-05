#include "CurveSolver.h"
#include <algorithm>
#include <cmath>

namespace HighPro {

namespace {

inline uint8_t clamp255u8(double v)
{
    if (v < 0)   v = 0;
    if (v > 255) v = 255;
    return (uint8_t)std::lround(v);
}

// 在已 prepare 的 spline 上, 对单点 x ∈ [0,255] 求值, 返回浮点 y ∈ [0,255].
// seg 是 caller 维护的段游标 (单调递增 x 时可加速; 不要求严格单调也能工作).
double evalAt(const CurveSolver::Spline& s, double x, int& seg)
{
    const auto& pts = s.pts;
    const auto& m   = s.m;
    const int N = (int)pts.size();
    if (N == 0) return x;
    if (N == 1) return pts[0].second;

    if (x <= pts.front().first) return pts.front().second;
    if (x >= pts.back().first)  return pts.back().second;

    // 段游标推进 (允许回退一格, 不强制单调)
    if (seg < 0) seg = 0;
    if (seg >= N - 1) seg = N - 2;
    while (seg < N - 1 && pts[seg + 1].first < x) ++seg;
    while (seg > 0      && pts[seg].first      > x) --seg;

    const double x0 = pts[seg].first;
    const double x1 = pts[seg + 1].first;
    const double y0 = pts[seg].second;
    const double y1 = pts[seg + 1].second;
    const double h  = x1 - x0;
    if (h <= 0) return y0;
    const double t  = (x - x0) / h;
    const double t2 = t * t;
    const double t3 = t2 * t;
    const double h00 =  2*t3 - 3*t2 + 1;
    const double h10 =      t3 - 2*t2 + t;
    const double h01 = -2*t3 + 3*t2;
    const double h11 =      t3 -   t2;
    return h00*y0 + h10*h*m[seg] + h01*y1 + h11*h*m[seg + 1];
}

} // namespace

CurveSolver::Spline CurveSolver::prepare(const CurveParams::Pts& ptsIn)
{
    Spline s;
    s.pts.reserve(ptsIn.size() + 2);
    for (const auto& p : ptsIn) s.pts.emplace_back((double)p.first, (double)p.second);
    std::sort(s.pts.begin(), s.pts.end(),
              [](const auto& a, const auto& b){ return a.first < b.first; });
    // 端点强制夹紧 (沿用原 buildSingleLut 行为)
    if (!s.pts.empty()) {
        if (s.pts.front().first > 0)
            s.pts.insert(s.pts.begin(), { 0.0, s.pts.front().second });
        if (s.pts.back().first < 255)
            s.pts.push_back({ 255.0, s.pts.back().second });
    }

    const int N = (int)s.pts.size();
    s.m.assign(N, 0.0);
    if (N < 2) return s;

    // 段斜率
    std::vector<double> d(N - 1);
    for (int k = 0; k < N - 1; ++k) {
        const double dx = s.pts[k + 1].first - s.pts[k].first;
        d[k] = (dx <= 0) ? 0.0 : (s.pts[k + 1].second - s.pts[k].second) / dx;
    }

    // 切线
    s.m[0]     = d[0];
    s.m[N - 1] = d[N - 2];
    for (int k = 1; k < N - 1; ++k) {
        if (d[k - 1] * d[k] <= 0) s.m[k] = 0.0;            // 极值点切平
        else                       s.m[k] = (d[k - 1] + d[k]) * 0.5;
    }

    // 单调性修正 (Fritsch-Carlson)
    for (int k = 0; k < N - 1; ++k) {
        if (d[k] == 0.0) {
            s.m[k] = 0.0; s.m[k + 1] = 0.0; continue;
        }
        const double a = s.m[k]     / d[k];
        const double b = s.m[k + 1] / d[k];
        const double sumSq = a * a + b * b;
        if (sumSq > 9.0) {
            const double t = 3.0 / std::sqrt(sumSq);
            s.m[k]     = t * a * d[k];
            s.m[k + 1] = t * b * d[k];
        }
    }

    return s;
}

double CurveSolver::evaluate(const Spline& s, double x)
{
    int seg = 0;
    return evalAt(s, x, seg);
}

void CurveSolver::buildSingleLut(const CurveParams::Pts& ptsIn,
                                 std::array<uint8_t, kSize>& out)
{
    const Spline s = prepare(ptsIn);
    int seg = 0;
    for (int x = 0; x < 256; ++x) {
        out[x] = clamp255u8(evalAt(s, (double)x, seg));
    }
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
