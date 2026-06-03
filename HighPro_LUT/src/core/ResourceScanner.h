#pragma once

#include "LayerData.h"
#include <QString>
#include <QVector>

namespace HighPro {

// 扫描资源根目录, 适配三种结构:
//   9353       : root/{addon/##/, 00, 01, 02, 03, 04, body}/<动作>/<方向帧>.tga
//   tianlongnnv: root/{addon/01, 00, body}/<动作>/<方向帧>.tga
//   fengxiong  : root/{00, 01, body}/<动作>/<方向帧>.tga (无 addon)
//
// 每层下:
//   - 子目录 = 动作 (stand/walk/attack/...)
//   - 动作目录下 *.tga 用正则 ^(\d)(\d{3})\.tga$ 解析方向(0..9)+帧号(000..999)
//   - <root>/add_lut/  (可选) 存方案 PNG
class ResourceScanner
{
public:
    struct Result
    {
        QString sourceRoot;
        QVector<LayerData> layers;     // 已按合成顺序 (body→00..04→addon) 排列
        QStringList   warnings;
        QString       error;           // 非空表示失败
        bool isOk() const { return error.isEmpty() && !layers.isEmpty(); }
    };

    static Result scan(const QString& sourceRoot);
};

} // namespace HighPro
