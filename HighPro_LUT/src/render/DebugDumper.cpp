#include "DebugDumper.h"
#include "D3D11Context.h"
#include "core/PathUtil.h"

#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <QFile>
#include <QDebug>
#include <vector>
#include <cmath>

namespace HighPro {

bool DebugDumper::dumpRtv(ID3D11RenderTargetView* rtv,
                          int width, int height,
                          const QString& outPath,
                          QString* errorOut)
{
    if (!rtv) { if (errorOut) *errorOut = "rtv null"; return false; }
    auto* dev = D3D11Context::instance().device();
    auto* dc  = D3D11Context::instance().context();
    if (!dev || !dc) { if (errorOut) *errorOut = "dev null"; return false; }

    // 1) 拿 RTV 后台资源
    ComPtr<ID3D11Resource> res;
    rtv->GetResource(res.GetAddressOf());
    ComPtr<ID3D11Texture2D> srcTex;
    if (FAILED(res.As(&srcTex))) { if (errorOut) *errorOut = "as Texture2D"; return false; }

    D3D11_TEXTURE2D_DESC desc{};
    srcTex->GetDesc(&desc);

    // 2) 创建 staging
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(dev->CreateTexture2D(&desc, nullptr, staging.GetAddressOf()))) {
        if (errorOut) *errorOut = "create staging"; return false;
    }

    dc->CopyResource(staging.Get(), srcTex.Get());

    D3D11_MAPPED_SUBRESOURCE map{};
    if (FAILED(dc->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map))) {
        if (errorOut) *errorOut = "map"; return false;
    }

    // back buffer 是 BGRA8, 转 RGBA + flip 不需要(本来就是 top-down)
    std::vector<uint8_t> rgba((size_t)width * height * 4);
    const uint8_t* src = (const uint8_t*)map.pData;
    for (int y = 0; y < height; ++y) {
        const uint8_t* row = src + y * map.RowPitch;
        uint8_t* dst = rgba.data() + (size_t)y * width * 4;
        for (int x = 0; x < width; ++x) {
            dst[x*4 + 0] = row[x*4 + 2]; // R
            dst[x*4 + 1] = row[x*4 + 1]; // G
            dst[x*4 + 2] = row[x*4 + 0]; // B
            dst[x*4 + 3] = row[x*4 + 3]; // A
        }
    }
    dc->Unmap(staging.Get(), 0);

    // stbi_write_png 中文路径不友好, 写到内存再 QFile.
    int outLen = 0;
    unsigned char* png = stbi_write_png_to_mem(rgba.data(), width * 4,
                                               width, height, 4, &outLen);
    if (!png || outLen <= 0) { if (errorOut) *errorOut = "encode png"; return false; }

    bool ok = PathUtil::writeAll(outPath, QByteArray((const char*)png, outLen));
    STBIW_FREE(png);
    if (!ok && errorOut) *errorOut = "write file";
    return ok;
}

