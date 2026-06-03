#pragma once

#include <QPoint>
#include <QSize>

namespace HighPro {

// PP/大话西游 角色 TGA 的 "K 点" (绘制锚点, 角色脚下中心点) 经验表.
// 来源: 截图四 PP 官方 K 点确定值. 按 TGA 尺寸查表, 不在表内时按比例外推.
struct KPoint
{
    int w; int h;
    int kx; int ky;
};

inline QPoint kPointForSize(const QSize& sz)
{
    static const KPoint kTable[] = {
        {  320,  320, 160,  225 },
        {  500,  500, 250,  343 },
        { 1000, 1000, 500,  592 },
        { 1500, 1500, 750, 1200 },
    };
    // 完全匹配
    for (const auto& e : kTable) {
        if (sz.width() == e.w && sz.height() == e.h) {
            return { e.kx, e.ky };
        }
    }
    // 兜底: x 居中, y 在 0.7 高度处 (经验近似)
    return { sz.width() / 2, (int)(sz.height() * 0.7) };
}

} // namespace HighPro
