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

} // namespace HighPro
