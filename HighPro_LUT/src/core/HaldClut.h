#pragma once

#include "render/D3D11Texture.h"
#include <QString>
#include <QByteArray>
#include <memory>

namespace HighPro {

// HALD-CLUT (16×16×16 → 256×16 平铺) 工具.
//
// 默认编码 (颜色图.png 实证):
//   像素 (x, y), x∈[0,255], y∈[0,15]:
//     R_in = (x mod 16) * 17
//     G_in = y * 17
//     B_in = (x / 16) * 17
//
// 一张 add_lut/0N.png 即对应输入色经"7 效果链"映射后的输出.
// 运行时变色 = 用源像素 (R,G,B) 在 add_lut 上做 HALD 三线性查找.
class HaldClut
{
public:
    static constexpr int kSize     = 16;            // 16×16×16
    static constexpr int kTexW     = 256;           // 16 * 16 (B 切片横铺)
    static constexpr int kTexH     = 16;

    // 把 add_lut/0N.png (或默认颜色图.png) 加载为 GPU 纹理.
    // 自动校验: 必须是 256×16. 不符合返回 false.
    static bool loadAsTexture(const QString& utf8Path,
                              D3D11Texture& outTex,
                              QString* errorOut = nullptr);

    // 加载 Qt 资源中的默认 :/lut/default.png 作 LUT.
    static bool loadDefaultAsTexture(D3D11Texture& outTex,
                                     QString* errorOut = nullptr);

    // 强制锁定 (0,0) 角点像素 = (0,0,0): 无论效果链如何, 影子 (RGB≈0) 永远黑.
    // (M4 LutBaker 会在烘焙后调用; M3 加载阶段先不强制, 保留 add_lut 原值)
    static void enforceShadowLock(uint8_t* rgba256x16);

    // 验证一张 256×16 PNG 是否符合默认 HALD 编码 (调试用).
    static bool isDefaultHaldEncoded(const uint8_t* rgba256x16, int* mismatchOut = nullptr);
};

} // namespace HighPro
