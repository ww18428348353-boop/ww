#pragma once

#include "D3D11Shader.h"
#include "D3D11Texture.h"
#include "core/ColorEffect.h"

#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <QString>
#include <array>
#include <cstdint>

namespace HighPro {

using Microsoft::WRL::ComPtr;

// LutBaker:
//   离屏 256×16 RT, 复用 effect_chain.hlsl 的 PSMainBake.
//   输入 EffectStack → 输出 LUT 纹理 (GPU SRV) + CPU 字节镜像 (导出 PNG 用).
//
// 关键约束:
//   - RTV 用 R8G8B8A8_UNORM (非 _SRGB), shader 直接写 sRGB 字节
//   - 影子保护强制 LUT (0,0) = (0,0,0)
//   - 颜色图源 = 资源 ":/lut/default.png"
class LutBaker
{
public:
    static constexpr int kWidth  = 256;
    static constexpr int kHeight = 16;
    static constexpr size_t kBytes = (size_t)kWidth * kHeight * 4;

    bool init(QString* errorOut = nullptr);
    void release();
    bool isReady() const { return m_ready; }

    // 烘焙单层: stack → m_lutTex / m_lutBytes (覆盖). 返回是否成功.
    // identityHint: 当 stack.isIdentity() 时直接拷源颜色图, 跳过 PS.
    bool bake(const EffectStack& stack, QString* errorOut = nullptr);

    // GPU 资源 (帧端可直接绑作 t1 替代旧 add_lut PNG)
    ID3D11ShaderResourceView* lutSrv() const { return m_lutSrv.Get(); }
    ID3D11Texture2D*          lutTex() const { return m_lutTex.Get(); }

    // CPU 镜像 (导出 PNG 用; bake 后立即可读)
    const std::array<uint8_t, kBytes>& bytes() const { return m_lutBytes; }

private:
    bool buildPipeline(QString* errorOut);
    bool createRt(QString* errorOut);
    void uploadEffectCB(const EffectStack& s);
    void uploadCurveLut(const EffectStack& s);
    bool readbackToCpu(QString* errorOut);
    void enforceShadowLockOnCpu();

    // 颜色图 (256×16, 资源加载一次)
    D3D11Texture m_srcColorMap;

    // 烘焙 RT (UNORM)
    ComPtr<ID3D11Texture2D>          m_lutTex;
    ComPtr<ID3D11RenderTargetView>   m_lutRtv;
    ComPtr<ID3D11ShaderResourceView> m_lutSrv;

    // staging (CPU 读回)
    ComPtr<ID3D11Texture2D>          m_staging;

    // 共享: cb / 曲线 LUT / sampler / blend / raster / depth
    D3D11Shader m_shader;          // VS = effect_chain VSMain, PS = PSMainBake
    ComPtr<ID3D11Buffer>             m_cb;        // QuadCB (b0)
    ComPtr<ID3D11Buffer>             m_cbEffect;  // EffectCB (b1)
    ComPtr<ID3D11Texture2D>          m_curveTex;
    ComPtr<ID3D11ShaderResourceView> m_curveSrv;
    ComPtr<ID3D11SamplerState>       m_samplerLinear;
    ComPtr<ID3D11BlendState>         m_blendOpaque;
    ComPtr<ID3D11RasterizerState>    m_raster;
    ComPtr<ID3D11DepthStencilState>  m_depthOff;

    std::array<uint8_t, kBytes> m_lutBytes{};

    bool m_ready = false;
};

} // namespace HighPro
