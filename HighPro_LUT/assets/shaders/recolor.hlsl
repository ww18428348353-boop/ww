// recolor.hlsl
// PS 用 HALD-CLUT (16×16×16, 平铺成 256×16) 对源像素做颜色查找.
//
// HALD 编码 (颜色图.png 实证):
//   像素 (x, y), x∈[0,255], y∈[0,15]:
//     R_in = (x mod 16) * 17
//     G_in = y * 17
//     B_in = (x / 16) * 17
//
// 逆运算 (运行时):
//   src.r * 15 → r4 ∈ [0..15]   (R 在切片内的列)
//   src.g * 15 → g4 ∈ [0..15]   (Y 行)
//   src.b * 15 → b4 ∈ [0..15]   (B 切片号)
//   B 切片间做线性插值; 切片内的 R/G 用 sampler 双线性自动处理.
//
// VS / cb0 共用 fullscreen_quad.hlsl 的 QuadCB.

cbuffer QuadCB : register(b0)
{
    float4 uvRect;     // 源帧 UV (0..1, V 已翻转 → uvRect = 0,1,1,0)
    float4 posRect;    // 输出 NDC 矩形
    float4 tint;
};

Texture2D    gFrame  : register(t0);   // 源 TGA 帧
Texture2D    gLut    : register(t1);   // HALD-CLUT 256×16

SamplerState gFrameSampler : register(s0);   // POINT/LINEAR (与 FrameRenderer 选择一致)
SamplerState gLutSampler   : register(s1);   // LINEAR / CLAMP

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

// ---- HALD-CLUT 查找 ----
// 输入: src.rgb ∈ [0,1]
// 输出: 经 LUT 映射后的 rgb (alpha 由调用者保留)
float3 SampleHald(float3 c)
{
    float3 q = saturate(c) * 15.0f;
    float r = q.r, g = q.g, b = q.b;

    float b0f = floor(b);
    float b1f = min(b0f + 1.0f, 15.0f);
    float bf  = b - b0f;

    float u0 = (b0f * 16.0f + r + 0.5f) / 256.0f;
    float u1 = (b1f * 16.0f + r + 0.5f) / 256.0f;
    float v  = (g + 0.5f) / 16.0f;

    float3 c0 = gLut.SampleLevel(gLutSampler, float2(u0, v), 0).rgb;
    float3 c1 = gLut.SampleLevel(gLutSampler, float2(u1, v), 0).rgb;
    return lerp(c0, c1, bf);
}

// sRGB → linear
float3 srgbToLinear(float3 s)
{
    float3 lo = s / 12.92;
    float3 hi = pow((s + 0.055) / 1.055, 2.4);
    return (s <= 0.04045) ? lo : hi;
}

float4 PSMain(VSOut i) : SV_TARGET
{
    // 素材 = Straight Alpha. RTV = _UNORM (非 _SRGB) → blend 在 Gamma (sRGB)
    // 空间做, 与 AE 默认行为一致 (避免线性合成时半透明区光晕亮度抬升).
    float4 src = gFrame.Sample(gFrameSampler, i.uv);
    if (src.a < 0.001) {
        return float4(0, 0, 0, 0);
    }
    // LUT 查找 (输入与输出都是 sRGB 数值, 直接写出, 不做 srgbToLinear)
    float3 mapped = SampleHald(saturate(src.rgb));
    return float4(mapped, src.a) * tint;
}
