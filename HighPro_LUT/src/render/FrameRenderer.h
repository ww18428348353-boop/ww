#pragma once

#include "D3D11Shader.h"
#include "D3D11Texture.h"
#include "core/ColorEffect.h"

#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <QColor>
#include <QHash>
#include <QString>
#include <memory>
#include <vector>

namespace HighPro {

using Microsoft::WRL::ComPtr;

// FrameRenderer:
//   每层一个 DrawItem. 三种渲染路径:
//     a) effects 非空 + identity? 走 effect_chain PS  (主路径, AE 对齐)
//     b) lut != null && useLut          走 recolor PS (从 add_lut PNG 反推, M3 兼容)
//     c) 其他                            走 passthrough (本体)
class FrameRenderer
{
public:
    bool init(QString* errorOut = nullptr);
    void release();

    struct DrawItem
    {
        std::shared_ptr<D3D11Texture> tex;        // 源帧
        std::shared_ptr<D3D11Texture> lut;        // 老 add_lut PNG (b 路径用)
        bool                          useLut = false;

        // 主路径: 7 效果链
        const EffectStack*            effects = nullptr;   // 不拥有, 由 Project 持有

        bool visible = true;
    };

    // 单 cell 渲染 (向后兼容): 整 RTV 清屏 + items 画在整 RTV.
    void render(ID3D11RenderTargetView* rtv, int rtvWidth, int rtvHeight,
                const QColor& bgColor,
                const std::vector<DrawItem>& items);

    // M5: 多 cell 渲染, 用于多方案画布. 同一 RTV 内画 N 个 cell, 每 cell 一份 items.
    //   cellRect: D3D viewport 矩形 (像素, top-down, RTV 坐标)
    //   每 cell 独立画 items (frame×layers 同步, effects 不同).
    struct Cell
    {
        int x, y, w, h;                              // viewport 矩形 (像素)
        std::vector<DrawItem> items;
        bool   highlighted = false;                  // M5: 选中态 → 在 K 点下方画 ▲
        // K 点 (相对 cell viewport 左上角的像素).
        int    kpointX = 0;
        int    kpointY = 0;
        // 三角"位置"已由 kpoint 跟随缩放; "大小"用固定像素 (不随 zoom 变).
        int    triPixelW = 24;
        int    triPixelH = 22;
        QColor highlightColor{ 88, 166, 255 };
        float  highlightAlpha = 1.0f;                // 0..1

        // 画布上 cell 顶部显示的文字 (例如 "02" / "07-锁" / "本体"). 空 = 不画.
        QString label;
        QColor  labelColor{ 220, 220, 220 };
        // label 在 cell viewport 内的 y 像素 (label 顶边). 不设 (=0) → 用默认 8px (cell 顶).
        int     labelY = 0;
    };
    void renderCells(ID3D11RenderTargetView* rtv, int rtvWidth, int rtvHeight,
                     const QColor& bgColor,
                     const std::vector<Cell>& cells);

private:
    bool buildPipeline(QString* errorOut);
    void uploadEffectCB(const EffectStack& s);
    void uploadCurveLut(const EffectStack& s);

    // 文字 overlay: label 字符串 → 文字纹理 (lazy). 用于 cell 顶部显示方案 ID.
    struct LabelTex {
        ComPtr<ID3D11Texture2D>          tex;
        ComPtr<ID3D11ShaderResourceView> srv;
        int w = 0, h = 0;
    };
    const LabelTex* getOrCreateLabel(const QString& text, const QColor& color);
    QHash<QString, LabelTex> m_labelCache;     // key = "<text>|<rgba>"

    D3D11Shader  m_shaderPass;
    D3D11Shader  m_shaderRecolor;
    D3D11Shader  m_shaderEffect;
    D3D11Shader  m_shaderSolid;       // M5: 选中边框纯色填充 (备用)
    D3D11Shader  m_shaderTriUp;       // M5: 选中向上三角 (K 点下方提示)

    ComPtr<ID3D11Buffer>            m_cb;            // QuadCB (b0)
    ComPtr<ID3D11Buffer>            m_cbEffect;      // EffectCB (b1)
    ComPtr<ID3D11Texture2D>         m_curveTex;      // 256x1 curve LUT
    ComPtr<ID3D11ShaderResourceView> m_curveSrv;

    ComPtr<ID3D11SamplerState>      m_samplerLinear;
    ComPtr<ID3D11SamplerState>      m_samplerPoint;
    ComPtr<ID3D11SamplerState>      m_samplerLutLinear;
    ComPtr<ID3D11BlendState>        m_blendOver;
    ComPtr<ID3D11RasterizerState>   m_rasterState;
    ComPtr<ID3D11DepthStencilState> m_depthOff;

    bool m_ready = false;

    // 缓存上次上传的 stack 哈希, 减少 CB 上传
    quint64 m_lastEffectHash = 0;
    quint64 m_lastCurveHash  = 0;
};

} // namespace HighPro
