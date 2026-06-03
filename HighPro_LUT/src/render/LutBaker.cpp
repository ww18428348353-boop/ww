#include "LutBaker.h"
#include "D3D11Context.h"
#include "core/CurveSolver.h"
#include "core/HaldClut.h"

#include <QFile>
#include <QDebug>
#include <cstring>
#include <cmath>

namespace HighPro {

namespace {

// 与 FrameRenderer 同布局 (effect_chain.hlsl 的 cb0/cb1)
struct QuadCB
{
    float uvRect[4];
    float posRect[4];
    float tint[4];
};
struct EffectCB
{
    int   enableMask[4];
    float hsl[4];
    float brtCtr[4];
    float mixR[4];
    float mixG[4];
    float mixB[4];
    int   mixFlag[4];
    float balShadow[4];
    float balMid[4];
    float balHigh[4];
    int   balFlag[4];
    float photoColor[4];
    int   photoFlag[4];
    float vibrance[4];
};
static_assert(sizeof(EffectCB) % 16 == 0, "EffectCB must be 16-aligned");

QByteArray loadShader(const char* qrc)
{
    QFile f(qrc);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return f.readAll();
}

} // namespace

bool LutBaker::init(QString* errorOut)
{
    if (m_ready) return true;
    auto* dev = D3D11Context::instance().device();
    if (!dev) { if (errorOut) *errorOut = "Device 未初始化"; return false; }

    // 1) 颜色图 (256×16 HALD-CLUT, 来自资源)
    if (!HaldClut::loadDefaultAsTexture(m_srcColorMap, errorOut)) return false;
    if (m_srcColorMap.width() != kWidth || m_srcColorMap.height() != kHeight) {
        if (errorOut) *errorOut = QString("颜色图尺寸异常 %1x%2")
                                  .arg(m_srcColorMap.width()).arg(m_srcColorMap.height());
        return false;
    }

    if (!buildPipeline(errorOut)) return false;
    if (!createRt(errorOut))      return false;

    m_ready = true;
    return true;
}

bool LutBaker::buildPipeline(QString* errorOut)
{
    auto* dev = D3D11Context::instance().device();

    // shader: VS=VSMain, PS=PSMainBake
    QByteArray src = loadShader(":/shaders/effect_chain.hlsl");
    if (src.isEmpty()) { if (errorOut) *errorOut = "effect_chain.hlsl 缺失"; return false; }
    if (!m_shader.compile(src, "VSMain", "PSMainBake", "lut_baker", errorOut)) return false;

    // cb0 (QuadCB), cb1 (EffectCB)
    {
        D3D11_BUFFER_DESC cbd{};
        cbd.ByteWidth      = (UINT)((sizeof(QuadCB) + 15) & ~15u);
        cbd.Usage          = D3D11_USAGE_DYNAMIC;
        cbd.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
        cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(dev->CreateBuffer(&cbd, nullptr, m_cb.GetAddressOf()))) {
            if (errorOut) *errorOut = "CreateBuffer cb"; return false;
        }
        cbd.ByteWidth = (UINT)((sizeof(EffectCB) + 15) & ~15u);
        if (FAILED(dev->CreateBuffer(&cbd, nullptr, m_cbEffect.GetAddressOf()))) {
            if (errorOut) *errorOut = "CreateBuffer cbEffect"; return false;
        }
    }

