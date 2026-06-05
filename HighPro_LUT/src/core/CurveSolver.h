#pragma once

#include "ColorEffect.h"
#include <array>
#include <cstdint>
#include <vector>
#include <utility>

namespace HighPro {

// 把 4 条曲线 (master/R/G/B) 编译为 256x4 的 LUT 表
// (供 effect_chain.hlsl 通过 t1 纹理采样).
//
// 算法: Fritsch-Carlson 单调三次 Hermite (与 py 实现一致).
// 输出: r/g/b 三通道独立 LUT (每个 256 字节)
//        以及一个综合 master LUT (用作 RGB 共同曲线)
//        最终返回 256 行 × 4 列 (R8G8B8A8) 的字节数组,
//        其中 .r=master, .g=channel R, .b=channel G, .a=channel B (按 shader 约定).
class CurveSolver
{
public:
    static constexpr int kSize = 256;

    // 预编译后的曲线 (sorted 控制点 + 单调修正切线), 供高密浮点采样复用.
    // UI 侧应一次 prepare, 循环 evaluate, 避免每像素都排序/求切线.
    struct Spline
    {
        std::vector<std::pair<double, double>> pts;  // x ∈ [0,255], 已含端点夹紧
        std::vector<double> m;                       // 长度 == pts.size()
    };

    // 把控制点编译成 Spline (排序 + 端点夹紧 + Fritsch-Carlson 切线).
    static Spline prepare(const CurveParams::Pts& pts);

    // 在已编译的 Spline 上做浮点求值: x ∈ [0,255], 返回 [0,255] (不夹紧到 uint8).
    // 用于 UI 高密绘制, 不带 8-bit 量化.
    static double evaluate(const Spline& s, double x);

    // 输出 size = 256*4 字节
    static void buildLut(const CurveParams& p, std::array<uint8_t, kSize * 4>& out);

    // 单条曲线 → 256-LUT (供 master / R / G / B 使用)
    static void buildSingleLut(const CurveParams::Pts& pts,
                               std::array<uint8_t, kSize>& out);
};

} // namespace HighPro
