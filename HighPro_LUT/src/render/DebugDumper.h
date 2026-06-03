#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <QString>

namespace HighPro {

using Microsoft::WRL::ComPtr;

// 调试: 把 GPU 上某 RTV 的内容读回 CPU, 存为 PNG (RGBA 8bit).
// 用于排查"我们渲染" vs "AE 参考"的颜色差异.
class DebugDumper
{
public:
    // 1) 抓当前 RTV (从 SwapChain back buffer 拿 staging)
    static bool dumpRtv(ID3D11RenderTargetView* rtv,
                        int width, int height,
                        const QString& outPath,
                        QString* errorOut = nullptr);

    // 2) 离线 reference: 用 stb 加载帧 TGA + LUT PNG, CPU 三线性查找,
    //    输出 reference.png. 与 GPU 输出对比, 误差应在 ±2/255 以内.
    static bool referenceCpuLut(const QString& framePath,
                                const QString& lutPath,
                                const QString& outPath,
                                QString* errorOut = nullptr);
};

} // namespace HighPro
