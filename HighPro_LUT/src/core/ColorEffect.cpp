#include "ColorEffect.h"

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

} // namespace HighPro
