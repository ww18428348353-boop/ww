#pragma once

#include <QString>
#include <QSize>
#include <QColor>
#include <functional>

struct ID3D11RenderTargetView;       // 前向声明 (放 global namespace, 不放 HighPro)

namespace HighPro {

// 导出"当前动作 N 帧 × M 方案" 多 cell 网格 GIF.
// 渲染走 FrameRenderer 离屏 RTV → CPU readback → gif.h 写出.
class GifExporter
{
public:
    // GIF 质量模式
    enum QualityMode { Quality, Balanced, Size };

    // 抖动算法
    enum DitherMode  { FloydSteinberg, Sierra, Ordered, NoDither };

    struct Options
    {
        QString outPath;
        int     width  = 0;
        int     height = 0;
        int     fps    = 12;
        int     loopCount = 1;      // 0=无限
        QColor  bgColor{ 60, 60, 60 };
        bool    showLabel = true;

        // --- GIF 质量优化参数 ---
        QualityMode quality    = Quality;
        DitherMode  ditherMode = NoDither;       // 默认不抖动 (对齐 PIL ADAPTIVE 直接量化, 无噪点)
        float       ditherStrength = 0.0f;    // 0.0~1.0, 抖动强度 (仅 ditherMode!=NoDither 时生效)
        int         paletteSamples = 4;      // 每帧采样像素比例 (1/N), 越小采样越多
    };

    struct Result
    {
        bool    ok = false;
        QString error;
        int     framesWritten = 0;
        qint64  bytesWritten  = 0;
    };

    using SetFrameFn  = std::function<void(int frameIdx)>;
    using RenderRtvFn = std::function<void(ID3D11RenderTargetView* rtv,
                                           int rtvW, int rtvH)>;

    static Result exportGif(const Options& opts,
                            int frameCount,
                            const SetFrameFn&  setFrame,
                            const RenderRtvFn& renderOnce);
};

} // namespace HighPro
