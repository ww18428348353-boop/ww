#include "D3D11Texture.h"
#include "D3D11Context.h"
#include "core/PathUtil.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace HighPro {

bool D3D11Texture::createFromRGBA(const uint8_t* pixels, int width, int height,
                                  DXGI_FORMAT fmt, QString* errorOut)
{
    release();
    if (!pixels || width <= 0 || height <= 0) {
        if (errorOut) *errorOut = "无效的像素数据";
        return false;
    }
    auto* dev = D3D11Context::instance().device();
    if (!dev) {
        if (errorOut) *errorOut = "D3D11 设备未初始化";
        return false;
    }

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width  = (UINT)width;
    desc.Height = (UINT)height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format    = fmt;
    desc.SampleDesc.Count   = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage     = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags      = 0;

    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem     = pixels;
    init.SysMemPitch = (UINT)width * 4;

    HRESULT hr = dev->CreateTexture2D(&desc, &init, m_tex.GetAddressOf());
    if (FAILED(hr)) {
        if (errorOut) *errorOut = QString("CreateTexture2D 失败 0x%1")
                                  .arg((quint32)hr, 8, 16, QChar('0'));
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = fmt;
    sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    sd.Texture2D.MipLevels = 1;
    hr = dev->CreateShaderResourceView(m_tex.Get(), &sd, m_srv.GetAddressOf());
    if (FAILED(hr)) {
        m_tex.Reset();
        if (errorOut) *errorOut = QString("CreateSRV 失败 0x%1")
                                  .arg((quint32)hr, 8, 16, QChar('0'));
        return false;
    }
    m_width  = width;
    m_height = height;
    return true;
}

bool D3D11Texture::loadFromMemory(const uint8_t* data, int size, QString* errorOut,
                                  bool premultiply)
{
    int w = 0, h = 0, comp = 0;
    stbi_uc* px = stbi_load_from_memory(data, size, &w, &h, &comp, 4);
    if (!px) {
        if (errorOut) *errorOut = QString("stbi_load_from_memory 失败: %1")
                                  .arg(stbi_failure_reason());
        return false;
    }
    if (premultiply) {
        // CPU 端预乘 alpha: rgb_premul = rgb_straight * a / 255 (round-half-up).
        // 帧 TGA 必开 → GPU 双线性采样在预乘空间做, 半透明边缘不出色块.
        const int total = w * h;
        unsigned char* p = px;
        for (int k = 0; k < total; ++k, p += 4) {
            const unsigned int a = p[3];
            if (a == 0) {
                p[0] = p[1] = p[2] = 0;
            } else if (a < 255) {
                p[0] = (unsigned char)((p[0] * a + 127) / 255);
                p[1] = (unsigned char)((p[1] * a + 127) / 255);
                p[2] = (unsigned char)((p[2] * a + 127) / 255);
            }
        }
    }
    bool ok = createFromRGBA(px, w, h, DXGI_FORMAT_R8G8B8A8_UNORM, errorOut);
    stbi_image_free(px);
    return ok;
}

bool D3D11Texture::loadFromPath(const QString& path, QString* errorOut, bool premultiply)
{
    QByteArray buf = PathUtil::readAll(path);
    if (buf.isEmpty()) {
        if (errorOut) *errorOut = QString("读文件失败: %1").arg(path);
        return false;
    }
    return loadFromMemory((const uint8_t*)buf.constData(), buf.size(), errorOut, premultiply);
}

void D3D11Texture::release()
{
    m_srv.Reset();
    m_tex.Reset();
    m_width = m_height = 0;
}

} // namespace HighPro
