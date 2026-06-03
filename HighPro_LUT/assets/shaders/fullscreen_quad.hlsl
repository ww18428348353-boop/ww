// fullscreen_quad.hlsl
// VS: 用 SV_VertexID 三角形覆盖整个 NDC, 无需 IA buffer.
// PS: 简单采样 t0 纹理 (alpha 透明), 用 cbuffer 决定纹理在 NDC 内的矩形.
//
// cb0 (slot b0) 由 FrameRenderer 填:
//   uvRect    : float4 = (uMin, vMin, uMax, vMax)        纹理 UV (一般 0..1)
//   posRect   : float4 = (xMin, yMin, xMax, yMax)        NDC 坐标 -1..1
//   tint      : float4 = (r,g,b,a)  乘性 tint, 默认 (1,1,1,1)
//
// SamplerState s0 在 C++ 端创建为 LINEAR/CLAMP.

cbuffer QuadCB : register(b0)
{
    float4 uvRect;
    float4 posRect;
    float4 tint;
};

Texture2D    gTex      : register(t0);
SamplerState gLinearClamp : register(s0);

struct VSOut
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

static const uint2 IDX[6] = {
    uint2(0, 0), uint2(1, 0), uint2(0, 1),
    uint2(1, 0), uint2(1, 1), uint2(0, 1),
};

VSOut VSMain(uint vid : SV_VertexID)
{
    VSOut o;
    uint ix = IDX[vid].x;
    uint iy = IDX[vid].y;

    float x = (ix == 0) ? posRect.x : posRect.z;
    float y = (iy == 0) ? posRect.y : posRect.w;
    float u = (ix == 0) ? uvRect.x  : uvRect.z;
    float v = (iy == 0) ? uvRect.y  : uvRect.w;

    o.pos = float4(x, y, 0, 1);
    o.uv  = float2(u, v);
    return o;
}

// sRGB → linear (per-channel, fast)
float3 srgbToLinear(float3 s)
{
    float3 lo = s / 12.92;
    float3 hi = pow((s + 0.055) / 1.055, 2.4);
    return (s <= 0.04045) ? lo : hi;
}

float4 PSMain(VSOut i) : SV_TARGET
{
    // 素材 = Straight Alpha. RTV 是 _UNORM (非 _SRGB), 直接输出 sRGB 字节,
    // GPU 不做 sRGB encode → blend 在 Gamma 空间做 = AE 默认行为.
    float4 c = gTex.Sample(gLinearClamp, i.uv);
    if (c.a < 0.001) {
        return float4(0, 0, 0, 0);
    }
    return c * tint;
}

// M5: 纯色填充 (用于选中边框等). RTV 非 _SRGB → tint 直接写入即 sRGB 字节.
float4 PSMainSolid(VSOut i) : SV_TARGET
{
    return tint;
}

// M5: 实心 ▲ 三角. 同 PSMainSolid, 不做 sRGB→linear.
float4 PSMainTriangleUp(VSOut i) : SV_TARGET
{
    float distFromTip = 1.0 - i.uv.y;        // 0 = 尖 (顶), 1 = 底
    float halfW = distFromTip * 0.5;
    float dx = abs(i.uv.x - 0.5);
    if (dx > halfW) discard;
    return tint;
}
