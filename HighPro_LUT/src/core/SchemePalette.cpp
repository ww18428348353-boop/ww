#include "SchemePalette.h"

#include <QRandomGenerator>
#include <random>
#include <algorithm>

namespace HighPro {

// 27 套风格槽位.
// 命名 / 配色参考 `服装智能随机颜色优化计划.md` 第十六节.
//
// P0 v5: 服装主色池强力偏暗 — 1~20 全部暗色 (DarkBlue/Red/Purple/Green/Teal/Gray/Brown),
//        21~23 LightNoble (高级亮色), 24~27 RareVivid (稀有彩色).
//        命中 100~200 个候选时, 大概率刷出"暗蓝/暗红/暗紫/暗绿/暗黑灰"高级方案.
//
// hue 是 HSV 0..359 度:
//   0=红 30=橙 60=黄 90=黄绿 120=绿 150=青绿 180=青
//   210=蓝青 240=蓝 270=紫 300=品红 330=粉红
const std::array<SchemeStyleDef, kSchemeStyleCount> kSchemeStyles = { {
    // idx  name              pri  sec  acc  acc2  metal              mood                  clothing
    { "暗蓝银",                220, 200, 210, 220, MetalTone::Silver,    StyleMood::DarkVivid, ClothingTone::DarkBlue   },  //  1
    { "深紫银",                280, 260, 320, 290, MetalTone::Silver,    StyleMood::DarkVivid, ClothingTone::DarkPurple },  //  2
    { "黑红金",                  0,  10,  45, 320, MetalTone::Gold,      StyleMood::DarkVivid, ClothingTone::DarkRed    },  //  3
    { "墨绿金",                140, 130,  45, 120, MetalTone::Gold,      StyleMood::DarkVivid, ClothingTone::DarkGreen  },  //  4
    { "黑银",                  220, 200, 210, 220, MetalTone::Silver,    StyleMood::Dark,      ClothingTone::DarkGray   },  //  5
    { "深海蓝",                225, 200, 180, 220, MetalTone::Silver,    StyleMood::DarkVivid, ClothingTone::DarkBlue   },  //  6
    { "紫黑魔纹",              285, 265, 200, 290, MetalTone::DarkGold,  StyleMood::DarkVivid, ClothingTone::DarkPurple },  //  7
    { "暗红黑铁",                5,  15,  45, 320, MetalTone::BlackIron, StyleMood::DarkVivid, ClothingTone::DarkRed    },  //  8
    { "青黑幽光",              190, 200, 170, 195, MetalTone::BlueSteel, StyleMood::DarkVivid, ClothingTone::DeepTeal   },  //  9
    { "黑金",                  240,  35,  45,  50, MetalTone::Gold,      StyleMood::DarkVivid, ClothingTone::DarkGray   },  // 10
    { "毒雾绿",                125, 145,  60, 120, MetalTone::DarkGold,  StyleMood::DarkVivid, ClothingTone::DarkGreen  },  // 11
    { "冰蓝黑",                215, 200, 200, 220, MetalTone::BlueSteel, StyleMood::Dark,      ClothingTone::DarkBlue   },  // 12
    { "蓝紫星空",              250, 270, 200, 280, MetalTone::Silver,    StyleMood::DarkVivid, ClothingTone::DarkPurple },  // 13
    { "暗铜棕",                 30,  45, 200,  50, MetalTone::Copper,    StyleMood::DarkVivid, ClothingTone::DarkBrown  },  // 14
    { "黑白无常",                0,   0,   0,   0, MetalTone::Silver,    StyleMood::Mono,      ClothingTone::DarkGray   },  // 15
    { "熔岩暗红",                8,  15,  45,   0, MetalTone::Copper,    StyleMood::DarkVivid, ClothingTone::DarkRed    },  // 16
    { "森林暗绿",              135, 110,  45, 130, MetalTone::DarkGold,  StyleMood::DarkVivid, ClothingTone::DarkGreen  },  // 17
    { "夜蓝金",                225,  45,  50, 220, MetalTone::Gold,      StyleMood::DarkVivid, ClothingTone::DarkBlue   },  // 18
    { "暗紫粉光",              285, 330, 200, 290, MetalTone::Silver,    StyleMood::DarkVivid, ClothingTone::DarkPurple },  // 19
    { "冷灰银",                220, 200, 210, 220, MetalTone::Silver,    StyleMood::Dark,      ClothingTone::DarkGray   },  // 20
    { "白金",                   45, 200,  50,  45, MetalTone::Gold,      StyleMood::Noble,     ClothingTone::LightNoble },  // 21
    { "冰白蓝",                195, 215, 200, 220, MetalTone::Silver,    StyleMood::Cold,      ClothingTone::LightNoble },  // 22
    { "珍珠粉",                330,   0,  45, 320, MetalTone::Silver,    StyleMood::Fantasy,   ClothingTone::LightNoble },  // 23
    { "宝石蓝",                225, 200, 180, 240, MetalTone::Silver,    StyleMood::BrightVivid, ClothingTone::RareVivid },// 24
    { "玫红紫",                315, 290, 200, 320, MetalTone::Gold,      StyleMood::BrightVivid, ClothingTone::RareVivid },// 25
    { "青绿幻彩",              160, 180,  60, 170, MetalTone::BlueSteel, StyleMood::Fantasy,     ClothingTone::RareVivid },// 26
    { "彩蛋霓虹",              300, 180,  60,  30, MetalTone::Silver,    StyleMood::Vivid,       ClothingTone::RareVivid },// 27
} };

namespace {

// hue 微抖动: ±range, 归一到 0..359.
int jitterHue(std::mt19937& rng, int base, int range)
{
    if (range <= 0) return ((base % 360) + 360) % 360;
    std::uniform_int_distribution<int> d(-range, range);
    int h = base + d(rng);
    h = ((h % 360) + 360) % 360;
    return h;
}

// hue 从 [lo, hi] 区间随机一个 (允许跨 360 边界, 如 350~15)
int randHueRange(std::mt19937& rng, int lo, int hi)
{
    if (lo <= hi) {
        std::uniform_int_distribution<int> d(lo, hi);
        return d(rng);
    }
    std::uniform_int_distribution<int> d(lo, hi + 360);
    return d(rng) % 360;
}

// ==========================================================================
// P0 v7: 全色相覆盖 + sat/lgt 高方差抽样
// ==========================================================================
//
// 用户诉求 (2026-06):
//   - 色相 0..360° 全覆盖, 红/橙/黄/绿/青/蓝/紫/粉每类都要有
//   - 饱和度 10%~80% 全分布
//   - 亮度 2%~99% 全分布
//   - 不能 93% 都是暗色, 要丰富度跨度大
//
// 实现:
//   1) 12 色相桶 (30°/桶), 每桶都有出场概率
//   2) 亮度档 暗 30% / 中 40% / 亮 30%
//   3) 饱和档 低 25% / 中 45% / 高 30%
//   4) palette 不再用 ClothingTone 强绑 hue, 改用色相桶 → tone 推断 (兼容旧字段)

// 12 色相桶中心点 (用于 jitterHue ±18 抖)
constexpr int kHueBucketCount = 12;
constexpr int kHueBucketCenter[kHueBucketCount] = {
    0,   30,  60,  90,  120, 150,
    180, 210, 240, 270, 300, 330,
};

// 色相桶 → ClothingTone (仅用于 ColorBalance / PhotoFilter / metal 选择)
ClothingTone bucketToTone(int bucket)
{
    switch (bucket) {
    case 0:  return ClothingTone::DarkRed;     // 红
    case 1:  return ClothingTone::DarkBrown;   // 红橙
    case 2:  return ClothingTone::DarkBrown;   // 橙黄
    case 3:  return ClothingTone::LightNoble;  // 黄 (黄色衣服多偏淡)
    case 4:  return ClothingTone::DarkGreen;   // 黄绿
    case 5:  return ClothingTone::DarkGreen;   // 绿
    case 6:  return ClothingTone::DeepTeal;    // 青绿
    case 7:  return ClothingTone::DeepTeal;    // 青蓝
    case 8:  return ClothingTone::DarkBlue;    // 蓝
    case 9:  return ClothingTone::DarkPurple;  // 蓝紫
    case 10: return ClothingTone::DarkPurple;  // 紫品
    case 11: return ClothingTone::DarkRed;     // 粉品 (用红的 ColorBalance, 偏暖)
    }
    return ClothingTone::DarkBlue;
}

// 按权重抽桶: 12 桶基本均匀, 略微给冷色 (蓝/紫/青) 多一点点权重 (用户偏好)
int pickHueBucket(std::mt19937& rng)
{
    // 12 桶权重:  红 橙红 橙黄 黄  黄绿 绿  青绿 青蓝 蓝  蓝紫 紫品 粉品
    constexpr int w[kHueBucketCount] = {
        9, 8, 7, 7, 8, 9, 8, 9, 10, 10, 9, 6
    };
    int total = 0;
    for (int v : w) total += v;
    std::uniform_int_distribution<int> d(0, total - 1);
    int r = d(rng);
    int acc = 0;
    for (int i = 0; i < kHueBucketCount; ++i) {
        acc += w[i];
        if (r < acc) return i;
    }
    return 0;
}

// 亮度档 (6 档, P0 v10 加 Grayscale 独立档 + 降亮档概率)
//   灰度 7%  : 彻底去色, 只剩亮度变化 (跨深灰/中灰/亮白灰)
//   极暗 17% : 黑色服装
//   暗  28% : 深色服装
//   中  32% : 原色级
//   亮  13% : 浅色服装
//   极亮 3% : 白色服装 (稀有)
enum class LgtLevel { Grayscale = 0, UltraDark = 1, Dark = 2, Mid = 3, Bright = 4, UltraBright = 5 };

LgtLevel pickLgtLevel(std::mt19937& rng)
{
    std::uniform_int_distribution<int> d(0, 99);
    int r = d(rng);
    if (r < 7)  return LgtLevel::Grayscale;
    if (r < 24) return LgtLevel::UltraDark;
    if (r < 52) return LgtLevel::Dark;
    if (r < 84) return LgtLevel::Mid;
    if (r < 97) return LgtLevel::Bright;
    return LgtLevel::UltraBright;
}

// 饱和档 (3 档)
//   低饱 20% : 灰白服装
//   中饱 45% : 写实色
//   高饱 35% : 鲜艳色
enum class SatLevel { Low = 0, Mid = 1, High = 2 };

SatLevel pickSatLevel(std::mt19937& rng)
{
    std::uniform_int_distribution<int> d(0, 99);
    int r = d(rng);
    if (r < 20) return SatLevel::Low;
    if (r < 65) return SatLevel::Mid;
    return SatLevel::High;
}

// 把 (LgtLevel, SatLevel) 编码到 saturationBias / lightnessBias 字段中,
// 供 applyClothing 读取. 占用约定:
//   lightnessBias  = 100 * (int)lgtLevel + 真实抖动 (-9..+9)
//   saturationBias = 100 * (int)satLevel + 真实抖动 (-9..+9)
// 解码: level = bias / 100, jitter = bias % 100 (符号处理见 applyClothing)
//
// 这样既复用现有字段, 又不破坏 JSON 兼容.
int encodeLgtBias(LgtLevel lvl, int jitter)
{
    return 100 * (int)lvl + std::clamp(jitter, -9, 9);
}
int encodeSatBias(SatLevel lvl, int jitter)
{
    return 100 * (int)lvl + std::clamp(jitter, -9, 9);
}

// 装饰色: 跟主色互补, 加一点冷暖二选一
int pickAccentHue(std::mt19937& rng, int primaryHue)
{
    return jitterHue(rng, primaryHue + 180, 30);
}

int pickAccent2Hue(std::mt19937& rng)
{
    std::uniform_int_distribution<int> d(0, 1);
    return d(rng) ? jitterHue(rng, 45, 15)   // 暖金 45°
                  : jitterHue(rng, 195, 15); // 冷青 195°
}

// 按色相桶选金属
MetalTone metalForBucket(std::mt19937& rng, int bucket)
{
    std::uniform_int_distribution<int> d2(0, 1);
    std::uniform_int_distribution<int> d3(0, 2);
    // 冷色 (6-10) 倾向银 / 蓝钢, 暖色 (0-4) 倾向金 / 铜, 中性绿/紫倾向暗金
    if (bucket >= 7 && bucket <= 10) {
        return d2(rng) ? MetalTone::Silver : MetalTone::BlueSteel;
    }
    if (bucket >= 0 && bucket <= 3) {
        return d3(rng) == 0 ? MetalTone::Gold
             : d3(rng) == 1 ? MetalTone::Copper : MetalTone::DarkGold;
    }
    return d2(rng) ? MetalTone::Gold : MetalTone::Silver;
}

} // namespace

SchemePalette generatePalette(int schemeIdx, quint32 seed)
{
    // P0 v7: 完全随机抽样, 不再按 schemeIdx 固定风格
    Q_UNUSED(schemeIdx);

    std::mt19937 rng(seed != 0 ? seed : QRandomGenerator::global()->generate());

    SchemePalette p;

    // 1) 抽色相桶 → 主 hue (桶中心 ± 18°, 自然落在桶覆盖范围内)
    const int bucket = pickHueBucket(rng);
    p.primaryHue   = jitterHue(rng, kHueBucketCenter[bucket], 18);

    // 2) 抽亮度档 + 饱和档, 编码到 bias 字段
    const LgtLevel lgtLvl = pickLgtLevel(rng);
    const SatLevel satLvl = pickSatLevel(rng);
    std::uniform_int_distribution<int> djit(-9, 9);
    p.lightnessBias  = encodeLgtBias(lgtLvl, djit(rng));
    p.saturationBias = encodeSatBias(satLvl, djit(rng));

    // 3) tone (仅用于 ColorBalance / PhotoFilter 选向)
    p.clothing = bucketToTone(bucket);
    // mood / metal 跟随
    switch (lgtLvl) {
    case LgtLevel::Grayscale:   p.mood = StyleMood::Mono;       break;
    case LgtLevel::UltraDark:   p.mood = StyleMood::DarkVivid;  break;
    case LgtLevel::Dark:        p.mood = StyleMood::DarkVivid;  break;
    case LgtLevel::Mid:         p.mood = StyleMood::Warm;       break;
    case LgtLevel::Bright:      p.mood = StyleMood::Noble;      break;
    case LgtLevel::UltraBright: p.mood = StyleMood::Noble;      break;
    }
    p.metal = metalForBucket(rng, bucket);

    // 4) 辅色与点缀
    p.secondaryHue = jitterHue(rng, p.primaryHue, 20);  // 同色系深浅
    p.accentHue    = pickAccentHue(rng, p.primaryHue);  // 互补色
    p.accent2Hue   = pickAccent2Hue(rng);               // 暖金 / 冷青二选一
    p.glowHue      = jitterHue(rng, p.accent2Hue, 20);

    return p;
}

// === enum ↔ string ===
QString styleMoodToString(StyleMood m)
{
    switch (m) {
    case StyleMood::Realistic:   return QStringLiteral("Realistic");
    case StyleMood::Dark:        return QStringLiteral("Dark");
    case StyleMood::DarkVivid:   return QStringLiteral("DarkVivid");
    case StyleMood::Cold:        return QStringLiteral("Cold");
    case StyleMood::Warm:        return QStringLiteral("Warm");
    case StyleMood::Noble:       return QStringLiteral("Noble");
    case StyleMood::Fantasy:     return QStringLiteral("Fantasy");
    case StyleMood::BrightVivid: return QStringLiteral("BrightVivid");
    case StyleMood::Mono:        return QStringLiteral("Mono");
    case StyleMood::Vivid:       return QStringLiteral("Vivid");
    }
    return QStringLiteral("Realistic");
}

StyleMood styleMoodFromString(const QString& s)
{
    if (s == QLatin1String("Dark"))        return StyleMood::Dark;
    if (s == QLatin1String("DarkVivid"))   return StyleMood::DarkVivid;
    if (s == QLatin1String("Cold"))        return StyleMood::Cold;
    if (s == QLatin1String("Warm"))        return StyleMood::Warm;
    if (s == QLatin1String("Noble"))       return StyleMood::Noble;
    if (s == QLatin1String("Fantasy"))     return StyleMood::Fantasy;
    if (s == QLatin1String("BrightVivid")) return StyleMood::BrightVivid;
    if (s == QLatin1String("Mono"))        return StyleMood::Mono;
    if (s == QLatin1String("Vivid"))       return StyleMood::Vivid;
    return StyleMood::Realistic;
}

QString metalToneToString(MetalTone t)
{
    switch (t) {
    case MetalTone::Gold:      return QStringLiteral("Gold");
    case MetalTone::Silver:    return QStringLiteral("Silver");
    case MetalTone::Copper:    return QStringLiteral("Copper");
    case MetalTone::BlackIron: return QStringLiteral("BlackIron");
    case MetalTone::BlueSteel: return QStringLiteral("BlueSteel");
    case MetalTone::DarkGold:  return QStringLiteral("DarkGold");
    }
    return QStringLiteral("Silver");
}

MetalTone metalToneFromString(const QString& s)
{
    if (s == QLatin1String("Gold"))      return MetalTone::Gold;
    if (s == QLatin1String("Copper"))    return MetalTone::Copper;
    if (s == QLatin1String("BlackIron")) return MetalTone::BlackIron;
    if (s == QLatin1String("BlueSteel")) return MetalTone::BlueSteel;
    if (s == QLatin1String("DarkGold"))  return MetalTone::DarkGold;
    return MetalTone::Silver;
}

QString clothingToneToString(ClothingTone c)
{
    switch (c) {
    case ClothingTone::DarkBlue:   return QStringLiteral("DarkBlue");
    case ClothingTone::DarkRed:    return QStringLiteral("DarkRed");
    case ClothingTone::DarkPurple: return QStringLiteral("DarkPurple");
    case ClothingTone::DarkGreen:  return QStringLiteral("DarkGreen");
    case ClothingTone::DarkGray:   return QStringLiteral("DarkGray");
    case ClothingTone::DeepTeal:   return QStringLiteral("DeepTeal");
    case ClothingTone::DarkBrown:  return QStringLiteral("DarkBrown");
    case ClothingTone::LightNoble: return QStringLiteral("LightNoble");
    case ClothingTone::RareVivid:  return QStringLiteral("RareVivid");
    }
    return QStringLiteral("DarkBlue");
}

ClothingTone clothingToneFromString(const QString& s)
{
    if (s == QLatin1String("DarkRed"))    return ClothingTone::DarkRed;
    if (s == QLatin1String("DarkPurple")) return ClothingTone::DarkPurple;
    if (s == QLatin1String("DarkGreen"))  return ClothingTone::DarkGreen;
    if (s == QLatin1String("DarkGray"))   return ClothingTone::DarkGray;
    if (s == QLatin1String("DeepTeal"))   return ClothingTone::DeepTeal;
    if (s == QLatin1String("DarkBrown"))  return ClothingTone::DarkBrown;
    if (s == QLatin1String("LightNoble")) return ClothingTone::LightNoble;
    if (s == QLatin1String("RareVivid"))  return ClothingTone::RareVivid;
    return ClothingTone::DarkBlue;
}

} // namespace HighPro
