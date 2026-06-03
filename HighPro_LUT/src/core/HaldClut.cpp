#include "HaldClut.h"
#include "PathUtil.h"
#include "stb_image.h"

#include <QFile>
#include <cstring>

namespace HighPro {

bool HaldClut::loadAsTexture(const QString& path, D3D11Texture& outTex, QString* errorOut)
{
    QByteArray buf = PathUtil::readAll(path);
    if (buf.isEmpty()) {
        if (errorOut) *errorOut = QString("LUT 文件读取失败: %1").arg(path);
        return false;
    }

    int w = 0, h = 0, comp = 0;
    stbi_uc* px = stbi_load_from_memory(
        (const stbi_uc*)buf.constData(), buf.size(), &w, &h, &comp, 4);
    if (!px) {
        if (errorOut) *errorOut = QString("PNG 解码失败: %1 (%2)")
                                  .arg(path).arg(stbi_failure_reason());
        return false;
    }

    if (w != kTexW || h != kTexH) {
        stbi_image_free(px);
        if (errorOut) *errorOut = QString("LUT 尺寸非法 %1×%2, 应为 %3×%4: %5")
                                  .arg(w).arg(h).arg(kTexW).arg(kTexH).arg(path);
        return false;
    }

    bool ok = outTex.createFromRGBA(px, w, h, DXGI_FORMAT_R8G8B8A8_UNORM, errorOut);
    stbi_image_free(px);
    return ok;
}

bool HaldClut::loadDefaultAsTexture(D3D11Texture& outTex, QString* errorOut)
{
    QFile f(":/lut/default.png");
    if (!f.open(QIODevice::ReadOnly)) {
        if (errorOut) *errorOut = "找不到默认 LUT 资源 :/lut/default.png";
        return false;
    }
    QByteArray buf = f.readAll();
    f.close();

    int w = 0, h = 0, comp = 0;
    stbi_uc* px = stbi_load_from_memory(
        (const stbi_uc*)buf.constData(), buf.size(), &w, &h, &comp, 4);
    if (!px) {
        if (errorOut) *errorOut = QString("默认 LUT 解码失败: %1").arg(stbi_failure_reason());
        return false;
    }
    if (w != kTexW || h != kTexH) {
        stbi_image_free(px);
        if (errorOut) *errorOut = QString("默认 LUT 尺寸异常 %1×%2").arg(w).arg(h);
        return false;
    }
    bool ok = outTex.createFromRGBA(px, w, h, DXGI_FORMAT_R8G8B8A8_UNORM, errorOut);
    stbi_image_free(px);
    return ok;
}

void HaldClut::enforceShadowLock(uint8_t* rgba)
{
    if (!rgba) return;
    rgba[0] = 0; rgba[1] = 0; rgba[2] = 0; /*alpha 不动*/
}

bool HaldClut::isDefaultHaldEncoded(const uint8_t* rgba, int* mismatchOut)
{
    if (!rgba) return false;
    int mismatch = 0;
    for (int y = 0; y < kTexH; ++y) {
        for (int x = 0; x < kTexW; ++x) {
            const uint8_t* p = rgba + (y * kTexW + x) * 4;
            const int r = (x % 16) * 17;
            const int g = y * 17;
            const int b = (x / 16) * 17;
            if (std::abs((int)p[0] - r) > 2 ||
                std::abs((int)p[1] - g) > 2 ||
                std::abs((int)p[2] - b) > 2) {
                ++mismatch;
            }
        }
    }
    if (mismatchOut) *mismatchOut = mismatch;
    return mismatch < 16;
}

} // namespace HighPro
