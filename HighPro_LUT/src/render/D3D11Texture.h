#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <QString>
#include <QSize>
#include <cstdint>

namespace HighPro {

using Microsoft::WRL::ComPtr;

// Texture2D + SRV 的轻量包装 (上传 RGBA8 像素 → GPU 纹理).
class D3D11Texture
{
public:
    bool createFromRGBA(const uint8_t* pixels, int width, int height,
                        DXGI_FORMAT fmt = DXGI_FORMAT_R8G8B8A8_UNORM,
                        QString* errorOut = nullptr);

    // 从 PNG/TGA 字节流加载 (内部用 stb_image).
    // premultiply=true: 加载后对 RGB 做 alpha 预乘 (帧 TGA 必开, LUT 颜色图必关).
    bool loadFromMemory(const uint8_t* data, int size, QString* errorOut = nullptr,
                        bool premultiply = false);

    // 从 utf-8 路径加载 (中文路径友好). premultiply 同 loadFromMemory.
    bool loadFromPath(const QString& path, QString* errorOut = nullptr,
                      bool premultiply = false);

    void release();
    bool isValid() const { return m_srv != nullptr; }

    ID3D11ShaderResourceView* srv() const { return m_srv.Get(); }
    ID3D11Texture2D*          texture() const { return m_tex.Get(); }
    QSize size() const { return { m_width, m_height }; }
    int   width()  const { return m_width; }
    int   height() const { return m_height; }

private:
    ComPtr<ID3D11Texture2D>          m_tex;
    ComPtr<ID3D11ShaderResourceView> m_srv;
    int m_width  = 0;
    int m_height = 0;
};

} // namespace HighPro