    // curve LUT (256x1)
    {
        D3D11_TEXTURE2D_DESC td{};
        td.Width = 256; td.Height = 1; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DYNAMIC;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(dev->CreateTexture2D(&td, nullptr, m_curveTex.GetAddressOf()))) {
            if (errorOut) *errorOut = "CreateTexture2D curve"; return false;
        }
        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        sd.Texture2D.MipLevels = 1;
        if (FAILED(dev->CreateShaderResourceView(m_curveTex.Get(), &sd, m_curveSrv.GetAddressOf()))) {
            if (errorOut) *errorOut = "CreateSRV curve"; return false;
        }
        // 初始为恒等
        CurveParams cp;
        std::array<uint8_t, 256*4> data;
        CurveSolver::buildLut(cp, data);
        auto* dc = D3D11Context::instance().context();
        D3D11_MAPPED_SUBRESOURCE map{};
        if (SUCCEEDED(dc->Map(m_curveTex.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
            memcpy(map.pData, data.data(), data.size());
            dc->Unmap(m_curveTex.Get(), 0);
        }
    }

    // sampler (LINEAR/CLAMP, 颜色图与曲线共用)
    {
        D3D11_SAMPLER_DESC sd{};
        sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT; // 颜色图采源像素需 POINT, 否则插值出错
        sd.MinLOD = 0; sd.MaxLOD = D3D11_FLOAT32_MAX;
        sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
        if (FAILED(dev->CreateSamplerState(&sd, m_samplerLinear.GetAddressOf()))) {
            if (errorOut) *errorOut = "CreateSamplerState"; return false;
        }
    }

    // blend opaque (整个矩形覆盖, 无 alpha)
    {
        D3D11_BLEND_DESC bd{};
        bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (FAILED(dev->CreateBlendState(&bd, m_blendOpaque.GetAddressOf()))) {
            if (errorOut) *errorOut = "CreateBlendState"; return false;
        }
    }
    // raster
    {
        D3D11_RASTERIZER_DESC rd{};
        rd.FillMode = D3D11_FILL_SOLID;
        rd.CullMode = D3D11_CULL_NONE;
        rd.DepthClipEnable = TRUE;
        if (FAILED(dev->CreateRasterizerState(&rd, m_raster.GetAddressOf()))) {
            if (errorOut) *errorOut = "CreateRasterizerState"; return false;
        }
    }
    // depth off
    {
        D3D11_DEPTH_STENCIL_DESC dsd{};
        if (FAILED(dev->CreateDepthStencilState(&dsd, m_depthOff.GetAddressOf()))) {
            if (errorOut) *errorOut = "CreateDepthStencilState"; return false;
        }
    }
    return true;
}

bool LutBaker::createRt(QString* errorOut)
{
    auto* dev = D3D11Context::instance().device();

    D3D11_TEXTURE2D_DESC td{};
    td.Width = kWidth; td.Height = kHeight;
    td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;          // 直存 sRGB 字节, shader 不再 srgbToLinear
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(dev->CreateTexture2D(&td, nullptr, m_lutTex.GetAddressOf()))) {
        if (errorOut) *errorOut = "CreateTexture2D lut"; return false;
    }

    D3D11_RENDER_TARGET_VIEW_DESC rd{};
    rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    if (FAILED(dev->CreateRenderTargetView(m_lutTex.Get(), &rd, m_lutRtv.GetAddressOf()))) {
        if (errorOut) *errorOut = "CreateRTV lut"; return false;
    }
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    sd.Texture2D.MipLevels = 1;
    if (FAILED(dev->CreateShaderResourceView(m_lutTex.Get(), &sd, m_lutSrv.GetAddressOf()))) {
        if (errorOut) *errorOut = "CreateSRV lut"; return false;
    }

    // staging (CPU 读回)
    D3D11_TEXTURE2D_DESC sg = td;
    sg.Usage = D3D11_USAGE_STAGING;
    sg.BindFlags = 0;
    sg.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(dev->CreateTexture2D(&sg, nullptr, m_staging.GetAddressOf()))) {
        if (errorOut) *errorOut = "CreateTexture2D staging"; return false;
    }
    return true;
}

void LutBaker::release()
{
    m_srcColorMap.release();
    m_lutTex.Reset();
    m_lutRtv.Reset();
    m_lutSrv.Reset();
    m_staging.Reset();
    m_shader.release();
    m_cb.Reset();
    m_cbEffect.Reset();
    m_curveTex.Reset();
    m_curveSrv.Reset();
    m_samplerLinear.Reset();
    m_blendOpaque.Reset();
    m_raster.Reset();
    m_depthOff.Reset();
    m_ready = false;
}

