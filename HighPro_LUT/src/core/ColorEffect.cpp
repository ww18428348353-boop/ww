#include "ColorEffect.h"
#include "core/LayerData.h"
#include "core/SchemePalette.h"

#include <QRandomGenerator>
#include <random>
#include <algorithm>

namespace HighPro {

const PhotoFilterPreset kPhotoFilterPresets[] = {
    { "暖色 85",   255, 187, 122 },
    { "暖色 LBA",  255, 218, 144 },
    { "暖色 81",   235, 180, 134 },
    { "冷色 80",   107, 175, 224 },
    { "冷色 LBB",  140, 198, 245 },
    { "冷色 82",   162, 214, 230 },
    { "红",        234,  26,  35 },
    { "橙红",      235,  89,  49 },
    { "黄",        240, 232,  84 },
    { "绿",         54, 199, 128 },
    { "青",         56, 195, 209 },
    { "蓝",         37,  94, 217 },
    { "紫",        126,  56, 187 },
    { "品红",      224,  79, 173 },
    { "棕褐",      175, 117,  79 },
    { "深蓝",       30,  60, 145 },
    { "翡翠绿",     15, 130,  70 },
    { "深黄",      243, 180,  31 },
    { "水下",       40, 130, 195 },
};

const int kPhotoFilterPresetCount =
    (int)(sizeof(kPhotoFilterPresets) / sizeof(kPhotoFilterPresets[0]));

void randomizeStack(EffectStack& s, quint32 seed)
{
    std::mt19937 rng(seed != 0 ? seed : QRandomGenerator::global()->generate());
    auto chance = [&](int pct) {
        std::uniform_int_distribution<int> d(0, 99);
        return d(rng) < pct;
    };
    auto rint = [&](int lo, int hi) {
        std::uniform_int_distribution<int> d(lo, hi);
        return d(rng);
    };

    const int savedThr = s.shadowProtectThreshold;
    s.reset();
    s.shadowProtectThreshold = savedThr;

    // === 风格档位 (写实游戏配色, 大部分情况低饱和) ===
    //   70% 写实  : 微调色相, 饱和度大幅下压, 接近原色
    //   20% 暗色  : 压低亮度+大幅脱色, 衣服偏暗灰
    //   8%  自然  : 略带饱和的写实
    //   2%  鲜艳  : 高饱和创意色 (偶发)
    int roll = rint(0, 99);
    enum Style { Realistic, Dark, Natural, Vivid } style;
    if (roll < 70)       style = Realistic;
    else if (roll < 90)  style = Dark;
    else if (roll < 98)  style = Natural;
    else                 style = Vivid;

    // 1. HSL — 必开. 饱和度按风格档分级
    s.enabled[EffectStack::EHsl] = true;
    s.hsl.hue = rint(-180, 180);
    switch (style) {
    case Realistic:
        s.hsl.saturation = rint(-25, 0);
        s.hsl.lightness  = rint(-8, 8);
        break;
    case Dark:
        s.hsl.saturation = rint(-50, -20);
        s.hsl.lightness  = rint(-30, -10);
        break;
    case Natural:
        s.hsl.saturation = rint(-15, 10);
        s.hsl.lightness  = rint(-10, 10);
        break;
    case Vivid:
        s.hsl.saturation = rint(15, 40);
        s.hsl.lightness  = rint(-15, 15);
        break;
    }

    // 2. 亮度对比度 — 暗色档强制开 (压暗+提对比), 其他 30% 开
    if (style == Dark || chance(30)) {
        s.enabled[EffectStack::EBrtCtr] = true;
        if (style == Dark) {
            s.brtCtr.brightness = rint(-25, -5);
            s.brtCtr.contrast   = rint(5, 25);
        } else {
            s.brtCtr.brightness = rint(-12, 10);
            s.brtCtr.contrast   = (style == Vivid) ? rint(0, 25) : rint(-10, 12);
        }
    }

    // 3. 曲线 — 15% 开 (避免与亮度叠加)
    if (chance(15)) {
        s.enabled[EffectStack::ECurves] = true;
        const int midX = rint(112, 144);
        const int midY = rint(108, 148);
        s.curves.master = { {0,0}, {midX, midY}, {255,255} };
    }

    // 4. 通道混合 — 不开 (容易出怪色, 写实游戏不用)

    // 5. 颜色平衡 — Realistic/Dark 30%, 其他 50%; 三区错开主轴防共振染色
    int balProb = (style == Realistic || style == Dark) ? 30 : 50;
    if (chance(balProb)) {
        s.enabled[EffectStack::EColorBal] = true;
        const int axes[3] = { rint(0, 2), rint(0, 2), rint(0, 2) };
        const int signs[3] = { (chance(50) ? -1 : 1),
                                (chance(50) ? -1 : 1),
                                (chance(50) ? -1 : 1) };
        const int amp = (style == Vivid) ? 22 : (style == Dark ? 8 : 12);
        int* triples[3][3] = {
            { &s.colorBal.sR, &s.colorBal.sG, &s.colorBal.sB },
            { &s.colorBal.mR, &s.colorBal.mG, &s.colorBal.mB },
            { &s.colorBal.hR, &s.colorBal.hG, &s.colorBal.hB },
        };
        for (int z = 0; z < 3; ++z) {
            *triples[z][axes[z]] = signs[z] * rint(amp / 2, amp);
            for (int j = 0; j < 3; ++j) if (j != axes[z]) *triples[z][j] = rint(-amp/4, amp/4);
        }
        s.colorBal.preserveLuma = true;
    }

    // 6. 照片滤镜 — Realistic/Dark 30%, 其他 15%
    int photoProb = (style == Realistic || style == Dark) ? 30 : 15;
    if (chance(photoProb)) {
        s.enabled[EffectStack::EPhotoFilter] = true;
        int idx;
        if (style == Dark) {
            // 暗色档专用: 冷蓝/深色滤镜
            const int darkPool[] = { 3, 4, 5, 15, 16, 17, 18 };
            idx = darkPool[rint(0, (int)(sizeof(darkPool)/sizeof(int)) - 1)];
        } else if (style == Realistic) {
            idx = rint(0, 5);
        } else {
            idx = rint(0, kPhotoFilterPresetCount - 1);
        }
        s.photoFilter.preset  = idx;
        s.photoFilter.filterR = kPhotoFilterPresets[idx].r;
        s.photoFilter.filterG = kPhotoFilterPresets[idx].g;
        s.photoFilter.filterB = kPhotoFilterPresets[idx].b;
        s.photoFilter.density = (style == Dark) ? rint(20, 40) : rint(10, 25);
        s.photoFilter.preserveLuma = true;
    }

    // 7. 自然饱和度 — Realistic/Dark 必开 (用来再次降 sat), 其他 30%
    if (style == Realistic || style == Dark || chance(30)) {
        s.enabled[EffectStack::EVibrance] = true;
        switch (style) {
        case Realistic:
            s.vibrance.vibrance   = rint(-15, 5);
            s.vibrance.saturation = rint(-20, -5);
            break;
        case Dark:
            s.vibrance.vibrance   = rint(-25, -5);
            s.vibrance.saturation = rint(-30, -10);
            break;
        case Natural:
            s.vibrance.vibrance   = rint(-5, 15);
            s.vibrance.saturation = rint(-10, 5);
            break;
        case Vivid:
            s.vibrance.vibrance   = rint(5, 25);
            s.vibrance.saturation = rint(0, 15);
            break;
        }
    }

    // 兜底
    bool any = false;
    for (size_t i = 0; i < EffectStack::kCount; ++i) if (s.enabled[i]) { any = true; break; }
    if (!any) {
        s.enabled[EffectStack::EHsl] = true;
        s.hsl.hue = rint(-180, 180);
        s.hsl.saturation = rint(-15, 0);
    }
}

// ===========================================================================
// P0 智能随机 (Palette + LayerSlot 驱动)
// ===========================================================================
namespace {

// 内部 rng 工具
struct Rng {
    std::mt19937 r;
    explicit Rng(quint32 seed)
        : r(seed != 0 ? seed : QRandomGenerator::global()->generate()) {}
    int   irange(int lo, int hi)        { std::uniform_int_distribution<int> d(lo, hi); return d(r); }
    bool  chance(int pct)               { return irange(0, 99) < pct; }
    int   sign()                        { return chance(50) ? -1 : 1; }
    quint32 nextSeed()                  { return static_cast<quint32>(r()); }
};

// hue 0..359 → AE HSL hue -180..180 (相对原图色相的偏移).
//   不知道原图主色相, P0 直接把 palette hue 作为绝对目标 hue 经偏移近似.
//   实测: 原图饱和色经 hsl.hue=N 旋转会绕色相轮, 这里给"色相绝对目标"近似实现:
//   把 [0,359] 映射到 [-180, 180].
int hueAbsToShift(int hueAbs)
{
    int h = ((hueAbs % 360) + 360) % 360;
    if (h > 180) h -= 360;     // → [-180, 179]
    return h;
}

// 通用准备工作: 保留 shadowProtectThreshold, reset, 打开 HSL.
void prepStack(EffectStack& s)
{
    const int shadow = s.shadowProtectThreshold;
    s.reset();
    s.shadowProtectThreshold = shadow;
    s.enabled[EffectStack::EHsl] = true;
}

// === v10: palette bias 编码解码 ===
// palette.lightnessBias / saturationBias 字段被复用为"档位 + jitter"编码:
//   bias = level * 100 + jitter (jitter ∈ [-9, +9])
//   level 含义见各 enum 注释
// 兼容旧值: |bias| < 50 时视为 jitter-only, 默认 Mid 档.
enum class LgtLevelInternal { Grayscale = 0, UltraDark = 1, Dark = 2, Mid = 3, Bright = 4, UltraBright = 5 };
enum class SatLevelInternal { Low = 0, Mid = 1, High = 2 };

LgtLevelInternal decodeLgtLevel(int bias) {
    if (std::abs(bias) < 50) return LgtLevelInternal::Mid;
    int lvl = bias / 100;
    if (lvl <= 0) return LgtLevelInternal::Grayscale;
    if (lvl == 1) return LgtLevelInternal::UltraDark;
    if (lvl == 2) return LgtLevelInternal::Dark;
    if (lvl == 3) return LgtLevelInternal::Mid;
    if (lvl == 4) return LgtLevelInternal::Bright;
    return LgtLevelInternal::UltraBright;
}
SatLevelInternal decodeSatLevel(int bias) {
    if (std::abs(bias) < 50) return SatLevelInternal::Mid;
    int lvl = bias / 100;
    return (lvl <= 0) ? SatLevelInternal::Low
         : (lvl == 1) ? SatLevelInternal::Mid
                      : SatLevelInternal::High;
}
int decodeJitter(int bias) {
    if (std::abs(bias) < 50) return bias;
    return bias % 100;
}

// === P0 v8: 服装专用曲线 (针对"黑不够黑 / 白不够白") ===
//
// DeepSafe 曲线 v3: 黑位拉更低 0→6 (v2 0→10), 中调更沉
//   关键: 原图深色像素经 HSL 旋转 + ColorBalance 偏色后, 还需要曲线把暗部彻底拍下去.
CurveParams::Pts makeDeepSafeCurve() {
    return CurveParams::Pts{
        {0, 6}, {32, 18}, {80, 58}, {128, 108}, {190, 196}, {230, 232}, {255, 246}
    };
}

// HighlightSafe 曲线 v3: 高光顶到 255→253 (v2 255→248), 亮部更亮
//   原图浅色像素 + 亮档 lgt 推高 + Vibrance, 加这条曲线后高光区接近纯白.
CurveParams::Pts makeHighlightSafeCurve() {
    return CurveParams::Pts{
        {0, 12}, {48, 52}, {128, 144}, {200, 224}, {240, 246}, {255, 253}
    };
}

// SoftContrast 曲线: 中档用 — 加 S 曲线对比, 让中调有层次
CurveParams::Pts makeSoftContrastCurve() {
    return CurveParams::Pts{
        {0, 4}, {64, 50}, {128, 130}, {192, 206}, {255, 252}
    };
}

// 极暗专用曲线 v8 新增: 黑位拍到 0→2, 中调狠压, 高光保留
//   用于"极暗"档 (18% 概率), 黑色服装、暗黑哥特方案
CurveParams::Pts makeUltraDarkCurve() {
    return CurveParams::Pts{
        {0, 2}, {32, 10}, {80, 42}, {128, 88}, {190, 178}, {230, 220}, {255, 240}
    };
}

// 极亮专用曲线 v9 收敛: 高光不再顶到 255, 保留 255→242 防过曝
//   v8 0→24, 255→255 过头 → v9 0→18, 255→242 (跟 HighlightSafe 相近, 更克制)
CurveParams::Pts makeUltraBrightCurve() {
    return CurveParams::Pts{
        {0, 18}, {48, 62}, {128, 152}, {200, 222}, {240, 238}, {255, 242}
    };
}

// === ColorBalance helper: P0 v10 降幅度 (整体降饱 -30 后, 偏色也要降, 避免违和) ===
//
// v8→v10 幅度对比:
//   阴影/中调 主轴偏移 v8 max +32 → v10 max +22
//   高光保留          v8 max +14 → v10 max +10
//   黑灰              v8 max +14 → v10 max +8 (黑白灰要更克制)
void applyColorBalanceForClothing(EffectStack& s, ClothingTone tone, Rng& rng)
{
    s.enabled[EffectStack::EColorBal] = true;
    s.colorBal.preserveLuma = true;
    auto rint = [&](int lo, int hi) { return rng.irange(lo, hi); };

    switch (tone) {
    case ClothingTone::DarkBlue:
        s.colorBal.sR = rint(-6, -1);  s.colorBal.sG = rint(-3, 1);   s.colorBal.sB = rint(12, 22);
        s.colorBal.mR = rint(-4, 0);   s.colorBal.mG = rint(-2, 2);   s.colorBal.mB = rint(8, 16);
        s.colorBal.hR = rint(-2, 1);   s.colorBal.hG = rint(0, 3);    s.colorBal.hB = rint(4, 10);
        break;
    case ClothingTone::DarkRed:
        s.colorBal.sR = rint(12, 22);  s.colorBal.sG = rint(-6, -1);  s.colorBal.sB = rint(-3, 4);
        s.colorBal.mR = rint(10, 18);  s.colorBal.mG = rint(-4, 0);   s.colorBal.mB = rint(-3, 1);
        s.colorBal.hR = rint(4, 10);   s.colorBal.hG = rint(0, 3);    s.colorBal.hB = rint(-3, 1);
        break;
    case ClothingTone::DarkPurple:
        s.colorBal.sR = rint(8, 16);   s.colorBal.sG = rint(-6, -1);  s.colorBal.sB = rint(10, 20);
        s.colorBal.mR = rint(6, 12);   s.colorBal.mG = rint(-4, 0);   s.colorBal.mB = rint(8, 16);
        s.colorBal.hR = rint(3, 8);    s.colorBal.hG = rint(-2, 1);   s.colorBal.hB = rint(4, 9);
        break;
    case ClothingTone::DarkGreen:
        s.colorBal.sR = rint(-4, 0);   s.colorBal.sG = rint(10, 20);  s.colorBal.sB = rint(-3, 3);
        s.colorBal.mR = rint(-3, 0);   s.colorBal.mG = rint(8, 16);   s.colorBal.mB = rint(-3, 1);
        s.colorBal.hR = rint(-2, 1);   s.colorBal.hG = rint(3, 8);    s.colorBal.hB = rint(-2, 1);
        break;
    case ClothingTone::DeepTeal:
        s.colorBal.sR = rint(-6, -1);  s.colorBal.sG = rint(5, 12);   s.colorBal.sB = rint(8, 16);
        s.colorBal.mR = rint(-4, 0);   s.colorBal.mG = rint(4, 10);   s.colorBal.mB = rint(6, 12);
        s.colorBal.hR = rint(-2, 1);   s.colorBal.hG = rint(2, 6);    s.colorBal.hB = rint(3, 8);
        break;
    case ClothingTone::DarkGray:
        // 黑灰: 弱偏冷
        s.colorBal.sR = rint(-3, 0);   s.colorBal.sG = rint(-2, 1);   s.colorBal.sB = rint(3, 8);
        s.colorBal.mR = rint(-2, 0);   s.colorBal.mG = rint(-1, 1);   s.colorBal.mB = rint(2, 6);
        s.colorBal.hR = rint(-1, 1);   s.colorBal.hG = 0;             s.colorBal.hB = rint(0, 3);
        break;
    case ClothingTone::DarkBrown:
        s.colorBal.sR = rint(5, 12);   s.colorBal.sG = rint(2, 7);    s.colorBal.sB = rint(-6, -1);
        s.colorBal.mR = rint(4, 10);   s.colorBal.mG = rint(2, 6);    s.colorBal.mB = rint(-4, 0);
        s.colorBal.hR = rint(2, 6);    s.colorBal.hG = rint(1, 4);    s.colorBal.hB = rint(-3, 0);
        break;
    case ClothingTone::LightNoble:
        s.colorBal.sR = rint(-3, 1);   s.colorBal.sG = rint(-2, 2);   s.colorBal.sB = rint(3, 7);
        s.colorBal.mR = rint(0, 4);    s.colorBal.mG = rint(0, 3);    s.colorBal.mB = rint(-3, 1);
        s.colorBal.hR = rint(2, 7);    s.colorBal.hG = rint(1, 4);    s.colorBal.hB = rint(-4, 0);
        break;
    case ClothingTone::RareVivid:
        s.colorBal.sR = rint(-4, 4);   s.colorBal.sG = rint(-4, 4);   s.colorBal.sB = rint(-4, 4);
        s.colorBal.mR = rint(-8, 8);   s.colorBal.mG = rint(-8, 8);   s.colorBal.mB = rint(-8, 8);
        s.colorBal.hR = rint(-4, 4);   s.colorBal.hG = rint(-4, 4);   s.colorBal.hB = rint(-4, 4);
        break;
    }
}

// === Skin: 不变色, 直接 reset (所有 effect disabled) ===
void applySkinReset(EffectStack& s)
{
    const int shadow = s.shadowProtectThreshold;
    s.reset();
    s.shadowProtectThreshold = shadow;
    // 全 disabled, isIdentity() == true.
}

// === Hair: secondaryHue ±18°, 低饱和保明度 (回滚到 v2 简单版 + v11 暗档 +20%) ===
//
// v10/v11 改动撤回原因: 接 v10 6 档系统后效果反而不好.
// 现在: 用回 v2 的简单逻辑 (固定 sat/lgt 区间), 不读 palette bias.
// v11 新增: 30% 概率走"暗发"分支 (lgt -10..-2), 给暗系角色用.
void applyHair(EffectStack& s, const SchemePalette& p, Rng& rng)
{
    prepStack(s);
    const int hueAbs = (p.secondaryHue + rng.irange(-18, 18) + 360) % 360;
    s.hsl.hue        = hueAbsToShift(hueAbs);

    // 30% 暗发分支 (v11 新增, 给暗系角色 +20% 暗色头发)
    const bool darkHair = rng.chance(30);

    if (darkHair) {
        s.hsl.saturation = std::clamp(rng.irange(-15, 0),   -100, 100);
        s.hsl.lightness  = std::clamp(rng.irange(-12, -2),  -14, 12);
    } else {
        // 普通: v2 简单区间, sat 微负, lgt 微正
        s.hsl.saturation = std::clamp(rng.irange(-12, 5),   -100, 100);
        s.hsl.lightness  = std::clamp(rng.irange(-3, 6),    -6, 12);
    }

    // 低概率开 Vibrance 降饱 (头发不要抢戏)
    if (rng.chance(50)) {
        s.enabled[EffectStack::EVibrance] = true;
        s.vibrance.vibrance   = rng.irange(-8, 0);
        s.vibrance.saturation = rng.irange(-10, -2);
    }
}

// === Clothing: P0 v7 — 全色相覆盖 + sat/lgt 高方差 ===
//
// v7 改进 (针对用户诉求 "色相全 360° + 亮度 2-99% + 饱和度 10-80% + 色系丰富"):
//   - 不再按 ClothingTone 强绑暗色, palette 已用色相桶均匀抽
//   - 亮度档 / 饱和档 已编码在 palette.lightnessBias / saturationBias 字段中:
//        bias / 100 = 档位 (0=暗, 1=中, 2=亮 / 0=低, 1=中, 2=高)
//        bias % 100 = jitter (-9..+9)
//   - 按 (lgtLevel, satLevel) 9 个组合各自有独立的 hsl/curve 配置
//   - 曲线: 暗档 DeepSafe, 亮档 HighlightSafe, 中档 SoftContrast (防死黑/曝白底线)
//   - 安全硬限: lgt ∈ [-30, +24], sat ∈ [-50, +55] (覆盖 hsv 2-99% / 10-80%)
//
// ClothingTone 现在仅用于:
//   - PhotoFilter 方向选择 (红桶用红滤镜, 绿桶用绿滤镜...)
//   - ColorBalance 偏移方向 (让色相旋转后真的偏暗蓝/暗红)
//   - 不再强制 sat/lgt 区间
// (LgtLevelInternal / SatLevelInternal / decodeXxx 已前移到第一个匿名 ns 中,
//  供 applyHair / applyDecor01 / applyDecor02 等使用)

void applyClothing(EffectStack& s, const SchemePalette& p, Rng& rng)
{
    prepStack(s);

    const LgtLevelInternal lgtLvl = decodeLgtLevel(p.lightnessBias);

    // ============ v10: Grayscale 档完全独立处理 (彻底去色) ============
    if (lgtLvl == LgtLevelInternal::Grayscale) {
        // hue 不重要 (sat=-100 后任何 hue 都是灰)
        s.hsl.hue        = 0;
        s.hsl.saturation = -100;
        // lightness 跨大范围: 深灰 -25..-10 (40%) / 中灰 -8..+8 (30%) / 亮白灰 +10..+25 (30%)
        int subRoll = rng.irange(0, 99);
        int lgt = 0;
        if (subRoll < 40)      lgt = rng.irange(-25, -10);
        else if (subRoll < 70) lgt = rng.irange(-8, 8);
        else                   lgt = rng.irange(10, 25);
        s.hsl.lightness = std::clamp(lgt, -28, 26);

        // brtCtr 跟随
        s.enabled[EffectStack::EBrtCtr] = true;
        if (lgt < -8) {
            s.brtCtr.brightness = std::clamp(rng.irange(-12, -3), -25, 15);
            s.brtCtr.contrast   = rng.irange(10, 22);
        } else if (lgt > 8) {
            s.brtCtr.brightness = std::clamp(rng.irange(2, 8), -25, 10);
            s.brtCtr.contrast   = rng.irange(0, 8);
        } else {
            s.brtCtr.brightness = std::clamp(rng.irange(-3, 3), -25, 15);
            s.brtCtr.contrast   = rng.irange(4, 16);
        }

        // 曲线按 lightness 选
        s.enabled[EffectStack::ECurves] = true;
        if (lgt < -8)      s.curves.master = makeDeepSafeCurve();
        else if (lgt > 8)  s.curves.master = makeHighlightSafeCurve();
        else               s.curves.master = makeSoftContrastCurve();

        // Vibrance 强降饱 (双保险)
        s.enabled[EffectStack::EVibrance] = true;
        s.vibrance.vibrance   = rng.irange(-25, -10);
        s.vibrance.saturation = rng.irange(-30, -15);

        // 不开 ColorBalance / PhotoFilter (任何染色都破坏纯灰)
        return;
    }

    // ============ 非 Grayscale 档: 正常流程 ============
    const int hueAbs = (p.primaryHue + rng.irange(-8, 8) + 360) % 360;
    s.hsl.hue        = hueAbsToShift(hueAbs);

    SatLevelInternal satLvl = decodeSatLevel(p.saturationBias);
    const int lgtJit = decodeJitter(p.lightnessBias);
    const int satJit = decodeJitter(p.saturationBias);

    // v9: 亮档强制不抽 Low sat 档. 亮 + 低饱 = 纯白过曝失体感.
    if ((lgtLvl == LgtLevelInternal::Bright || lgtLvl == LgtLevelInternal::UltraBright)
        && satLvl == SatLevelInternal::Low) {
        satLvl = SatLevelInternal::Mid;
    }

    // ============ hsl.lightness 5 档 ============
    int lgtTarget = 0;
    switch (lgtLvl) {
    case LgtLevelInternal::UltraDark:   lgtTarget = rng.irange(-40, -22); break;
    case LgtLevelInternal::Dark:        lgtTarget = rng.irange(-22, -8);  break;
    case LgtLevelInternal::Mid:         lgtTarget = rng.irange(-6, 8);    break;
    case LgtLevelInternal::Bright:      lgtTarget = rng.irange(4, 14);    break;
    case LgtLevelInternal::UltraBright: lgtTarget = rng.irange(14, 24);   break;
    default: lgtTarget = 0; break;
    }
    s.hsl.lightness = std::clamp(lgtTarget + lgtJit / 3, -45, 28);

    // ============ hsl.saturation v11: 中档再 -10, 高档上限再 -20 ============
    // v10 → v11:
    //   Mid:  -30..-5  → -40..-15  (整体下移 10)
    //   High: -5..+25  → -5..+5    (上限砍 20)
    int satTarget = 0;
    switch (satLvl) {
    case SatLevelInternal::Low:  satTarget = rng.irange(-60, -35); break;
    case SatLevelInternal::Mid:  satTarget = rng.irange(-40, -15); break;
    case SatLevelInternal::High: satTarget = rng.irange(-5, 5);    break;
    }
    s.hsl.saturation = std::clamp(satTarget + satJit / 3, -75, 25);

    // ============ brtCtr ============
    s.enabled[EffectStack::EBrtCtr] = true;
    switch (lgtLvl) {
    case LgtLevelInternal::UltraDark:
        s.brtCtr.brightness = std::clamp(rng.irange(-22, -12), -25, 15);
        s.brtCtr.contrast   = rng.irange(15, 28);
        break;
    case LgtLevelInternal::Dark:
        s.brtCtr.brightness = std::clamp(rng.irange(-12, -3), -25, 15);
        s.brtCtr.contrast   = rng.irange(8, 20);
        break;
    case LgtLevelInternal::Mid:
        s.brtCtr.brightness = std::clamp(rng.irange(-4, 5), -25, 15);
        s.brtCtr.contrast   = rng.irange(2, 14);
        break;
    case LgtLevelInternal::Bright:
        s.brtCtr.brightness = std::clamp(rng.irange(0, 5), -25, 10);
        s.brtCtr.contrast   = rng.irange(0, 10);
        break;
    case LgtLevelInternal::UltraBright:
        s.brtCtr.brightness = std::clamp(rng.irange(4, 10), -25, 12);
        s.brtCtr.contrast   = rng.irange(2, 10);
        break;
    default: break;
    }

    // ============ 曲线 (5 档对应 5 曲线) ============
    s.enabled[EffectStack::ECurves] = true;
    switch (lgtLvl) {
    case LgtLevelInternal::UltraDark:   s.curves.master = makeUltraDarkCurve();    break;
    case LgtLevelInternal::Dark:        s.curves.master = makeDeepSafeCurve();     break;
    case LgtLevelInternal::Mid:         s.curves.master = makeSoftContrastCurve(); break;
    case LgtLevelInternal::Bright:      s.curves.master = makeHighlightSafeCurve();break;
    case LgtLevelInternal::UltraBright: s.curves.master = makeUltraBrightCurve();  break;
    default: break;
    }

    // ============ Vibrance v10: 整体降饱, 不再加饱 ============
    s.enabled[EffectStack::EVibrance] = true;
    switch (satLvl) {
    case SatLevelInternal::Low:
        s.vibrance.vibrance   = rng.irange(-15, -3);
        s.vibrance.saturation = rng.irange(-15, -5);
        break;
    case SatLevelInternal::Mid:
        if (lgtLvl == LgtLevelInternal::Bright || lgtLvl == LgtLevelInternal::UltraBright) {
            // 亮档 v10: 不再加饱 +3..+12, 改 -5..+3
            s.vibrance.vibrance   = rng.irange(-5, 3);
            s.vibrance.saturation = rng.irange(-3, 5);
        } else {
            s.vibrance.vibrance   = rng.irange(-8, 2);
            s.vibrance.saturation = rng.irange(-8, 2);
        }
        break;
    case SatLevelInternal::High:
        // v10 高饱档也要克制 vibrance, 不再 +3..+15
        s.vibrance.vibrance   = rng.irange(-3, 8);
        s.vibrance.saturation = rng.irange(-3, 8);
        break;
    }

    // ============ ColorBalance ============
    if (satLvl != SatLevelInternal::Low) {
        applyColorBalanceForClothing(s, p.clothing, rng);
    }

    // ============ PhotoFilter ============
    int filterChance = 25;
    int densityMin = 3, densityMax = 8;
    if (lgtLvl == LgtLevelInternal::UltraDark) {
        filterChance = 40; densityMin = 6; densityMax = 14;
    } else if (lgtLvl == LgtLevelInternal::Dark) {
        filterChance = 35; densityMin = 5; densityMax = 12;
    } else if (lgtLvl == LgtLevelInternal::Bright) {
        filterChance = 30; densityMin = 4; densityMax = 9;
    } else if (lgtLvl == LgtLevelInternal::UltraBright) {
        filterChance = 35; densityMin = 5; densityMax = 10;
    }

    if (rng.chance(filterChance)) {
        s.enabled[EffectStack::EPhotoFilter] = true;
        int idx = 0;
        switch (p.clothing) {
        case ClothingTone::DarkBlue:   idx = 3;  break;
        case ClothingTone::DarkRed:    idx = 6;  break;
        case ClothingTone::DarkPurple: idx = 12; break;
        case ClothingTone::DarkGreen:  idx = 9;  break;
        case ClothingTone::DeepTeal:   idx = 10; break;
        case ClothingTone::DarkBrown:  idx = 14; break;
        case ClothingTone::DarkGray:   idx = 3;  break;
        case ClothingTone::LightNoble: idx = 0;  break;
        case ClothingTone::RareVivid:  idx = 11; break;
        }
        s.photoFilter.preset  = idx;
        s.photoFilter.filterR = kPhotoFilterPresets[idx].r;
        s.photoFilter.filterG = kPhotoFilterPresets[idx].g;
        s.photoFilter.filterB = kPhotoFilterPresets[idx].b;
        s.photoFilter.density = rng.irange(densityMin, densityMax);
        s.photoFilter.preserveLuma = true;
    }
}

// === Skirt: 跟 Clothing 同档位, 略亮 5-8 (P0 v7) ===
//
// v6 用 ClothingTone 强绑 → v7 改用解码后的 lgt 档位.
//   - 暗档: Skirt 比 Clothing 亮 6-8 (身体核心暗, 裙摆下摆有亮度)
//   - 中档: Skirt 跟 Clothing 同亮度区间
//   - 亮档: Skirt 跟 Clothing 同亮度区间 (亮档不再额外推亮防曝白)
void applySkirt(EffectStack& s, const SchemePalette& p, Rng& rng)
{
    prepStack(s);

    const LgtLevelInternal lgtLvl = decodeLgtLevel(p.lightnessBias);

    // v10: Grayscale 档跟 Clothing 同步, 完全去色
    if (lgtLvl == LgtLevelInternal::Grayscale) {
        s.hsl.hue        = 0;
        s.hsl.saturation = -100;
        int subRoll = rng.irange(0, 99);
        int lgt = 0;
        if (subRoll < 40)      lgt = rng.irange(-20, -5);
        else if (subRoll < 70) lgt = rng.irange(-5, 10);
        else                   lgt = rng.irange(12, 22);
        s.hsl.lightness = std::clamp(lgt, -25, 24);

        s.enabled[EffectStack::EBrtCtr] = true;
        if (lgt < -5) {
            s.brtCtr.brightness = std::clamp(rng.irange(-10, -2), -25, 15);
            s.brtCtr.contrast   = rng.irange(8, 18);
        } else {
            s.brtCtr.brightness = std::clamp(rng.irange(-2, 4), -25, 10);
            s.brtCtr.contrast   = rng.irange(2, 10);
        }

        s.enabled[EffectStack::ECurves] = true;
        if (lgt < -5)      s.curves.master = makeDeepSafeCurve();
        else if (lgt > 8)  s.curves.master = makeHighlightSafeCurve();
        else               s.curves.master = makeSoftContrastCurve();

        s.enabled[EffectStack::EVibrance] = true;
        s.vibrance.vibrance   = rng.irange(-25, -10);
        s.vibrance.saturation = rng.irange(-30, -15);
        return;
    }

    const int baseHue = rng.chance(70) ? p.primaryHue : p.secondaryHue;
    const int hueAbs  = (baseHue + rng.irange(-12, 12) + 360) % 360;
    s.hsl.hue         = hueAbsToShift(hueAbs);

    SatLevelInternal satLvl = decodeSatLevel(p.saturationBias);

    // v9: 跟 Clothing 同步, 亮档不抽 Low sat
    if ((lgtLvl == LgtLevelInternal::Bright || lgtLvl == LgtLevelInternal::UltraBright)
        && satLvl == SatLevelInternal::Low) {
        satLvl = SatLevelInternal::Mid;
    }

    // v11 新增: 20% 概率"裙摆独立走亮档", 不跟随 Clothing 暗.
    // 视觉效果: 暗服装方案下, 裙摆有 20% 几率变亮 (下摆光感, 像渐变到光)
    // 注意: UltraDark/Dark 才有意义, Mid/Bright 已经够亮就不强制
    LgtLevelInternal skirtLgt = lgtLvl;
    if ((lgtLvl == LgtLevelInternal::UltraDark || lgtLvl == LgtLevelInternal::Dark)
        && rng.chance(20)) {
        skirtLgt = LgtLevelInternal::Bright;
    }

    // lightness: 按 skirtLgt 档 (可能被独立提亮过)
    int lgtTarget = 0;
    switch (skirtLgt) {
    case LgtLevelInternal::UltraDark:   lgtTarget = rng.irange(-35, -18); break;
    case LgtLevelInternal::Dark:        lgtTarget = rng.irange(-18, -5);  break;
    case LgtLevelInternal::Mid:         lgtTarget = rng.irange(-4, 8);    break;
    case LgtLevelInternal::Bright:      lgtTarget = rng.irange(2, 12);    break;
    case LgtLevelInternal::UltraBright: lgtTarget = rng.irange(10, 20);   break;
    default: lgtTarget = 0; break;
    }
    s.hsl.lightness = std::clamp(lgtTarget, -42, 24);

    // saturation v10 降饱 -30 (跟 Clothing 同步)
    int satTarget = 0;
    switch (satLvl) {
    case SatLevelInternal::Low:  satTarget = rng.irange(-60, -35); break;
    case SatLevelInternal::Mid:  satTarget = rng.irange(-32, -8);  break;
    case SatLevelInternal::High: satTarget = rng.irange(-8, 22);   break;
    }
    s.hsl.saturation = std::clamp(satTarget, -75, 30);

    // brt + 曲线 (跟 skirtLgt 同档, 让独立提亮的裙摆参数也协调)
    s.enabled[EffectStack::EBrtCtr] = true;
    switch (skirtLgt) {
    case LgtLevelInternal::UltraDark:
        s.brtCtr.brightness = std::clamp(rng.irange(-18, -8), -25, 15);
        s.brtCtr.contrast   = rng.irange(12, 22);
        break;
    case LgtLevelInternal::Dark:
        s.brtCtr.brightness = std::clamp(rng.irange(-8, -1), -25, 15);
        s.brtCtr.contrast   = rng.irange(6, 16);
        break;
    case LgtLevelInternal::Mid:
        s.brtCtr.brightness = std::clamp(rng.irange(-2, 4), -25, 15);
        s.brtCtr.contrast   = rng.irange(2, 12);
        break;
    case LgtLevelInternal::Bright:
        s.brtCtr.brightness = std::clamp(rng.irange(0, 4), -25, 10);
        s.brtCtr.contrast   = rng.irange(0, 8);
        break;
    case LgtLevelInternal::UltraBright:
        s.brtCtr.brightness = std::clamp(rng.irange(3, 8), -25, 10);
        s.brtCtr.contrast   = rng.irange(-2, 6);
        break;
    default: break;
    }

    s.enabled[EffectStack::ECurves] = true;
    switch (skirtLgt) {
    case LgtLevelInternal::UltraDark:   s.curves.master = makeUltraDarkCurve();    break;
    case LgtLevelInternal::Dark:        s.curves.master = makeDeepSafeCurve();     break;
    case LgtLevelInternal::Mid:         s.curves.master = makeSoftContrastCurve(); break;
    case LgtLevelInternal::Bright:      s.curves.master = makeHighlightSafeCurve();break;
    case LgtLevelInternal::UltraBright: s.curves.master = makeUltraBrightCurve();  break;
    default: break;
    }

    if (satLvl != SatLevelInternal::Low) {
        applyColorBalanceForClothing(s, p.clothing, rng);
    }
}

// === Decor01: 布料质感点缀 (P0 v10 重构) ===
//
// 修复:
//   - 旧版错读 saturationBias / 2 (编码值被当 bias, 实际推满高饱) → 色块艳俗
//   - 旧版不读 lgtLvl/satLvl → 跟服装方案氛围脱节
//   - 旧版开 Vibrance 加饱 → 进一步推鲜
//
// 新原则:
//   - 跟随方案 lgtLvl / satLvl, 跟服装协调
//   - 比 Clothing 饱和度低 5 (装饰不能比主体鲜)
//   - 永不开 Vibrance 加饱
//   - Grayscale 档完全去色
void applyDecor01(EffectStack& s, const SchemePalette& p, Rng& rng)
{
    prepStack(s);

    const LgtLevelInternal lgtLvl = decodeLgtLevel(p.lightnessBias);

    // Grayscale 档跟服装去色
    if (lgtLvl == LgtLevelInternal::Grayscale) {
        s.hsl.hue        = 0;
        s.hsl.saturation = -100;
        s.hsl.lightness  = std::clamp(rng.irange(-10, 8), -15, 18);
        s.enabled[EffectStack::EVibrance] = true;
        s.vibrance.vibrance   = rng.irange(-20, -8);
        s.vibrance.saturation = rng.irange(-25, -12);
        return;
    }

    // hue: 用 accentHue (服装主色互补色) ±15°
    const int hueAbs = (p.accentHue + rng.irange(-15, 15) + 360) % 360;
    s.hsl.hue        = hueAbsToShift(hueAbs);

    // satLvl: 跟方案同档, 但区间比 Clothing 低 5-10
    SatLevelInternal satLvl = decodeSatLevel(p.saturationBias);
    int satTarget = 0;
    switch (satLvl) {
    case SatLevelInternal::Low:  satTarget = rng.irange(-55, -28); break;  // Clothing -60..-35
    case SatLevelInternal::Mid:  satTarget = rng.irange(-32, -10); break;  // Clothing -30..-5
    case SatLevelInternal::High: satTarget = rng.irange(-12, 12);  break;  // Clothing -5..+25
    }
    s.hsl.saturation = std::clamp(satTarget, -70, 25);

    // lightness: 跟 lgtLvl 方向, 比 Clothing 区间略窄
    int lgtTarget = 0;
    switch (lgtLvl) {
    case LgtLevelInternal::UltraDark:   lgtTarget = rng.irange(-25, -8);  break;
    case LgtLevelInternal::Dark:        lgtTarget = rng.irange(-15, -2);  break;
    case LgtLevelInternal::Mid:         lgtTarget = rng.irange(-5, 6);    break;
    case LgtLevelInternal::Bright:      lgtTarget = rng.irange(0, 10);    break;
    case LgtLevelInternal::UltraBright: lgtTarget = rng.irange(8, 18);    break;
    default: lgtTarget = 0; break;
    }
    s.hsl.lightness = std::clamp(lgtTarget, -28, 22);

    // 暗档加 BrtCtr 增对比, 让点缀有金属/布料质感
    if (lgtLvl == LgtLevelInternal::UltraDark || lgtLvl == LgtLevelInternal::Dark) {
        s.enabled[EffectStack::EBrtCtr] = true;
        s.brtCtr.brightness = std::clamp(rng.irange(-10, -2), -15, 8);
        s.brtCtr.contrast   = rng.irange(6, 18);
    }

    // 永不开 Vibrance 加饱 (装饰不能再推鲜)
    // 仅在低饱档时降饱一次, 配合 satLvl=Low 形成纯灰装饰
    if (satLvl == SatLevelInternal::Low) {
        s.enabled[EffectStack::EVibrance] = true;
        s.vibrance.vibrance   = rng.irange(-12, -3);
        s.vibrance.saturation = rng.irange(-12, -4);
    }
}

// === Decor02: 装饰02 / 发光点 (P0 v10 重构) ===
//
// 修复同 Decor01: 旧版 saturationBias/3 + lightnessBias/2 错读 → 推满
//
// 新原则:
//   - 跟随方案档位, Grayscale 去色, 暗档暗装饰
//   - 比 Decor01 饱和度高 5-8 (装饰02 = 高亮点缀, 有发光感)
//   - 但永远比 Clothing 鲜 5-8 之内, 不再独立高饱
//   - Mid/Bright + High sat 档下才允许稍微高饱 (发光宝石)
void applyDecor02(EffectStack& s, const SchemePalette& p, Rng& rng)
{
    prepStack(s);

    const LgtLevelInternal lgtLvl = decodeLgtLevel(p.lightnessBias);

    if (lgtLvl == LgtLevelInternal::Grayscale) {
        s.hsl.hue        = 0;
        s.hsl.saturation = -100;
        s.hsl.lightness  = std::clamp(rng.irange(-5, 12), -15, 22);
        s.enabled[EffectStack::EVibrance] = true;
        s.vibrance.vibrance   = rng.irange(-20, -8);
        s.vibrance.saturation = rng.irange(-25, -12);
        return;
    }

    // hue: 50% accent2, 50% glow
    const int baseHue = rng.chance(50) ? p.accent2Hue : p.glowHue;
    const int hueAbs  = (baseHue + rng.irange(-12, 12) + 360) % 360;
    s.hsl.hue         = hueAbsToShift(hueAbs);

    // satLvl: 比 Clothing 高 5, 但仍受档位限制
    SatLevelInternal satLvl = decodeSatLevel(p.saturationBias);
    int satTarget = 0;
    switch (satLvl) {
    case SatLevelInternal::Low:  satTarget = rng.irange(-50, -25); break;
    case SatLevelInternal::Mid:  satTarget = rng.irange(-25, -5);  break;
    case SatLevelInternal::High: satTarget = rng.irange(-5, 22);   break;
    }
    s.hsl.saturation = std::clamp(satTarget, -65, 30);

    // lightness: 装饰02 比 Decor01 略亮 (高光点缀)
    int lgtTarget = 0;
    switch (lgtLvl) {
    case LgtLevelInternal::UltraDark:   lgtTarget = rng.irange(-18, -2);  break;
    case LgtLevelInternal::Dark:        lgtTarget = rng.irange(-10, 4);   break;
    case LgtLevelInternal::Mid:         lgtTarget = rng.irange(-2, 12);   break;
    case LgtLevelInternal::Bright:      lgtTarget = rng.irange(4, 14);    break;
    case LgtLevelInternal::UltraBright: lgtTarget = rng.irange(10, 20);   break;
    default: lgtTarget = 0; break;
    }
    s.hsl.lightness = std::clamp(lgtTarget, -22, 24);

    // 仅 (Mid 或 Bright) + High sat 档时开 Vibrance 微加饱, 模拟发光宝石感
    // 其他档不开, 跟服装质感统一
    if ((lgtLvl == LgtLevelInternal::Mid || lgtLvl == LgtLevelInternal::Bright)
        && satLvl == SatLevelInternal::High) {
        s.enabled[EffectStack::EVibrance] = true;
        s.vibrance.vibrance   = rng.irange(0, 10);
        s.vibrance.saturation = rng.irange(-3, 6);
    } else if (satLvl == SatLevelInternal::Low) {
        // 低饱档降饱
        s.enabled[EffectStack::EVibrance] = true;
        s.vibrance.vibrance   = rng.irange(-10, -2);
        s.vibrance.saturation = rng.irange(-12, -4);
    }

    // 暗档加微亮度抬起, 让点缀有发光感 (不强推, 防止变荧光)
    if ((lgtLvl == LgtLevelInternal::UltraDark || lgtLvl == LgtLevelInternal::Dark)
        && satLvl != SatLevelInternal::Low) {
        s.enabled[EffectStack::EBrtCtr] = true;
        s.brtCtr.brightness = std::clamp(rng.irange(-2, 6), -12, 10);
        s.brtCtr.contrast   = rng.irange(4, 14);
    }
}

// === WeaponMetal: 固定金属色池 ===
//   每种金属 = 固定 hue + 固定 sat/lgt 区间. ±5° 微抖动.
void applyWeaponMetal(EffectStack& s, const SchemePalette& p, Rng& rng)
{
    prepStack(s);
    int hueAbs = 0, satLo = 0, satHi = 0, lgtLo = 0, lgtHi = 0;
    switch (p.metal) {
    case MetalTone::Gold:      hueAbs = 45;  satLo = 10;  satHi = 25; lgtLo = -2; lgtHi = 8;  break;
    case MetalTone::Silver:    hueAbs = 210; satLo = -35; satHi = -15; lgtLo = 0; lgtHi = 6;  break;
    case MetalTone::Copper:    hueAbs = 25;  satLo =  5;  satHi = 20; lgtLo = -5; lgtHi = 5;  break;
    case MetalTone::BlackIron: hueAbs = 220; satLo = -45; satHi = -25; lgtLo = -15; lgtHi = -5; break;
    case MetalTone::BlueSteel: hueAbs = 215; satLo = -10; satHi = 10; lgtLo = -5; lgtHi = 5;  break;
    case MetalTone::DarkGold:  hueAbs = 40;  satLo = -5;  satHi = 10; lgtLo = -10; lgtHi = 2;  break;
    }
    hueAbs = (hueAbs + rng.irange(-5, 5) + 360) % 360;
    s.hsl.hue        = hueAbsToShift(hueAbs);
    s.hsl.saturation = std::clamp(rng.irange(satLo, satHi), -100, 100);
    s.hsl.lightness  = std::clamp(rng.irange(lgtLo, lgtHi), -100, 100);

    // 金属固定加曲线增对比 + 保高光
    s.enabled[EffectStack::ECurves] = true;
    const int midX = rng.irange(118, 138);
    const int midY = rng.irange(106, 130);   // 偏暗中, 让高光更突出
    s.curves.master = { {0, 0}, {midX, midY}, {255, 255} };

    // BlackIron 额外压亮
    if (p.metal == MetalTone::BlackIron) {
        s.enabled[EffectStack::EBrtCtr] = true;
        s.brtCtr.brightness = rng.irange(-22, -8);
        s.brtCtr.contrast   = rng.irange(8, 22);
    }
}

// === WeaponNonMetal: 70% 随 Clothing, 30% 随 Decor01 ===
void applyWeaponNonMetal(EffectStack& s, const SchemePalette& p, Rng& rng)
{
    if (rng.chance(70)) {
        applyClothing(s, p, rng);
    } else {
        applyDecor01(s, p, rng);
    }
}


// =============================================================================
// === 颜色 LayerSlot 多档子方案 (P2 优化) ===
//
// 设计目标:
//   1. 每个色相提供 5~6 个 sub-variant (鲜艳/深/淡/哑光/莹光/金属等),
//      单次随机时挑一档 + 档内大幅 jitter → 单击产生 30+ 不同视觉变化, 但都落在同色系内.
//   2. 整体提亮 / 提饱: 有彩色 sat 改正向 (+10..+25 鲜艳档), lgt 允许正向, brt 不再强制压暗.
//   3. 无彩色 (Black/White/Silver/Gray) 用 ChannelMixer monochrome 保留原图明暗梯度,
//      White 大幅 brt 提亮到 +30, Black 大幅 brt 压到 -30, 配合多档曲线/染色.
//   4. 部件 slot 后置: Hair 选稳重档, Decor02 选亮档, WeaponMetal 优先 metal 曲线档.
//
// 不改变: 部件 slot (Skin/Hair/Clothing/Skirt/Decor01/Decor02/WeaponMetal/WeaponNonMetal)
//        本身的随机逻辑 — 颜色补丁只是在其上覆盖.
// =============================================================================

// 单档子方案. 有彩色用 hueOff/hueJit/sat/lgt; 无彩色用 monoR/G/B/monoBias.
// brt/ctr/curve/photo 两路通用.
struct ColorVariant {
    int hueOff = 0;
    int hueJit = 6;
    int satLo = -10, satHi = 10;
    int lgtLo = -8,  lgtHi = 8;
    int brtLo = -4,  brtHi = 4;
    int ctrLo = 0,   ctrHi = 10;
    int vibLo = -8,  vibHi = 5;
    int vsatLo = -10, vsatHi = 5;
    int curve = 0;            // 0=soft 1=deep 2=highlight 3=metal 4=ultraBright 5=identity
    int photoPreset = -1;     // -1 = 不开
    int photoChance = 30;
    int photoLo = 3, photoHi = 10;
    // ColorBalance 目标 (任何分量非 0 即启用; apply 时 ±2 jitter)
    int balSR=0, balSG=0, balSB=0;
    int balMR=0, balMG=0, balMB=0;
    int balHR=0, balHG=0, balHB=0;
    // mono only
    int monoR = 30, monoG = 59, monoB = 11;
    int monoBias = 0;
};

struct ColorSlotParams {
    int baseHue = 0;
    bool useChMixMono = false;
    QVector<ColorVariant> variants;
};

CurveParams::Pts curveByCode(int code) {
    switch (code) {
    case 1: return makeDeepSafeCurve();
    case 2: return makeHighlightSafeCurve();
    case 3: return CurveParams::Pts{ {0,6}, {64,48}, {128,122}, {200,218}, {255,248} };
    case 4: return makeUltraBrightCurve();
    case 5: return CurveParams::Pts{ {0,0}, {255,255} };
    default: return makeSoftContrastCurve();
    }
}

ColorSlotParams paramsForColorSlot(LayerColorSlot color)
{
    // 有彩色 variant 构造器
    auto V = [](int hueOff, int hueJit, int sLo, int sHi, int lLo, int lHi,
                int bLo, int bHi, int cLo, int cHi, int curve,
                int photoPreset, int photoChance) {
        ColorVariant v;
        v.hueOff = hueOff; v.hueJit = hueJit;
        v.satLo = sLo; v.satHi = sHi;
        v.lgtLo = lLo; v.lgtHi = lHi;
        v.brtLo = bLo; v.brtHi = bHi;
        v.ctrLo = cLo; v.ctrHi = cHi;
        v.curve = curve;
        v.photoPreset = photoPreset;
        v.photoChance = photoChance;
        v.vibLo  = -8;
        v.vibHi  = std::max(0, sHi / 3);
        v.vsatLo = -10;
        v.vsatHi = std::max(0, sHi / 3);
        return v;
    };
    // mono variant 构造器
    auto M = [](int mR, int mG, int mB, int bias,
                int bLo, int bHi, int cLo, int cHi, int curve,
                int photoPreset, int photoChance) {
        ColorVariant v;
        v.monoR = mR; v.monoG = mG; v.monoB = mB; v.monoBias = bias;
        v.brtLo = bLo; v.brtHi = bHi;
        v.ctrLo = cLo; v.ctrHi = cHi;
        v.curve = curve;
        v.photoPreset = photoPreset;
        v.photoChance = photoChance;
        v.photoLo = 2; v.photoHi = 6;
        return v;
    };

    ColorSlotParams p;
    switch (color) {
    // ============ 有彩色 ============
    case LayerColorSlot::Red:
        p.baseHue = 0;
        //          hueOff hueJit  sLo sHi  lLo lHi  bLo bHi  cLo cHi crv pho %
        p.variants = {
            V( 0,  4,  12, 28, -6,  6, -2,  6, 12, 24, 0,  6, 30),   // 鲜艳正红
            V( 2,  6,   8, 22,-16, -4, -8, -2, 12, 26, 1, 15, 40),   // 深酒红
            V(12,  8,   8, 22, -4,  8,  0,  6,  8, 18, 0,  7, 30),   // 朱红 (向橙)
            V(-15, 6, -10, 14,  2, 12,  2,  8,  0, 12, 2, 13, 35),   // 玫瑰红 (向粉)
            V( 0,  6, -22,  4, -8,  6, -4,  3,  0, 12, 0, -1, 0),    // 哑光红
            V( 0,  5,  10, 25, -2,  8,  2,  8,  6, 16, 4, -1, 0),    // 高光正红
        };
        break;
    case LayerColorSlot::Orange:
        p.baseHue = 28;
        p.variants = {
            V( 0,  4,  12, 28, -2,  8,  0,  6,  8, 18, 0,  7, 30),   // 鲜艳橙
            V(-4,  6,  10, 24,-12, -2, -6,  0, 12, 24, 1, 14, 40),   // 焦糖橙
            V( 8,  8,   5, 20,  2, 12,  2,  6,  0, 12, 0,  8, 30),   // 杏橙
            V(-10, 6,   8, 22, -4,  6, -2,  4,  8, 18, 0,  6, 30),   // 红橙
            V( 0,  6, -15, 10, -4,  8, -2,  4,  0, 10, 0, -1, 0),    // 哑光橙
            V( 0,  5,   8, 22, -2,  4,  0,  4, 18, 30, 3, -1, 0),    // 金属橙
        };
        break;
    case LayerColorSlot::Yellow:
        p.baseHue = 52;
        p.variants = {
            V( 0,  4,  12, 28,  0, 10,  2, 10,  4, 14, 0,  8, 30),   // 明黄
            V(-6,  6,  12, 26, -4,  4,  0,  6, 10, 22, 3, 17, 40),   // 金黄 (向橙, 金属)
            V( 8,  6,   8, 22,  2, 12,  2,  6,  0, 12, 0,  9, 30),   // 柠檬黄 (向绿)
            V( 0,  6, -22,  4,  2, 12,  0,  6,  0, 10, 0, -1, 0),    // 哑光黄
            V( 0,  5,   8, 22,  4, 16,  4, 12, -2,  8, 4,  8, 25),   // 高光明黄
            V( 0,  6,  -8, 12,  4, 14,  2,  8,  0, 10, 2, 14, 25),   // 米黄/沙黄
        };
        break;
    case LayerColorSlot::Green:
        p.baseHue = 135;
        p.variants = {
            V( 0,  6,  12, 28, -4,  6,  0,  6, 10, 22, 0,  9, 30),   // 鲜艳绿
            V(10,  8,  12, 26,-16, -4, -8, -2, 12, 26, 1, 16, 40),   // 深翠绿 (向青)
            V(-15, 8,   8, 22,  2, 12,  2,  6,  0, 12, 0,  9, 30),   // 嫩绿 (向黄)
            V( 0,  8,   8, 20, -4,  8, -2,  4,  4, 14, 0,  9, 30),   // 草绿
            V( 0,  8, -22,  6, -8,  6, -4,  2,  0, 10, 0, -1, 0),    // 哑光绿
            V( 0,  6,  15, 30,  2, 12,  2,  8,  0, 10, 4,  9, 40),   // 莹光绿
        };
        break;
    case LayerColorSlot::Cyan:
        p.baseHue = 188;
        p.variants = {
            V( 0,  4,  12, 28, -2,  8,  0,  6,  8, 20, 0, 10, 30),   // 鲜艳青
            V( 0,  6,  10, 24,-14, -2, -6,  0, 10, 22, 1, 11, 40),   // 深青
            V(15,  8,   8, 22, -6,  6, -2,  4,  8, 18, 0, 11, 30),   // 蓝青
            V(-15, 8,   8, 22, -2,  8,  0,  6,  4, 14, 0,  9, 30),   // 绿青
            V( 0,  6, -18,  8, -6,  6, -2,  4,  0, 12, 0, -1, 0),    // 哑光青
            V( 0,  6,  15, 30,  0, 12,  2, 10,  0, 10, 4, 10, 40),   // 莹光青
        };
        break;
    case LayerColorSlot::Blue:
        p.baseHue = 228;
        p.variants = {
            V( 0,  4,  12, 28, -6,  6, -2,  4,  8, 20, 0, 11, 30),   // 鲜艳蓝
            V( 0,  6,  10, 26,-18, -4, -8, -2, 12, 26, 1, 15, 40),   // 海军蓝
            V(-12, 8,   8, 22,  0, 12,  2,  6,  0, 12, 0,  4, 30),   // 天蓝 (向青)
            V(12,  8,   8, 22, -8,  4, -2,  4,  4, 14, 0, 12, 30),   // 紫蓝
            V( 0,  6, -18,  8, -6,  6, -2,  2,  0, 10, 0, -1, 0),    // 哑光蓝
            V( 0,  6,  15, 30,  0, 12,  2, 10,  0, 10, 4, 11, 35),   // 莹光蓝
        };
        break;
    case LayerColorSlot::Purple:
        p.baseHue = 278;
        p.variants = {
            V( 0,  4,  12, 28, -4,  8, -2,  4,  8, 20, 0, 12, 30),   // 鲜艳紫
            V( 0,  6,  10, 26,-16, -2, -6,  0, 10, 24, 1, 12, 40),   // 深紫
            V(-15, 8,   8, 22, -6,  6, -2,  4,  4, 14, 0, 11, 30),   // 蓝紫
            V(15,  8,   8, 22, -2,  8,  0,  6,  4, 14, 0, 13, 30),   // 玫紫
            V( 0,  8, -18,  8, -4,  8, -2,  4,  0, 12, 0, -1, 0),    // 灰紫/哑光
            V( 0,  6,  15, 30,  0, 12,  2, 10,  0, 10, 4, 12, 35),   // 莹光紫
        };
        break;
    case LayerColorSlot::Pink:
        p.baseHue = 340;
        p.variants = {
            V( 0,  4,  -8, 18,  4, 14,  4, 10,  0, 10, 2, 13, 30),   // 樱花粉
            V(-10, 6,   8, 22,  0,  8,  0,  6,  4, 14, 0, 13, 35),   // 玫瑰粉 (向红)
            V(15,  8,   2, 18,  2, 12,  2,  8,  0, 10, 0, 13, 30),   // 桃粉 (向橙)
            V( 0,  6, -28, -2,  4, 16,  4, 10,  0,  8, 2, -1, 0),    // 哑光淡粉
            V(-4,  4,  12, 28, -4,  6, -2,  4,  6, 16, 0, 13, 35),   // 高饱品红
            V( 0,  6,   5, 20,  6, 16,  4, 12,  0, 10, 4, 13, 40),   // 莹光粉
        };
        break;

    // ============ 无彩色 (ChannelMixer monochrome) ============
    case LayerColorSlot::Black:
        p.useChMixMono = true;
        //          mR  mG  mB  bias  bLo  bHi  cLo cHi crv pho %
        p.variants = {
            M(30, 59, 11, -10, -32, -20, 15, 30, 1,  -1, 0),         // 纯黑
            M(28, 56, 16,  -6, -24, -14, 10, 24, 1, 15, 30),         // 墨黑 (略偏冷)
            M(30, 59, 11,  -3, -18,  -8,  6, 18, 0,  -1, 0),         // 灰黑/哑光
            M(26, 56, 18,  -8, -28, -16, 12, 26, 1, 15, 45),         // 冷黑
            M(34, 56, 10,  -8, -28, -16, 12, 26, 1, 14, 35),         // 暖黑/墨棕
            M(32, 58, 10,  -5, -22, -12,  8, 20, 5,  -1, 0),         // 软黑 (无对比曲线)
        };
        // 冷/暖黑分别加 ColorBalance 阴影染色
        p.variants[3].balSR = -4; p.variants[3].balSB = 8;
        p.variants[4].balSR =  6; p.variants[4].balSG = 2; p.variants[4].balSB = -4;
        break;
    case LayerColorSlot::White:
        p.useChMixMono = true;
        p.variants = {
            M(33, 55, 12,  12,  20,  32, -4, 12, 2,  -1, 0),         // 纯白
            M(30, 59, 11,   8,  15,  26, -4,  6, 4,  -1, 0),         // 雪白 (ultraBright)
            M(28, 58, 14,   2,   2,  12, 12, 26, 3,  -1, 0),         // 银白 (metal)
            M(34, 54, 12,  10,  16,  28, -2,  8, 2,  0, 35),         // 暖白
            M(30, 56, 14,  10,  16,  28, -2,  8, 2,  5, 35),         // 冷白
            M(32, 55, 13,   6,  10,  22,  0, 10, 0,  2, 25),         // 米白
        };
        p.variants[3].balHR = 6; p.variants[3].balHG = 2;             // 高光暖
        p.variants[4].balHR = -4; p.variants[4].balHB = 8;            // 高光冷
        p.variants[5].balHR = 3; p.variants[5].balHG = 1;             // 米白微暖
        break;
    case LayerColorSlot::Silver:
        p.useChMixMono = true;
        p.variants = {
            M(28, 58, 14,   3,   5,  16, 14, 26, 3,  3, 30),         // 亮银
            M(28, 58, 14,  -2,  -8,   4, 12, 22, 3,  3, 30),         // 暗银
            M(26, 58, 16,   2,   0,  10, 12, 24, 3,  3, 45),         // 冷钢银
            M(33, 55, 12,   4,   4,  14, 12, 24, 3,  2, 30),         // 钛银 (微暖)
            M(28, 58, 14,   3,   5,  14, 18, 32, 3, -1, 0),          // 镜面银 (强对比)
            M(30, 59, 11,   0,  -4,   8,  6, 16, 0, -1, 0),          // 哑光银
        };
        p.variants[2].balSR = -3; p.variants[2].balSB = 6;
        p.variants[2].balHR = -4; p.variants[2].balHB = 8;
        p.variants[3].balHR = 4;  p.variants[3].balHG = 1;
        break;
    case LayerColorSlot::Gray:
        p.useChMixMono = true;
        p.variants = {
            M(30, 59, 11,   0,  -6,   6,  2, 12, 0, -1, 0),          // 中性灰
            M(31, 58, 11,   6,   6,  16,  0, 10, 0, -1, 0),          // 浅灰
            M(30, 59, 11,  -8, -16,  -6,  6, 18, 1, -1, 0),          // 深灰
            M(33, 56, 11,   2,  -4,   8,  2, 12, 0,  0, 35),         // 暖灰
            M(28, 58, 14,   2,  -4,   8,  2, 12, 0,  3, 35),         // 冷灰
            M(28, 58, 14,   4,   0,  10, 10, 22, 3,  3, 30),         // 钢灰 (metal)
        };
        p.variants[3].balMR = 5; p.variants[3].balMG = 2; p.variants[3].balMB = -4;
        p.variants[4].balMR = -3; p.variants[4].balMB = 6;
        break;

    case LayerColorSlot::Auto:
        break;
    }
    return p;
}

void applyColorSlotPatch(EffectStack& s, LayerSlot slot, LayerColorSlot color, Rng& rng)
{
    // 不再用 slot == Skin 做早返回:
    //   1) 肤色保护层由 ProjectController::isSkinSafe() 在调用前短路, 不会进到这里.
    //   2) body 层默认 slotFor() = Skin, 但 body 实际是普通图层 (跟 00/01/.../addon 同性质),
    //      用户主动指定颜色 LayerSlot 时必须生效.
    if (color == LayerColorSlot::Auto) return;

    ColorSlotParams p = paramsForColorSlot(color);
    if (p.variants.isEmpty()) return;

    // ===== 部件 slot 后置: 转色 (失语义的强制改) =====
    if (slot == LayerSlot::Decor02
        && (color == LayerColorSlot::Black || color == LayerColorSlot::Gray)) {
        // 发光位强行黑/灰失"发光"语义 → 转 Blue/Silver
        p = paramsForColorSlot(color == LayerColorSlot::Black ? LayerColorSlot::Blue : LayerColorSlot::Silver);
    } else if (slot == LayerSlot::WeaponMetal) {
        if (color == LayerColorSlot::Yellow || color == LayerColorSlot::Orange)
            p = paramsForColorSlot(LayerColorSlot::Yellow);
        else if (color == LayerColorSlot::Cyan || color == LayerColorSlot::Blue
              || color == LayerColorSlot::Green || color == LayerColorSlot::Pink
              || color == LayerColorSlot::Purple)
            p = paramsForColorSlot(LayerColorSlot::Silver);
        // Red / Black / White / Silver / Gray / Auto → 保持
    }

    // ===== 选档: 部件不同 → variant 候选不同 =====
    const int vCount = p.variants.size();
    int vIdx = 0;
    switch (slot) {
    case LayerSlot::Hair:
        // 头发偏稳重 → 前 4 档 (鲜艳/深/淡 等), 避开过度莹光/金属
        vIdx = rng.irange(0, std::min(vCount - 1, 3));
        break;
    case LayerSlot::Decor02: {
        // 发光点偏亮 → 后半档 (莹光/高光优先)
        int lo = std::min(vCount - 1, vCount / 2);
        vIdx = rng.irange(lo, vCount - 1);
        break;
    }
    case LayerSlot::WeaponMetal: {
        // 金属优先 metal 曲线档
        int found = -1;
        for (int i = 0; i < vCount; ++i) if (p.variants[i].curve == 3) { found = i; break; }
        vIdx = (found >= 0 && rng.chance(70)) ? found : rng.irange(0, vCount - 1);
        break;
    }
    default:
        vIdx = rng.irange(0, vCount - 1);
        break;
    }

    ColorVariant v = p.variants[vIdx];

    // 部件细化微调
    if (slot == LayerSlot::Hair) {
        v.satHi = std::min(v.satHi, 22);
        v.lgtLo = std::max(v.lgtLo, -14);
        v.lgtHi = std::min(v.lgtHi, 14);
        v.brtLo = std::max(v.brtLo, -14);
        v.brtHi = std::min(v.brtHi, 12);
        if (p.useChMixMono) v.monoBias = std::clamp(v.monoBias, -10, 10);
    } else if (slot == LayerSlot::Decor01) {
        v.satLo += 2; v.satHi += 4;
        v.lgtLo += 1; v.lgtHi += 3;
        v.brtLo += 1; v.brtHi += 2;
        if (p.useChMixMono) v.monoBias += 2;
    } else if (slot == LayerSlot::Decor02) {
        v.satHi += 6;
        v.lgtHi += 4;
        v.brtLo += 2; v.brtHi += 4;
        if (p.useChMixMono) v.monoBias += 4;
        v.photoChance = std::min(80, v.photoChance + 15);
    } else if (slot == LayerSlot::WeaponMetal) {
        // 金属强对比 + 强制 metal 曲线
        v.curve = 3;
        v.ctrLo = std::max(v.ctrLo, 12);
        v.ctrHi = std::max(v.ctrHi, 24);
    }

    // ===== 应用到 EffectStack =====
    if (p.useChMixMono) {
        // ----- Mono 路径 -----
        s.enabled[EffectStack::EHsl] = false; s.hsl.reset();
        s.enabled[EffectStack::EVibrance] = false; s.vibrance.reset();

        s.enabled[EffectStack::EChMix] = true;
        s.chMix.monochrome = true;
        s.chMix.rr = v.monoR; s.chMix.rg = v.monoG; s.chMix.rb = v.monoB;
        s.chMix.r_const = std::clamp(v.monoBias, -50, 50);
        s.chMix.gr = 0; s.chMix.gg = 100; s.chMix.gb = 0; s.chMix.g_const = 0;
        s.chMix.br = 0; s.chMix.bg = 0;   s.chMix.bb = 100; s.chMix.b_const = 0;

        s.enabled[EffectStack::EBrtCtr] = true;
        s.brtCtr.brightness = std::clamp(rng.irange(v.brtLo, v.brtHi), -50, 40);
        s.brtCtr.contrast   = std::clamp(rng.irange(v.ctrLo, v.ctrHi), -25, 40);
    } else {
        // ----- 有彩色路径 -----
        s.enabled[EffectStack::EChMix] = false; s.chMix.reset();

        s.enabled[EffectStack::EHsl] = true;
        const int hueAbs = (p.baseHue + v.hueOff + rng.irange(-v.hueJit, v.hueJit) + 360) % 360;
        s.hsl.hue = hueAbsToShift(hueAbs);
        s.hsl.saturation = std::clamp(rng.irange(v.satLo, v.satHi), -100, 100);
        s.hsl.lightness  = std::clamp(rng.irange(v.lgtLo, v.lgtHi), -45, 30);

        s.enabled[EffectStack::EBrtCtr] = true;
        s.brtCtr.brightness = std::clamp(rng.irange(v.brtLo, v.brtHi), -30, 25);
        s.brtCtr.contrast   = std::clamp(rng.irange(v.ctrLo, v.ctrHi), -25, 35);

        s.enabled[EffectStack::EVibrance] = true;
        s.vibrance.vibrance   = std::clamp(rng.irange(v.vibLo, v.vibHi), -100, 100);
        s.vibrance.saturation = std::clamp(rng.irange(v.vsatLo, v.vsatHi), -100, 100);
    }

    // 曲线 (通用)
    s.enabled[EffectStack::ECurves] = true;
    s.curves.master = curveByCode(v.curve);
    s.curves.r = CurveParams::Pts{ {0,0}, {255,255} };
    s.curves.g = s.curves.r;
    s.curves.b = s.curves.r;

    // ColorBalance: 任何分量非 0 即启用, ±2 jitter 让档内再分裂
    const bool hasBal = (v.balSR | v.balSG | v.balSB
                       | v.balMR | v.balMG | v.balMB
                       | v.balHR | v.balHG | v.balHB) != 0;
    if (hasBal) {
        s.enabled[EffectStack::EColorBal] = true;
        s.colorBal.preserveLuma = true;
        auto jit = [&](int t) { return t == 0 ? 0 : t + rng.irange(-2, 2); };
        s.colorBal.sR = jit(v.balSR); s.colorBal.sG = jit(v.balSG); s.colorBal.sB = jit(v.balSB);
        s.colorBal.mR = jit(v.balMR); s.colorBal.mG = jit(v.balMG); s.colorBal.mB = jit(v.balMB);
        s.colorBal.hR = jit(v.balHR); s.colorBal.hG = jit(v.balHG); s.colorBal.hB = jit(v.balHB);
    } else {
        s.enabled[EffectStack::EColorBal] = false;
        s.colorBal.reset();
    }

    // PhotoFilter
    if (v.photoPreset >= 0 && v.photoPreset < kPhotoFilterPresetCount
        && rng.chance(v.photoChance)) {
        s.enabled[EffectStack::EPhotoFilter] = true;
        s.photoFilter.preset  = v.photoPreset;
        s.photoFilter.filterR = kPhotoFilterPresets[v.photoPreset].r;
        s.photoFilter.filterG = kPhotoFilterPresets[v.photoPreset].g;
        s.photoFilter.filterB = kPhotoFilterPresets[v.photoPreset].b;
        s.photoFilter.density = std::clamp(rng.irange(v.photoLo, v.photoHi), 0, 50);
        s.photoFilter.preserveLuma = true;
    } else {
        s.enabled[EffectStack::EPhotoFilter] = false;
    }
}

} // namespace

void randomizeStackBySlot(EffectStack& s,
                          LayerSlot slot,
                          const SchemePalette& palette,
                          quint32 seed)
{
    Rng rng(seed);
    switch (slot) {
    case LayerSlot::Skin:           applySkinReset(s);                          break;
    case LayerSlot::Hair:           applyHair(s, palette, rng);                 break;
    case LayerSlot::Clothing:       applyClothing(s, palette, rng);             break;
    case LayerSlot::Skirt:          applySkirt(s, palette, rng);                break;
    case LayerSlot::Decor01:        applyDecor01(s, palette, rng);              break;
    case LayerSlot::Decor02:        applyDecor02(s, palette, rng);              break;
    case LayerSlot::WeaponMetal:    applyWeaponMetal(s, palette, rng);          break;
    case LayerSlot::WeaponNonMetal: applyWeaponNonMetal(s, palette, rng);       break;
    case LayerSlot::Unknown:
    default:
        // Unknown 走 Clothing 兜底 (上层 slotFor 已经做了启发式, 走到这里说明 palette 异常)
        applyClothing(s, palette, rng);
        break;
    }
}

void randomizeStackBySlot(EffectStack& s,
                          LayerSlot slot,
                          LayerColorSlot colorSlot,
                          const SchemePalette& palette,
                          quint32 seed)
{
    if (colorSlot == LayerColorSlot::Auto) {
        randomizeStackBySlot(s, slot, palette, seed);
        return;
    }

    Rng rng(seed);
    randomizeStackBySlot(s, slot, palette, rng.nextSeed());
    applyColorSlotPatch(s, slot, colorSlot, rng);
}

} // namespace HighPro