namespace {

inline uint8_t clamp255(float f) {
    int v = (int)std::lroundf(f);
    if (v < 0)   v = 0;
    if (v > 255) v = 255;
    return (uint8_t)v;
}

// CPU HALD-CLUT 三线性查找 (与 shader 算法严格一致)
void cpuLut(const uint8_t* lut256x16, uint8_t r, uint8_t g, uint8_t b,
            uint8_t& or_, uint8_t& og, uint8_t& ob)
{
    const float fr = r / 255.0f;
    const float fg = g / 255.0f;
    const float fb = b / 255.0f;

    const float qr = fr * 15.0f;
    const float qg = fg * 15.0f;
    const float qb = fb * 15.0f;

    const int b0 = (int)std::floor(qb);
    const int b1 = std::min(b0 + 1, 15);
    const float bf = qb - (float)b0;

    // R/G 双线性 (sampler 等价)
    const int r0 = (int)std::floor(qr);
    const int r1 = std::min(r0 + 1, 15);
    const float rf = qr - (float)r0;
    const int g0 = (int)std::floor(qg);
    const int g1 = std::min(g0 + 1, 15);
    const float gf = qg - (float)g0;

    auto sampleSlice = [&](int bSlice) -> std::tuple<float,float,float> {
        const int x00 = bSlice * 16 + r0;
        const int x10 = bSlice * 16 + r1;
        const auto px = [&](int x, int y) {
            const uint8_t* p = lut256x16 + (y * 256 + x) * 4;
            return std::tuple<float,float,float>(p[0], p[1], p[2]);
        };
        auto [a_r, a_g, a_b] = px(x00, g0);
        auto [b_r, b_g, b_b] = px(x10, g0);
        auto [c_r, c_g, c_b] = px(x00, g1);
        auto [d_r, d_g, d_b] = px(x10, g1);
        // bilinear in (rf, gf)
        float t0r = a_r + (b_r - a_r) * rf;
        float t0g = a_g + (b_g - a_g) * rf;
        float t0b = a_b + (b_b - a_b) * rf;
        float t1r = c_r + (d_r - c_r) * rf;
        float t1g = c_g + (d_g - c_g) * rf;
        float t1b = c_b + (d_b - c_b) * rf;
        return { t0r + (t1r - t0r) * gf,
                 t0g + (t1g - t0g) * gf,
                 t0b + (t1b - t0b) * gf };
    };

    auto [ar, ag, ab] = sampleSlice(b0);
    auto [br, bg, bb] = sampleSlice(b1);
    or_ = clamp255(ar + (br - ar) * bf);
    og  = clamp255(ag + (bg - ag) * bf);
    ob  = clamp255(ab + (bb - ab) * bf);
}

} // namespace

bool DebugDumper::referenceCpuLut(const QString& framePath,
                                  const QString& lutPath,
                                  const QString& outPath,
                                  QString* errorOut)
{
    QByteArray fbuf = PathUtil::readAll(framePath);
    QByteArray lbuf = PathUtil::readAll(lutPath);
    if (fbuf.isEmpty() || lbuf.isEmpty()) {
        if (errorOut) *errorOut = "load file";
        return false;
    }

    int fw=0,fh=0,fc=0;
    stbi_uc* fpx = stbi_load_from_memory((stbi_uc*)fbuf.data(), fbuf.size(), &fw, &fh, &fc, 4);
    int lw=0,lh=0,lc=0;
    stbi_uc* lpx = stbi_load_from_memory((stbi_uc*)lbuf.data(), lbuf.size(), &lw, &lh, &lc, 4);
    if (!fpx || !lpx || lw != 256 || lh != 16) {
        if (fpx) stbi_image_free(fpx);
        if (lpx) stbi_image_free(lpx);
        if (errorOut) *errorOut = "decode or LUT size";
        return false;
    }

    std::vector<uint8_t> out((size_t)fw * fh * 4);
    for (int i = 0; i < fw * fh; ++i) {
        const uint8_t* p = fpx + i * 4;
        const uint8_t a = p[3];
        if (a == 0) {
            out[i*4 + 0] = p[0];
            out[i*4 + 1] = p[1];
            out[i*4 + 2] = p[2];
            out[i*4 + 3] = 0;
            continue;
        }
        uint8_t mr, mg, mb;
        cpuLut(lpx, p[0], p[1], p[2], mr, mg, mb);
        out[i*4 + 0] = mr;
        out[i*4 + 1] = mg;
        out[i*4 + 2] = mb;
        out[i*4 + 3] = a;
    }
    stbi_image_free(fpx);
    stbi_image_free(lpx);

    int outLen = 0;
    unsigned char* png = stbi_write_png_to_mem(out.data(), fw * 4, fw, fh, 4, &outLen);
    if (!png || outLen <= 0) { if (errorOut) *errorOut = "encode"; return false; }
    bool ok = PathUtil::writeAll(outPath, QByteArray((const char*)png, outLen));
    STBIW_FREE(png);
    return ok;
}

} // namespace HighPro