void LutBaker::uploadEffectCB(const EffectStack& s)
{
    EffectCB cb{};
    int em = 0;
    for (size_t i = 0; i < EffectStack::kCount; ++i) if (s.enabled[i]) em |= (1 << i);
    cb.enableMask[0] = em;
    cb.enableMask[1] = s.shadowProtectThreshold;

    cb.hsl[0] = s.hsl.hue / 360.0f;
    cb.hsl[1] = s.hsl.saturation / 100.0f;
    cb.hsl[2] = s.hsl.lightness  / 100.0f;

    cb.brtCtr[0] = s.brtCtr.brightness / 255.0f;
    cb.brtCtr[1] = s.brtCtr.contrast   / 100.0f;

    cb.mixR[0] = s.chMix.rr / 100.0f; cb.mixR[1] = s.chMix.rg / 100.0f;
    cb.mixR[2] = s.chMix.rb / 100.0f; cb.mixR[3] = s.chMix.r_const / 100.0f;
    cb.mixG[0] = s.chMix.gr / 100.0f; cb.mixG[1] = s.chMix.gg / 100.0f;
    cb.mixG[2] = s.chMix.gb / 100.0f; cb.mixG[3] = s.chMix.g_const / 100.0f;
    cb.mixB[0] = s.chMix.br / 100.0f; cb.mixB[1] = s.chMix.bg / 100.0f;
    cb.mixB[2] = s.chMix.bb / 100.0f; cb.mixB[3] = s.chMix.b_const / 100.0f;
    cb.mixFlag[0] = s.chMix.monochrome ? 1 : 0;

    cb.balShadow[0] = s.colorBal.sR / 100.0f;
    cb.balShadow[1] = s.colorBal.sG / 100.0f;
    cb.balShadow[2] = s.colorBal.sB / 100.0f;
    cb.balMid[0]    = s.colorBal.mR / 100.0f;
    cb.balMid[1]    = s.colorBal.mG / 100.0f;
    cb.balMid[2]    = s.colorBal.mB / 100.0f;
    cb.balHigh[0]   = s.colorBal.hR / 100.0f;
    cb.balHigh[1]   = s.colorBal.hG / 100.0f;
    cb.balHigh[2]   = s.colorBal.hB / 100.0f;
    cb.balFlag[0]   = s.colorBal.preserveLuma ? 1 : 0;

    cb.photoColor[0] = s.photoFilter.filterR / 255.0f;
    cb.photoColor[1] = s.photoFilter.filterG / 255.0f;
    cb.photoColor[2] = s.photoFilter.filterB / 255.0f;
    cb.photoColor[3] = s.photoFilter.density / 100.0f;
    cb.photoFlag[0]  = s.photoFilter.preserveLuma ? 1 : 0;

    cb.vibrance[0] = s.vibrance.vibrance   / 100.0f;
    cb.vibrance[1] = s.vibrance.saturation / 100.0f;

    auto* dc = D3D11Context::instance().context();
    D3D11_MAPPED_SUBRESOURCE map{};
    if (SUCCEEDED(dc->Map(m_cbEffect.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
        memcpy(map.pData, &cb, sizeof(cb));
        dc->Unmap(m_cbEffect.Get(), 0);
    }
}

void LutBaker::uploadCurveLut(const EffectStack& s)
{
    std::array<uint8_t, 256*4> data;
    CurveSolver::buildLut(s.curves, data);

    auto* dc = D3D11Context::instance().context();
    D3D11_MAPPED_SUBRESOURCE map{};
    if (SUCCEEDED(dc->Map(m_curveTex.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
        memcpy(map.pData, data.data(), data.size());
        dc->Unmap(m_curveTex.Get(), 0);
    }
}

bool LutBaker::readbackToCpu(QString* errorOut)
{
    auto* dc = D3D11Context::instance().context();
    dc->CopyResource(m_staging.Get(), m_lutTex.Get());

    D3D11_MAPPED_SUBRESOURCE map{};
    if (FAILED(dc->Map(m_staging.Get(), 0, D3D11_MAP_READ, 0, &map))) {
        if (errorOut) *errorOut = "Map staging 失败"; return false;
    }
    const uint8_t* src = (const uint8_t*)map.pData;
    for (int y = 0; y < kHeight; ++y) {
        memcpy(m_lutBytes.data() + (size_t)y * kWidth * 4,
               src + y * map.RowPitch,
               (size_t)kWidth * 4);
    }
    dc->Unmap(m_staging.Get(), 0);
    return true;
}

void LutBaker::enforceShadowLockOnCpu()
{
    // HALD-CLUT (0,0) 像素应该是 (0,0,0). 防止 PS 端的浮点漂移.
    HaldClut::enforceShadowLock(m_lutBytes.data());
}

bool LutBaker::bake(const EffectStack& stack, QString* errorOut)
{
    if (!m_ready) { if (errorOut) *errorOut = "LutBaker 未 init"; return false; }

    auto* dc = D3D11Context::instance().context();

    // Identity 优化: 直接读 staging 拷源
    if (stack.isIdentity()) {
        // 走 CopyResource: 先把源颜色图拷到 lutTex (DEFAULT), 再走 staging
        // 简单做法: 仍然走管线 (em=0 时 PS 直 return src), 反正 1×16×256 只需 ~0.05ms
    }

    // 上传 cb
    uploadEffectCB(stack);
    if (stack.enabled[EffectStack::ECurves]) uploadCurveLut(stack);

    // RTV/Viewport
    ID3D11RenderTargetView* rtvs[] = { m_lutRtv.Get() };
    dc->OMSetRenderTargets(1, rtvs, nullptr);

    D3D11_VIEWPORT vp{};
    vp.Width = (FLOAT)kWidth; vp.Height = (FLOAT)kHeight; vp.MaxDepth = 1;
    dc->RSSetViewports(1, &vp);

    dc->RSSetState(m_raster.Get());
    dc->OMSetDepthStencilState(m_depthOff.Get(), 0);
    const float blendFactor[4] = { 0,0,0,0 };
    dc->OMSetBlendState(m_blendOpaque.Get(), blendFactor, 0xffffffff);
    dc->IASetInputLayout(nullptr);
    dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // QuadCB: 全屏覆盖 (NDC -1..1).
    // 注意 D3D NDC y 与纹理 v 的关系: NDC y=-1 落在 RTV 底行 (像素 y=H-1),
    // 而源颜色图 SRV 是 top-down (v=0 在顶). 因此 v 必须翻转, 否则烘出来的
    // LUT 上下颠倒, 反推时绿色通道全错 (recolor.hlsl 用 v=(g+0.5)/16 假设顶部 g 小).
    QuadCB qb{};
    qb.uvRect[0] = 0.0f; qb.uvRect[1] = 1.0f;
    qb.uvRect[2] = 1.0f; qb.uvRect[3] = 0.0f;
    qb.posRect[0] = -1.0f; qb.posRect[1] = -1.0f;
    qb.posRect[2] =  1.0f; qb.posRect[3] =  1.0f;
    qb.tint[0] = qb.tint[1] = qb.tint[2] = qb.tint[3] = 1.0f;

    D3D11_MAPPED_SUBRESOURCE map{};
    if (SUCCEEDED(dc->Map(m_cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
        memcpy(map.pData, &qb, sizeof(qb));
        dc->Unmap(m_cb.Get(), 0);
    }

    dc->VSSetShader(m_shader.vs(), nullptr, 0);
    dc->PSSetShader(m_shader.ps(), nullptr, 0);

    ID3D11Buffer* cbs0[] = { m_cb.Get() };
    dc->VSSetConstantBuffers(0, 1, cbs0);
    dc->PSSetConstantBuffers(0, 1, cbs0);
    ID3D11Buffer* cbs1[] = { m_cbEffect.Get() };
    dc->PSSetConstantBuffers(1, 1, cbs1);

    ID3D11SamplerState* samps[] = { m_samplerLinear.Get(), m_samplerLinear.Get() };
    dc->PSSetSamplers(0, 2, samps);

    ID3D11ShaderResourceView* srvs[] = { m_srcColorMap.srv(), m_curveSrv.Get() };
    dc->PSSetShaderResources(0, 2, srvs);

    dc->Draw(6, 0);

    // 解绑 SRV (避免 RTV/SRV hazard)
    ID3D11ShaderResourceView* nullSRVs[] = { nullptr, nullptr };
    dc->PSSetShaderResources(0, 2, nullSRVs);
    ID3D11RenderTargetView* nullRtvs[] = { nullptr };
    dc->OMSetRenderTargets(1, nullRtvs, nullptr);

    if (!readbackToCpu(errorOut)) return false;
    enforceShadowLockOnCpu();
    return true;
}

} // namespace HighPro
