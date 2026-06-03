#pragma once

#include <QString>
#include <QVector>
#include <QPair>
#include <array>
#include <cstdint>

namespace HighPro {

// 7 个 AE 风格效果的参数模型 (与 effect_chain.hlsl cbuffer 严格对应).
// 顺序固定 (与 AE 标准链一致):
//   1. 色相/饱和度
//   2. 亮度/对比度
//   3. 曲线
//   4. 通道混合器
//   5. 颜色平衡
//   6. 照片滤镜
//   7. 自然饱和度

// === 1. 色相饱和度 ===
struct HslParams
{
    int hue        = 0;     // -180 ~ 180 (degree)
    int saturation = 0;     // -100 ~ 100 (-100=灰阶, +100=2倍)
    int lightness  = 0;     // -100 ~ 100
    void reset() { hue = saturation = lightness = 0; }
    bool isIdentity() const { return hue==0 && saturation==0 && lightness==0; }
};

// === 2. 亮度/对比度 (传统模式, 对齐 AE 经典) ===
struct BrtCtrParams
{
    int brightness = 0;     // -150 ~ 150
    int contrast   = 0;     // -100 ~ 100
    void reset() { brightness = contrast = 0; }
    bool isIdentity() const { return brightness==0 && contrast==0; }
};

// === 3. 曲线 (一期: RGB 主曲线 + R/G/B 三通道曲线, 各自最多 8 控制点) ===
struct CurveParams
{
    using Pts = QVector<QPair<int,int>>;       // [(x,y), ...] 0..255
    Pts master = { {0,0}, {255,255} };
    Pts r      = { {0,0}, {255,255} };
    Pts g      = { {0,0}, {255,255} };
    Pts b      = { {0,0}, {255,255} };
    void reset() {
        master = { {0,0}, {255,255} };
        r = master; g = master; b = master;
    }
    bool isIdentity() const {
        auto isLine = [](const Pts& p){
            return p.size()==2 && p[0].first==0 && p[0].second==0
                   && p[1].first==255 && p[1].second==255;
        };
        return isLine(master) && isLine(r) && isLine(g) && isLine(b);
    }
};

// === 4. 通道混合器 (3x4 仿射: R'/G'/B' = m·(R,G,B,1)) ===
// 单位百分比, 默认单位阵: rr=100, gg=100, bb=100, 其余 0.
struct ChMixParams
{
    int rr = 100, rg = 0,   rb = 0,   r_const = 0;
    int gr = 0,   gg = 100, gb = 0,   g_const = 0;
    int br = 0,   bg = 0,   bb = 100, b_const = 0;
    bool monochrome = false;
    void reset() {
        rr = gg = bb = 100;
        rg = rb = r_const = 0;
        gr = gb = g_const = 0;
        br = bg = b_const = 0;
        monochrome = false;
    }
    bool isIdentity() const {
        return rr==100 && gg==100 && bb==100
            && rg==0 && rb==0 && gr==0 && gb==0 && br==0 && bg==0
            && r_const==0 && g_const==0 && b_const==0
            && !monochrome;
    }
};

// === 5. 颜色平衡 (3 区 × 3 通道) ===
struct ColorBalParams
{
    int sR = 0, sG = 0, sB = 0;   // 阴影
    int mR = 0, mG = 0, mB = 0;   // 中间调
    int hR = 0, hG = 0, hB = 0;   // 高光
    bool preserveLuma = true;
    void reset() {
        sR=sG=sB=mR=mG=mB=hR=hG=hB=0;
        preserveLuma = true;
    }
    bool isIdentity() const {
        return sR==0&&sG==0&&sB==0&&mR==0&&mG==0&&mB==0&&hR==0&&hG==0&&hB==0;
    }
};

// === 6. 照片滤镜 ===
struct PhotoFilterParams
{
    // RGB 0..255 (sRGB), 默认 = 暖色 85
    int filterR = 255, filterG = 187, filterB = 122;
    int density = 25;     // 0..100 (%)
    bool preserveLuma = true;
    int  preset = 0;      // 0 = 暖色85, 1 = 暖色LBA, ... 详见 PRESETS_TABLE
    void reset() {
        filterR = 255; filterG = 187; filterB = 122;
        density = 25;
        preserveLuma = true;
        preset = 0;
    }
    bool isIdentity() const { return density == 0; }
};

// === 7. 自然饱和度 ===
struct VibranceParams
{
    int vibrance   = 0;   // -100 ~ 100
    int saturation = 0;   // -100 ~ 100  (与 #1 区别: 这是均匀加成)
    void reset() { vibrance = saturation = 0; }
    bool isIdentity() const { return vibrance==0 && saturation==0; }
};

// === 7 效果总开关 + 参数集合 ===
struct EffectStack
{
    enum {
        EHsl = 0,
        EBrtCtr,
        ECurves,
        EChMix,
        EColorBal,
        EPhotoFilter,
        EVibrance,
        kCount = 7,
    };
    std::array<bool, kCount> enabled{ {false,false,false,false,false,false,false} };

    HslParams         hsl;
    BrtCtrParams      brtCtr;
    CurveParams       curves;
    ChMixParams       chMix;
    ColorBalParams    colorBal;
    PhotoFilterParams photoFilter;
    VibranceParams    vibrance;

    // 影子保护: src.rgb < 阈值 时输出 = 输入. 0 = 关.
    int shadowProtectThreshold = 8;     // 0..255

    // 重置全部
    void reset() {
        for (auto& e : enabled) e = false;
        hsl.reset(); brtCtr.reset(); curves.reset();
        chMix.reset(); colorBal.reset(); photoFilter.reset(); vibrance.reset();
        shadowProtectThreshold = 8;
    }

    // 是否完全 identity (全 disabled 或全 disabled-equivalent)
    bool isIdentity() const {
        for (size_t i = 0; i < kCount; ++i) {
            if (enabled[i]) {
                switch (i) {
                case EHsl:         if (!hsl.isIdentity())          return false; break;
                case EBrtCtr:      if (!brtCtr.isIdentity())       return false; break;
                case ECurves:      if (!curves.isIdentity())       return false; break;
                case EChMix:       if (!chMix.isIdentity())        return false; break;
                case EColorBal:    if (!colorBal.isIdentity())     return false; break;
                case EPhotoFilter: if (!photoFilter.isIdentity()) return false; break;
                case EVibrance:    if (!vibrance.isIdentity())     return false; break;
                }
            }
        }
        return true;
    }
};

// === 照片滤镜 18 种 AE 预设 ===
struct PhotoFilterPreset { const char* name; int r, g, b; };

// === M7 智能随机 ===
//   对 EffectStack 应用一组随机参数: 7 个效果各有 60% 概率开启,
//   开启后参数在合理范围内随机. 不动 shadowProtectThreshold.
//   seed = 0 表示用全局随机源; 否则用固定种子 (用于"同一方案不同层一致性"等场景, 可空着).
void randomizeStack(EffectStack& s, quint32 seed = 0);
extern const PhotoFilterPreset kPhotoFilterPresets[];
extern const int kPhotoFilterPresetCount;

} // namespace HighPro
