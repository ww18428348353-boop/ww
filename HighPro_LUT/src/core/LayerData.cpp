#include "LayerData.h"

namespace HighPro {

QString LayerData::key() const
{
    switch (kind) {
    case LayerKind::Body:     return QStringLiteral("body");
    case LayerKind::Numbered: return QString("num_%1").arg(numberedIdx, 2, 10, QChar('0'));
    case LayerKind::Addon:    return QString("addon_%1").arg(addonSubIdx, 2, 10, QChar('0'));
    }
    return {};
}

// === LayerSlot helper ===
QString layerSlotToString(LayerSlot slot)
{
    switch (slot) {
    case LayerSlot::Unknown:        return QStringLiteral("Unknown");
    case LayerSlot::Skin:           return QStringLiteral("Skin");
    case LayerSlot::Hair:           return QStringLiteral("Hair");
    case LayerSlot::Clothing:       return QStringLiteral("Clothing");
    case LayerSlot::Skirt:          return QStringLiteral("Skirt");
    case LayerSlot::Decor01:        return QStringLiteral("Decor01");
    case LayerSlot::Decor02:        return QStringLiteral("Decor02");
    case LayerSlot::WeaponMetal:    return QStringLiteral("WeaponMetal");
    case LayerSlot::WeaponNonMetal: return QStringLiteral("WeaponNonMetal");
    }
    return QStringLiteral("Unknown");
}

LayerSlot layerSlotFromString(const QString& s)
{
    if (s == QLatin1String("Skin"))           return LayerSlot::Skin;
    if (s == QLatin1String("Hair"))           return LayerSlot::Hair;
    if (s == QLatin1String("Clothing"))       return LayerSlot::Clothing;
    if (s == QLatin1String("Skirt"))          return LayerSlot::Skirt;
    if (s == QLatin1String("Decor01"))        return LayerSlot::Decor01;
    if (s == QLatin1String("Decor02"))        return LayerSlot::Decor02;
    if (s == QLatin1String("WeaponMetal"))    return LayerSlot::WeaponMetal;
    if (s == QLatin1String("WeaponNonMetal")) return LayerSlot::WeaponNonMetal;
    return LayerSlot::Unknown;
}

QString layerSlotEmoji(LayerSlot slot)
{
    switch (slot) {
    case LayerSlot::Skin:           return QStringLiteral("🛡");
    case LayerSlot::Hair:           return QStringLiteral("💇");
    case LayerSlot::Clothing:       return QStringLiteral("👕");
    case LayerSlot::Skirt:          return QStringLiteral("👗");
    case LayerSlot::Decor01:        return QStringLiteral("💎");
    case LayerSlot::Decor02:        return QStringLiteral("⚡");
    case LayerSlot::WeaponMetal:    return QStringLiteral("⚔");
    case LayerSlot::WeaponNonMetal: return QStringLiteral("🪵");
    case LayerSlot::Unknown:        return QStringLiteral("❓");
    }
    return QStringLiteral("❓");
}

QString layerSlotDisplayName(LayerSlot slot)
{
    switch (slot) {
    case LayerSlot::Unknown:        return QStringLiteral("自动 / 未知");
    case LayerSlot::Skin:           return QStringLiteral("肤色");
    case LayerSlot::Hair:           return QStringLiteral("头发");
    case LayerSlot::Clothing:       return QStringLiteral("服装");
    case LayerSlot::Skirt:          return QStringLiteral("裙摆");
    case LayerSlot::Decor01:        return QStringLiteral("装饰 01");
    case LayerSlot::Decor02:        return QStringLiteral("装饰 02 / 发光");
    case LayerSlot::WeaponMetal:    return QStringLiteral("武器金属");
    case LayerSlot::WeaponNonMetal: return QStringLiteral("武器非金属");
    }
    return QStringLiteral("未知");
}

QString stripLayerSlotPrefix(QString text)
{
    // 9 个 emoji + 后跟一个空格 (与 LayerTreePanel 拼接格式 "emoji 名" 对应).
    // 用循环防止多次刷新累积多重前缀.
    static const QString kEmojis[] = {
        QStringLiteral("🛡"),  QStringLiteral("💇"),
        QStringLiteral("👕"),  QStringLiteral("👗"),
        QStringLiteral("💎"),  QStringLiteral("⚡"),
        QStringLiteral("⚔"),   QStringLiteral("🪵"),
        QStringLiteral("❓"),
    };
    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& e : kEmojis) {
            if (text.startsWith(e)) {
                text.remove(0, e.size());
                while (text.startsWith(QLatin1Char(' '))) text.remove(0, 1);
                changed = true;
            }
        }
    }
    return text;
}

} // namespace HighPro
