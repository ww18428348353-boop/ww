#pragma once

#include <QString>
#include <QtGlobal>
#include <array>

namespace HighPro {

// 方案级氛围. 仅作风格分类提示, 实际 hue 由 primary/secondary/accent 决定.
//
// P0 v4: 加 DarkVivid (暗高饱) / BrightVivid (亮高饱) 解决"暗的不够暗 + 亮的曝白"
// 老 Dark 仍是 "暗 + 低饱 + 滤镜氛围", 新 DarkVivid 是 "暗 + 高饱 + 强暗背景".
enum class StyleMood : int {
    Realistic = 0,  // 写实
    Dark,           // 暗黑 (低饱 + 冷色滤镜氛围)
    DarkVivid,      // 暗黑高饱 (暗红 / 暗紫 / 暗绿 / 暗蓝 / 暗黑灰)
    Cold,           // 冷色
    Warm,           // 暖色
    Noble,          // 高贵金属
    Fantasy,        // 奇幻发光
    BrightVivid,    // 亮调高饱 (鲜艳但不曝白)
    Mono,           // 黑白灰
    Vivid,          // 彩蛋高饱
};

// 武器金属固定色池.
enum class MetalTone : int {
    Gold = 0,       // 金
    Silver,         // 银
    Copper,         // 铜
    BlackIron,      // 黑铁
    BlueSteel,      // 蓝钢
    DarkGold,       // 暗金
};

// 服装主色调 (P0 v5 引入). 决定 Clothing 走哪个 profile.
//   DarkBlue/Red/Purple/Green/Teal → DarkRich profile (暗高饱 + DeepSafe 曲线)
//   DarkGray/Brown                  → DarkNeutral profile (低饱黑灰 + DeepSafe 曲线)
//   LightNoble                      → LightNoble profile (亮但不曝白 + HighlightSafe 曲线)
//   RareVivid                       → RareVivid profile (稀有彩色, sat 受限)
enum class ClothingTone : int {
    DarkBlue = 0,
    DarkRed,
    DarkPurple,
    DarkGreen,
    DarkGray,
    DeepTeal,
    DarkBrown,
    LightNoble,
    RareVivid,
};

// 方案级调色盘. 整个 Scheme 共用一个 Palette, 各层按 LayerSlot 取对应 hue.
//   primaryHue   = Clothing 主色
//   secondaryHue = Skirt / Hair 协调色
//   accentHue    = Decor01 点缀
//   accent2Hue   = Decor02 高亮
//   glowHue      = Decor02 发光备用 (与 accent2 接近, 微偏)
//   metalTone    = WeaponMetal 固定色
//   mood         = 风格氛围 (影响饱和/亮度偏移)
//   saturationBias / lightnessBias = 全方案细微偏移, [-30, +30] / [-20, +20]
struct SchemePalette {
    int          primaryHue     = 0;       // 0..359
    int          secondaryHue   = 0;
    int          accentHue      = 0;
    int          accent2Hue     = 0;
    int          glowHue        = 0;
    MetalTone    metal          = MetalTone::Silver;
    StyleMood    mood           = StyleMood::Realistic;
    ClothingTone clothing       = ClothingTone::DarkBlue;   // P0 v5: 服装 profile 选择
    int          saturationBias = 0;       // [-30, +30]
    int          lightnessBias  = 0;       // [-20, +20]
};

// 27 风格槽位定义. 用于按 schemeIdx 选风格.
struct SchemeStyleDef {
    const char*  name;
    int          primaryHue;
    int          secondaryHue;
    int          accentHue;
    int          accent2Hue;
    MetalTone    metal;
    StyleMood    mood;
    ClothingTone clothing;          // P0 v5
};

constexpr int kSchemeStyleCount = 27;
extern const std::array<SchemeStyleDef, kSchemeStyleCount> kSchemeStyles;

// 按 schemeIdx (1..N, 0 是本体不参与) 取 kSchemeStyles[(idx-1) % 27] 风格,
// 然后用 seed 在风格基础上做 ±10° 微抖动, 生成 SchemePalette.
//   schemeIdx <= 0   → 按 idx 0 处理, 取第 1 套风格 (兜底)
//   seed == 0        → 用全局随机源
SchemePalette generatePalette(int schemeIdx, quint32 seed = 0);

// --- enum string helper (JSON 用) ---
QString      styleMoodToString(StyleMood m);
StyleMood    styleMoodFromString(const QString& s);
QString      metalToneToString(MetalTone t);
MetalTone    metalToneFromString(const QString& s);
QString      clothingToneToString(ClothingTone c);
ClothingTone clothingToneFromString(const QString& s);

} // namespace HighPro
