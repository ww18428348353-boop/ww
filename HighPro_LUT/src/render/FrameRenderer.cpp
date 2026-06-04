#include "FrameRenderer.h"
#include "D3D11Context.h"
#include "core/CurveSolver.h"

#include <QFile>
#include <QDebug>
#include <QHash>
#include <QImage>
#include <QPainter>
#include <QFont>
#include <QFontMetrics>
#include <cmath>
#include <cstring>

namespace HighPro {

namespace {

struct QuadCB
{
    float uvRect[4];
    float posRect[4];
    float tint[4];
};

struct EffectCB
{
    int   enableMask[4];      // x=mask, y=shadowThreshold(0..255), z/w 备用
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

quint64 hashEffect(const EffectStack& s)
{
    QByteArray buf;
    buf.reserve(256);
    auto append = [&](const void* p, int n){ buf.append((const char*)p, n); };
    for (size_t i = 0; i < EffectStack::kCount; ++i) {
        bool e = s.enabled[i];
        append(&e, 1);
    }
    append(&s.hsl, sizeof(s.hsl));
    append(&s.brtCtr, sizeof(s.brtCtr));
    append(&s.chMix, sizeof(s.chMix));
    append(&s.colorBal, sizeof(s.colorBal));
    append(&s.photoFilter, sizeof(s.photoFilter));
    append(&s.vibrance, sizeof(s.vibrance));
    append(&s.shadowProtectThreshold, sizeof(s.shadowProtectThreshold));
    return qHash(buf);
}

quint64 hashCurve(const CurveParams& c)
{
    QByteArray buf;
    auto append = [&](const CurveParams::Pts& pts){
        for (auto& p : pts) {
            buf.append((const char*)&p.first,  sizeof(int));
            buf.append((const char*)&p.second, sizeof(int));
        }
        int sentinel = -1;
        buf.append((const char*)&sentinel, sizeof(int));
    };
    append(c.master); append(c.r); append(c.g); append(c.b);
    return qHash(buf);
}

} // namespace

bool FrameRenderer::init(QString* errorOut)
{
    if (m_ready) return true;
    if (!buildPipeline(errorOut)) return false;
    m_ready = true;
    return true;
}

bool FrameRenderer::buildPipeline(QString* errorOut)
{
    auto* dev = D3D11Context::instance().device();
    if (!dev) { if (errorOut) *errorOut = "Device 未初始化"; return false; }

    // 1) shaders
    QByteArray srcQuad = loadShader(":/shaders/fullscreen_quad.hlsl");
    QByteArray srcRec  = loadShader(":/shaders/recolor.hlsl");
    QByteArray srcEff  = loadShader(":/shaders/effect_chain.hlsl");
    if (srcQuad.isEmpty() || srcRec.isEmpty() || srcEff.isEmpty()) {
        if (errorOut) *errorOut = "shader 资源缺失";
        return false;
    }
    if (!m_shaderPass.compile(srcQuad, "VSMain", "PSMain", "fullscreen_quad", errorOut)) return false;
    if (!m_shaderRecolor.compile(srcRec, "VSMain", "PSMain", "recolor", errorOut))      return false;
    if (!m_shaderEffect.compile (srcEff, "VSMain", "PSMain", "effect_chain", errorOut)) return false;
    if (!m_shaderSolid.compile  (srcQuad, "VSMain", "PSMainSolid", "solid", errorOut))  return false;
    if (!m_shaderTriUp.compile  (srcQuad, "VSMain", "PSMainTriangleUp", "tri_up", errorOut)) return false;

    // 2) cbuffers
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

    // 3) curve LUT 256x1 (R8G8B8A8)
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
        // 初始化为恒等
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

    // 4) samplers
    {
        D3D11_SAMPLER_DESC sd{};
        sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MinLOD = 0; sd.MaxLOD = D3D11_FLOAT32_MAX;
        sd.ComparisonFunc = D3D11_COMPARISON_NEVER;

        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        if (FAILED(dev->CreateSamplerState(&sd, m_samplerLinear.GetAddressOf()))) {
            if (errorOut) *errorOut = "CreateSamplerState linear"; return false;
        }
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        if (FAILED(dev->CreateSamplerState(&sd, m_samplerPoint.GetAddressOf()))) {
            if (errorOut) *errorOut = "CreateSamplerState point"; return false;
        }
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        if (FAILED(dev->CreateSamplerState(&sd, m_samplerLutLinear.GetAddressOf()))) {
            if (errorOut) *errorOut = "CreateSamplerState lut"; return false;
        }
    }

    // 5) blend (straight alpha over — 与 AE/游戏引擎一致)
    //   纹理 = Straight RGBA (rgb 与 a 独立; 半透明像素 rgb 仍可以是满色).
    //   PS 输出也保持 Straight: return float4(rgb, a).
    //   公式: out.rgb = src.rgb * src.a + dst.rgb * (1 - src.a)
    //         out.a   = src.a         + dst.a   * (1 - src.a)
    {
        D3D11_BLEND_DESC bd{};
        bd.RenderTarget[0].BlendEnable = TRUE;
        bd.RenderTarget[0].SrcBlend  = D3D11_BLEND_SRC_ALPHA;       // Straight
        bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        bd.RenderTarget[0].BlendOp   = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].SrcBlendAlpha  = D3D11_BLEND_ONE;
        bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        bd.RenderTarget[0].BlendOpAlpha   = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (FAILED(dev->CreateBlendState(&bd, m_blendOver.GetAddressOf()))) {
            if (errorOut) *errorOut = "CreateBlendState"; return false;
        }
    }
    // 6) rasterizer
    {
        D3D11_RASTERIZER_DESC rd{};
        rd.FillMode = D3D11_FILL_SOLID;
        rd.CullMode = D3D11_CULL_NONE;
        rd.DepthClipEnable = TRUE;
        if (FAILED(dev->CreateRasterizerState(&rd, m_rasterState.GetAddressOf()))) {
            if (errorOut) *errorOut = "CreateRasterizerState"; return false;
        }
    }
    // 7) depth off
    {
        D3D11_DEPTH_STENCIL_DESC dsd{};
        if (FAILED(dev->CreateDepthStencilState(&dsd, m_depthOff.GetAddressOf()))) {
            if (errorOut) *errorOut = "CreateDepthStencilState"; return false;
        }
    }
    return true;
}

void FrameRenderer::release()
{
    m_shaderPass.release();
    m_shaderRecolor.release();
    m_shaderEffect.release();
    m_shaderSolid.release();
    m_shaderTriUp.release();
    m_cb.Reset();
    m_cbEffect.Reset();
    m_curveTex.Reset();
    m_curveSrv.Reset();
    m_samplerLinear.Reset();
    m_samplerPoint.Reset();
    m_samplerLutLinear.Reset();
    m_blendOver.Reset();
    m_rasterState.Reset();
    m_depthOff.Reset();
    m_labelCache.clear();
    m_ready = false;
}

void FrameRenderer::uploadEffectCB(const EffectStack& s)
{
    quint64 h = hashEffect(s);
    if (h == m_lastEffectHash) return;
    m_lastEffectHash = h;

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

void FrameRenderer::uploadCurveLut(const EffectStack& s)
{
    quint64 h = hashCurve(s.curves);
    if (h == m_lastCurveHash) return;
    m_lastCurveHash = h;

    std::array<uint8_t, 256*4> data;
    CurveSolver::buildLut(s.curves, data);

    auto* dc = D3D11Context::instance().context();
    D3D11_MAPPED_SUBRESOURCE map{};
    if (SUCCEEDED(dc->Map(m_curveTex.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
        memcpy(map.pData, data.data(), data.size());
        dc->Unmap(m_curveTex.Get(), 0);
    }
}

void FrameRenderer::render(ID3D11RenderTargetView* rtv, int rtvWidth, int rtvHeight,
                           const QColor& bgColor,
                           const std::vector<DrawItem>& items)
{
    Cell c;
    c.x = 0; c.y = 0; c.w = rtvWidth; c.h = rtvHeight;
    c.items = items;
    std::vector<Cell> one;
    one.push_back(std::move(c));
    renderCells(rtv, rtvWidth, rtvHeight, bgColor, one);
}

void FrameRenderer::renderCells(ID3D11RenderTargetView* rtv, int rtvWidth, int rtvHeight,
                                const QColor& bgColor,
                                const std::vector<Cell>& cells,
                                const std::shared_ptr<D3D11Texture>& bgImage)
{
    if (!m_ready || !rtv || rtvWidth <= 0 || rtvHeight <= 0) return;
    auto* dc = D3D11Context::instance().context();
    if (!dc) return;

    // RTV 是 _UNORM (非 _SRGB) → 清屏值就是 sRGB 字节, 不做 linear 转换.
    // 与 PS 直接输出 sRGB 字节配套 → Gamma 空间 alpha 合成 (= AE 默认).
    const float bg[4] = {
        (float)bgColor.redF(),
        (float)bgColor.greenF(),
        (float)bgColor.blueF(),
        1.0f,
    };
    dc->ClearRenderTargetView(rtv, bg);

    // 背景图: 在 clear 之后、cells 之前绘制 (保持原始比例, 不压缩)
    if (bgImage && bgImage->isValid()) {
        renderBgImage(rtv, rtvWidth, rtvHeight, bgImage);
    }

    ID3D11RenderTargetView* rtvs[] = { rtv };
    dc->OMSetRenderTargets(1, rtvs, nullptr);

    dc->IASetInputLayout(nullptr);
    dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    dc->RSSetState(m_rasterState.Get());
    dc->OMSetDepthStencilState(m_depthOff.Get(), 0);
    const float blendFactor[4] = { 0,0,0,0 };
    dc->OMSetBlendState(m_blendOver.Get(), blendFactor, 0xffffffff);

    ID3D11Buffer* cbs0[] = { m_cb.Get() };
    dc->VSSetConstantBuffers(0, 1, cbs0);
    dc->PSSetConstantBuffers(0, 1, cbs0);

    for (const auto& cell : cells) {
        if (cell.w <= 0 || cell.h <= 0) continue;

        D3D11_VIEWPORT vp{};
        vp.TopLeftX = (FLOAT)cell.x;
        vp.TopLeftY = (FLOAT)cell.y;
        vp.Width    = (FLOAT)cell.w;
        vp.Height   = (FLOAT)cell.h;
        vp.MaxDepth = 1;
        dc->RSSetViewports(1, &vp);

        for (const auto& it : cell.items) {
            if (!it.visible || !it.tex || !it.tex->isValid()) continue;
            const int tw = it.tex->width();
            const int th = it.tex->height();
            if (tw <= 0 || th <= 0) continue;

            const float vpW = (float)cell.w, vpH = (float)cell.h;
            // 等比 fit cell. zoom>1 时允许放大 (LINEAR sampler 双线性插值, 视觉柔和).
            float scale = qMin(vpW / tw, vpH / th);
            int wpx = (int)std::floor(tw * scale + 0.5f); if (wpx <= 0) wpx = 1;
            int hpx = (int)std::floor(th * scale + 0.5f); if (hpx <= 0) hpx = 1;
            int xpx = (cell.w - wpx) / 2;
            int ypx = (cell.h - hpx) / 2;

            QuadCB qb{};
            qb.uvRect[0] = 0; qb.uvRect[1] = 1; qb.uvRect[2] = 1; qb.uvRect[3] = 0;
            qb.posRect[0] = (float)xpx          / vpW * 2 - 1;
            qb.posRect[1] = 1 - (float)(ypx + hpx) / vpH * 2;
            qb.posRect[2] = (float)(xpx + wpx)  / vpW * 2 - 1;
            qb.posRect[3] = 1 - (float)ypx          / vpH * 2;
            qb.tint[0] = qb.tint[1] = qb.tint[2] = qb.tint[3] = 1.0f;

            D3D11_MAPPED_SUBRESOURCE map{};
            if (SUCCEEDED(dc->Map(m_cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
                memcpy(map.pData, &qb, sizeof(qb));
                dc->Unmap(m_cb.Get(), 0);
            }

            const bool oneToOne = (wpx == tw && hpx == th);
            ID3D11SamplerState* frameSampler =
                oneToOne ? m_samplerPoint.Get() : m_samplerLinear.Get();

            const bool useEffect = (it.effects && !it.effects->isIdentity());
            const bool useLut    = (!useEffect && it.useLut && it.lut && it.lut->isValid());

            if (useEffect) {
                uploadEffectCB(*it.effects);
                if (it.effects->enabled[EffectStack::ECurves]) uploadCurveLut(*it.effects);

                dc->VSSetShader(m_shaderEffect.vs(), nullptr, 0);
                dc->PSSetShader(m_shaderEffect.ps(), nullptr, 0);

                ID3D11Buffer* cbs1[] = { m_cbEffect.Get() };
                dc->PSSetConstantBuffers(1, 1, cbs1);

                ID3D11SamplerState* samps[] = { frameSampler, m_samplerLutLinear.Get() };
                dc->PSSetSamplers(0, 2, samps);

                ID3D11ShaderResourceView* srvs[] = { it.tex->srv(), m_curveSrv.Get() };
                dc->PSSetShaderResources(0, 2, srvs);
            } else if (useLut) {
                dc->VSSetShader(m_shaderRecolor.vs(), nullptr, 0);
                dc->PSSetShader(m_shaderRecolor.ps(), nullptr, 0);

                ID3D11SamplerState* samps[] = { frameSampler, m_samplerLutLinear.Get() };
                dc->PSSetSamplers(0, 2, samps);

                ID3D11ShaderResourceView* srvs[] = { it.tex->srv(), it.lut->srv() };
                dc->PSSetShaderResources(0, 2, srvs);
            } else {
                dc->VSSetShader(m_shaderPass.vs(), nullptr, 0);
                dc->PSSetShader(m_shaderPass.ps(), nullptr, 0);

                ID3D11SamplerState* samps[] = { frameSampler };
                dc->PSSetSamplers(0, 1, samps);

                ID3D11ShaderResourceView* srvs[] = { it.tex->srv() };
                dc->PSSetShaderResources(0, 1, srvs);
            }
            dc->Draw(6, 0);
        }

        // M5: 选中标识 = ▲ 蓝三角 (尖朝上, 指向角色).
        // PreviewPanel 已把 "K 点 + 50×scale" 算进 (kpointX, kpointY) → 直接当顶点用.
        // 三角"位置"已跟随缩放, "大小"用 cell.triPixelW/H 固定像素.
        if (cell.highlighted && cell.w > 4 && cell.h > 4) {
            const float triBaseW = (float)qMax(2, cell.triPixelW);
            const float triH     = (float)qMax(2, cell.triPixelH);

            const float topPx_x  = (float)cell.kpointX;
            const float topPx_y  = (float)cell.kpointY;
            const float boxL = topPx_x - triBaseW * 0.5f;
            const float boxR = topPx_x + triBaseW * 0.5f;
            const float boxT = topPx_y;
            const float boxB = topPx_y + triH;

            // 转 cell 内 NDC: x = px / cellW * 2 - 1, y = 1 - py / cellH * 2
            const float vpW = (float)cell.w, vpH = (float)cell.h;
            QuadCB qb{};
            qb.uvRect[0] = 0; qb.uvRect[1] = 0; qb.uvRect[2] = 1; qb.uvRect[3] = 1;
            qb.posRect[0] = boxL / vpW * 2.0f - 1.0f;
            qb.posRect[1] = 1.0f - boxB / vpH * 2.0f;
            qb.posRect[2] = boxR / vpW * 2.0f - 1.0f;
            qb.posRect[3] = 1.0f - boxT / vpH * 2.0f;

            const QColor& hc = cell.highlightColor;
            const float a = qBound(0.0f, cell.highlightAlpha, 1.0f);
            qb.tint[0] = (float)hc.redF();
            qb.tint[1] = (float)hc.greenF();
            qb.tint[2] = (float)hc.blueF();
            qb.tint[3] = a;

            D3D11_MAPPED_SUBRESOURCE m{};
            if (SUCCEEDED(dc->Map(m_cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
                memcpy(m.pData, &qb, sizeof(qb));
                dc->Unmap(m_cb.Get(), 0);
            }
            dc->VSSetShader(m_shaderTriUp.vs(), nullptr, 0);
            dc->PSSetShader(m_shaderTriUp.ps(), nullptr, 0);
            dc->Draw(6, 0);
        }

        // 文字 label (cell 内指定锚点; PreviewPanel 算好 labelY 让 label 紧贴角色头顶).
        if (!cell.label.isEmpty() && cell.w > 4 && cell.h > 4) {
            const auto* lt = getOrCreateLabel(cell.label, cell.labelColor);
            if (lt && lt->srv) {
                const float marginTop = 8.0f;
                const float lw = (float)lt->w;
                const float lh = (float)lt->h;
                // anchorY = label 顶边的 y (cell viewport 内). PreviewPanel 给 0 → 用默认 8px.
                const float anchorY = (cell.labelY > 0) ? (float)cell.labelY : marginTop;
                const float boxL = ((float)cell.w - lw) * 0.5f;
                const float boxR = boxL + lw;
                const float boxT = anchorY;
                const float boxB = anchorY + lh;

                const float vpW = (float)cell.w, vpH = (float)cell.h;
                QuadCB qb{};
                // 与主帧一致 uvRect = (0,1,1,0):
                //   - U: x=0 在 ix=0 (NDC 左), x=1 在 ix=1 (NDC 右) → 字水平正向
                //   - V: y=1 在 iy=0 (NDC 下), y=0 在 iy=1 (NDC 上) → 字垂直正向
                //     (QImage 内存 y=0 是顶, 跟"屏幕上方 NDC y 大" 配, 所以 V 翻)
                qb.uvRect[0] = 0; qb.uvRect[1] = 1; qb.uvRect[2] = 1; qb.uvRect[3] = 0;
                qb.posRect[0] = boxL / vpW * 2.0f - 1.0f;
                qb.posRect[1] = 1.0f - boxB / vpH * 2.0f;
                qb.posRect[2] = boxR / vpW * 2.0f - 1.0f;
                qb.posRect[3] = 1.0f - boxT / vpH * 2.0f;
                qb.tint[0] = qb.tint[1] = qb.tint[2] = qb.tint[3] = 1.0f;

                D3D11_MAPPED_SUBRESOURCE m{};
                if (SUCCEEDED(dc->Map(m_cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
                    memcpy(m.pData, &qb, sizeof(qb));
                    dc->Unmap(m_cb.Get(), 0);
                }
                dc->VSSetShader(m_shaderPass.vs(), nullptr, 0);
                dc->PSSetShader(m_shaderPass.ps(), nullptr, 0);
                ID3D11SamplerState* samps[] = { m_samplerLinear.Get() };
                dc->PSSetSamplers(0, 1, samps);
                ID3D11ShaderResourceView* srvs[] = { lt->srv.Get() };
                dc->PSSetShaderResources(0, 1, srvs);
                dc->Draw(6, 0);
            }
        }
    }

    ID3D11ShaderResourceView* nullSRVs[] = { nullptr, nullptr };
    dc->PSSetShaderResources(0, 2, nullSRVs);
}

// 文字 → 纹理 (lazy cache). 字体: 系统等宽体, 高度 24px, 抗锯齿.
// 输出 R8G8B8A8_UNORM 透明背景纯色文字, 给 fullscreen_quad PSMain 当普通 sRGB 帧画.
const FrameRenderer::LabelTex* FrameRenderer::getOrCreateLabel(const QString& text, const QColor& color)
{
    if (text.isEmpty()) return nullptr;
    const QString key = text + "|" + QString::number(color.rgba(), 16);
    auto it = m_labelCache.find(key);
    if (it != m_labelCache.end()) return &it.value();

    // 1) CPU 端 QImage 渲染文字
    QFont font;
    font.setPointSize(11);
    font.setBold(true);
    QFontMetrics fm(font);
    const int padX = 8, padY = 4;
    const int textW = fm.horizontalAdvance(text);
    const int textH = fm.height();
    int imgW = textW + padX * 2;
    int imgH = textH + padY * 2;
    // 对齐 4 字节避免 GPU 上传 pitch 问题
    if (imgW % 4) imgW += 4 - (imgW % 4);

    QImage img(imgW, imgH, QImage::Format_RGBA8888);
    img.fill(Qt::transparent);
    {
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::TextAntialiasing, true);
        // 半透明黑色背景圆角条 (易读性)
        p.setBrush(QColor(0, 0, 0, 140));
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(QRectF(0.5, 0.5, imgW - 1, imgH - 1), 4, 4);
        // 文字
        p.setPen(color);
        p.setFont(font);
        p.drawText(QRect(0, 0, imgW, imgH), Qt::AlignCenter, text);
    }

    // 2) 创 D3D11 Texture2D + SRV
    LabelTex lt;
    lt.w = imgW; lt.h = imgH;
    auto* dev = D3D11Context::instance().device();
    if (!dev) return nullptr;

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = imgW; desc.Height = imgH;
    desc.MipLevels = 1; desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = img.constBits();
    init.SysMemPitch = (UINT)img.bytesPerLine();
    if (FAILED(dev->CreateTexture2D(&desc, &init, lt.tex.GetAddressOf()))) return nullptr;

    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    sd.Texture2D.MipLevels = 1;
    if (FAILED(dev->CreateShaderResourceView(lt.tex.Get(), &sd, lt.srv.GetAddressOf()))) return nullptr;

    // 缓存上限 (避免长时间累积): 100 个不同 label 后清掉
    if (m_labelCache.size() > 100) m_labelCache.clear();
    auto inserted = m_labelCache.insert(key, std::move(lt));
    return &inserted.value();
}

void FrameRenderer::renderBgImage(ID3D11RenderTargetView* rtv, int rtvWidth, int rtvHeight,
                                  const std::shared_ptr<D3D11Texture>& tex)
{
    if (!m_ready || !rtv || !tex || !tex->isValid()) return;
    if (rtvWidth <= 0 || rtvHeight <= 0) return;
    auto* dc = D3D11Context::instance().context();
    if (!dc) return;

    const int tw = tex->width();
    const int th = tex->height();
    if (tw <= 0 || th <= 0) return;

    // "Cover" 模式: 保持图片宽高比, 缩放使整个画布被覆盖 (超出裁剪).
    // 即取 max(scaleX, scaleY) 使图片至少覆盖整个 RTV.
    const float vpW = (float)rtvWidth, vpH = (float)rtvHeight;
    const float scaleX = vpW / (float)tw;
    const float scaleY = vpH / (float)th;
    const float scale = qMax(scaleX, scaleY);

    // 缩放后图片像素大小
    const float scaledW = tw * scale;
    const float scaledH = th * scale;

    // 超出画布的部分通过 UV 裁剪 (居中)
    // UV 范围: 只显示画布能容纳的部分
    float u0 = (scaledW - vpW) / (2.0f * scaledW);
    float v0 = (scaledH - vpH) / (2.0f * scaledH);
    float u1 = 1.0f - u0;
    float v1 = 1.0f - v0;

    // 画满整个 viewport
    int drawX = 0, drawY = 0;
    int drawW = rtvWidth, drawH = rtvHeight;

    D3D11_VIEWPORT vp{};
    vp.TopLeftX = 0; vp.TopLeftY = 0;
    vp.Width = vpW; vp.Height = vpH;
    vp.MaxDepth = 1;
    dc->RSSetViewports(1, &vp);

    ID3D11RenderTargetView* rtvs[] = { rtv };
    dc->OMSetRenderTargets(1, rtvs, nullptr);
    dc->IASetInputLayout(nullptr);
    dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    dc->RSSetState(m_rasterState.Get());
    dc->OMSetDepthStencilState(m_depthOff.Get(), 0);
    const float blendFactor[4] = { 0,0,0,0 };
    dc->OMSetBlendState(m_blendOver.Get(), blendFactor, 0xffffffff);

    ID3D11Buffer* cbs0[] = { m_cb.Get() };
    dc->VSSetConstantBuffers(0, 1, cbs0);
    dc->PSSetConstantBuffers(0, 1, cbs0);

    QuadCB qb{};
    qb.uvRect[0] = u0; qb.uvRect[1] = v1; qb.uvRect[2] = u1; qb.uvRect[3] = v0;
    qb.posRect[0] = (float)drawX / vpW * 2 - 1;
    qb.posRect[1] = 1 - (float)(drawY + drawH) / vpH * 2;
    qb.posRect[2] = (float)(drawX + drawW) / vpW * 2 - 1;
    qb.posRect[3] = 1 - (float)drawY / vpH * 2;
    qb.tint[0] = qb.tint[1] = qb.tint[2] = qb.tint[3] = 1.0f;

    D3D11_MAPPED_SUBRESOURCE map{};
    if (SUCCEEDED(dc->Map(m_cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
        memcpy(map.pData, &qb, sizeof(qb));
        dc->Unmap(m_cb.Get(), 0);
    }

    dc->VSSetShader(m_shaderPass.vs(), nullptr, 0);
    dc->PSSetShader(m_shaderPass.ps(), nullptr, 0);

    ID3D11SamplerState* samps[] = { m_samplerLinear.Get() };
    dc->PSSetSamplers(0, 1, samps);

    ID3D11ShaderResourceView* srvs[] = { tex->srv() };
    dc->PSSetShaderResources(0, 1, srvs);

    dc->Draw(6, 0);
}

} // namespace HighPro
