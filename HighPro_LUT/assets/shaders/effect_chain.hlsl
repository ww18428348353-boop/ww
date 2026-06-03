// effect_chain.hlsl — 7 个 AE 效果链直接作用于源帧, 顺序固定:
//   1. 色相/饱和度
//   2. 亮度/对比度
//   3. 曲线 (LUT 256x1, 4通道: R=master, G=R, B=G, A=B)
//   4. 通道混合器 (3x4 仿射)
//   5. 颜色平衡 (smoothstep 三区权重)
//   6. 照片滤镜
//   7. 自然饱和度
//
// VS 与 fullscreen_quad 兼容 (cb0 同布局).
// PS 输入: t0 = 源帧, t1 = 256x1 曲线 LUT, s0 = 帧 sampler (POINT/LINEAR), s1 = LUT sampler (LINEAR/CLAMP)
// cb1: 7 效果参数

cbuffer QuadCB : register(b0)
{
    float4 uvRect;
    float4 posRect;
    float4 tint;
};

cbuffer EffectCB : register(b1)
{
    // 开关 + 影子保护
    int4 enableMask;       // x = enable bitmask (低 7 位); y = shadow_protect threshold (0..255), z/w 备用
    // 1. HSL
    float4 hsl;            // x=hue/360, y=sat/100, z=lit/100, w 备用
    // 2. BrtCtr
    float4 brtCtr;         // x=brt/255, y=ctr/100, z/w 备用
    // 3. 通道混合器 (3 行)
    float4 mixR;           // x=rr%, y=rg%, z=rb%, w=const%
    float4 mixG;           // x=gr%, y=gg%, z=gb%, w=const%
    float4 mixB;           // x=br%, y=bg%, z=bb%, w=const%
    int4   mixFlag;        // x = monochrome (0/1), 其他备用
    // 4. 颜色平衡
    float4 balShadow;      // xyz = R/G/B (-100..100)/100
    float4 balMid;
    float4 balHigh;
    int4   balFlag;        // x = preserveLuma (0/1)
    // 5. 照片滤镜
    float4 photoColor;     // xyz = sRGB filter rgb /255, w = density (0..1)
    int4   photoFlag;      // x = preserveLuma (0/1)
    // 6. 自然饱和度
    float4 vibrance;       // x = vibrance/100, y = saturation/100, z/w 备用
};

Texture2D    gFrame   : register(t0);
Texture2D    gCurve   : register(t1);     // 256x1, RGBA = (master, r, g, b)

SamplerState gFrameSampler : register(s0);
SamplerState gCurveSampler : register(s1);

struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };

static const uint2 IDX[6] = {
    uint2(0,0), uint2(1,0), uint2(0,1),
    uint2(1,0), uint2(1,1), uint2(0,1),
};

VSOut VSMain(uint vid : SV_VertexID)
{
    VSOut o;
    uint ix = IDX[vid].x, iy = IDX[vid].y;
    float x = (ix == 0) ? posRect.x : posRect.z;
    float y = (iy == 0) ? posRect.y : posRect.w;
    float u = (ix == 0) ? uvRect.x  : uvRect.z;
    float v = (iy == 0) ? uvRect.y  : uvRect.w;
    o.pos = float4(x, y, 0, 1);
    o.uv  = float2(u, v);
    return o;
}

// ----------------- 算法块 -----------------

// AE 的 "色相/饱和度" 用 HSV/HSB 模型 (不是 HSL)
// V = max(R,G,B), S = (V==0) ? 0 : (V-min)/V
float3 rgb2hsv(float3 c)
{
    float mx = max(max(c.r, c.g), c.b);
    float mn = min(min(c.r, c.g), c.b);
    float V  = mx;
    float d  = mx - mn;
    float S  = (mx == 0.0) ? 0.0 : d / mx;
    float H  = 0.0;
    if (d != 0.0) {
        if      (mx == c.r) H = (c.g - c.b) / d + (c.g < c.b ? 6 : 0);
        else if (mx == c.g) H = (c.b - c.r) / d + 2;
        else                H = (c.r - c.g) / d + 4;
        H /= 6;
    }
    return float3(H, S, V);
}

float3 hsv2rgb(float3 hsv)
{
    float H = frac(hsv.x) * 6.0;
    float S = saturate(hsv.y);
    float V = saturate(hsv.z);
    int   i = (int)floor(H);
    float f = H - i;
    float p = V * (1.0 - S);
    float q = V * (1.0 - S * f);
    float t = V * (1.0 - S * (1.0 - f));
    if      (i == 0) return float3(V, t, p);
    else if (i == 1) return float3(q, V, p);
    else if (i == 2) return float3(p, V, t);
    else if (i == 3) return float3(p, q, V);
    else if (i == 4) return float3(t, p, V);
    else             return float3(V, p, q);
}

float luma(float3 c) { return dot(c, float3(0.299, 0.587, 0.114)); }

// 1. 色相/饱和度 (AE 风格 HSB: H 旋转, S/B 加性百分比 [-100,+100])
//
//   AE 行为:
//     H: 直接旋转 (deg) - 输入参数 hsl.x = 用户设置的角度/360
//     S: 加性 [-100, +100]
//        正值: S = clamp(S + (1-S)*ds, 0, 1)   (向 1 靠)
//        负值: S = clamp(S * (1+ds), 0, 1)     (向 0 靠)
//        ds = saturation/100
//     B: 类似饱和度
float3 applyHsl(float3 c, float dh, float ds, float dl)
{
    float3 h = rgb2hsv(c);
    h.x = frac(h.x + dh + 1.0);
    // S
    if (ds > 0)       h.y = saturate(h.y + (1.0 - h.y) * ds);
    else              h.y = saturate(h.y * (1.0 + ds));
    // V (亮度)
    if (dl > 0)       h.z = saturate(h.z + (1.0 - h.z) * dl);
    else              h.z = saturate(h.z * (1.0 + dl));
    return saturate(hsv2rgb(h));
}

// 2. 亮度/对比度 (传统模式 = AE 经典: c = (c-0.5)*(1+ctr) + 0.5 + brt)
float3 applyBrtCtr(float3 c, float brt, float ctr)
{
    return saturate((c - 0.5) * (1.0 + ctr) + 0.5 + brt);
}

// 3. 曲线 (LUT 256x1, 通道 R=master, G=R, B=G, A=B)
float3 applyCurves(float3 c)
{
    // 取每个通道的 R/G/B 子曲线
    float3 ch;
    ch.r = gCurve.SampleLevel(gCurveSampler, float2(c.r, 0.5), 0).g;
    ch.g = gCurve.SampleLevel(gCurveSampler, float2(c.g, 0.5), 0).b;
    ch.b = gCurve.SampleLevel(gCurveSampler, float2(c.b, 0.5), 0).a;
    // 再叠 master (R 通道) — 等价于先各通道后整体
    ch.r = gCurve.SampleLevel(gCurveSampler, float2(ch.r, 0.5), 0).r;
    ch.g = gCurve.SampleLevel(gCurveSampler, float2(ch.g, 0.5), 0).r;
    ch.b = gCurve.SampleLevel(gCurveSampler, float2(ch.b, 0.5), 0).r;
    return saturate(ch);
}

// 4. 通道混合
float3 applyChMix(float3 c, float4 mr, float4 mg, float4 mb, bool mono)
{
    if (mono) {
        // 单色: 用 R 行系数生成灰度
        float gray = dot(c, mr.xyz) + mr.w;
        return saturate(float3(gray, gray, gray));
    }
    float3 outc;
    outc.r = dot(c, mr.xyz) + mr.w;
    outc.g = dot(c, mg.xyz) + mg.w;
    outc.b = dot(c, mb.xyz) + mb.w;
    return saturate(outc);
}

// 5. 颜色平衡 (smoothstep 三区)
float3 applyColorBalance(float3 c, float3 sBal, float3 mBal, float3 hBal, bool preserveLuma)
{
    float Y = luma(c);
    // 权重 (与 PS 经典公式接近)
    float wS = max(0.0, 1.0 - 2.0 * Y);
    float wH = max(0.0, 2.0 * Y - 1.0);
    float wM = 1.0 - wS - wH;

    // 各分量值 -1..1 → 加成系数. 0.5 是体感强度因子.
    float3 add = (sBal * wS + mBal * wM + hBal * wH) * 0.5;
    float3 outc = c + add;
    if (preserveLuma) {
        float Yn = luma(outc);
        outc *= (Yn > 1e-5) ? (Y / Yn) : 1.0;
    }
    return saturate(outc);
}

// 6. 照片滤镜
float3 applyPhotoFilter(float3 c, float3 fc, float density, bool preserveLuma)
{
    if (density <= 0.0) return c;
    float3 outc = lerp(c, fc, density);
    if (preserveLuma) {
        float Y  = luma(c);
        float Yn = luma(outc);
        outc *= (Yn > 1e-5) ? (Y / Yn) : 1.0;
    }
    return saturate(outc);
}

// 7. 自然饱和度 (vibrance + 普通 sat)
float3 applyVibrance(float3 c, float vib, float sat)
{
    float Y = luma(c);
    float curSat = max(max(c.r, c.g), c.b) - min(min(c.r, c.g), c.b);
    // vibrance 对低饱和加得多, 高饱和加得少
    float factor = 1.0 + sat + vib * (1.0 - curSat);
    return saturate(Y + (c - Y) * factor);
}

// ----------------- 主入口 -----------------

// sRGB → linear (写 sRGB RTV 用)
float3 srgbToLinear(float3 s)
{
    float3 lo = s / 12.92;
    float3 hi = pow((s + 0.055) / 1.055, 2.4);
    return (s <= 0.04045) ? lo : hi;
}

// 共用核心: 对原始 sRGB 颜色应用 7 效果链, 输出 sRGB 颜色 (尚未做 sRGB→linear)
float3 applyEffectChain(float3 c0)
{
    float3 c = c0;
    int em = enableMask.x;
    if (em & 0x01) c = applyHsl(c, hsl.x, hsl.y, hsl.z);
    if (em & 0x02) c = applyBrtCtr(c, brtCtr.x, brtCtr.y);
    if (em & 0x04) c = applyCurves(c);
    if (em & 0x08) c = applyChMix(c, mixR, mixG, mixB, mixFlag.x != 0);
    if (em & 0x10) c = applyColorBalance(c, balShadow.xyz, balMid.xyz, balHigh.xyz, balFlag.x != 0);
    if (em & 0x20) c = applyPhotoFilter(c, photoColor.xyz, photoColor.w, photoFlag.x != 0);
    if (em & 0x40) c = applyVibrance(c, vibrance.x, vibrance.y);

    // 影子保护
    int thr = enableMask.y;
    if (thr > 0) {
        float t = thr / 255.0;
        float lum0 = luma(c0);
        if (lum0 < t) {
            float k = saturate(lum0 / t);
            c = lerp(c0, c, k);
        }
    }
    return saturate(c);
}

// 主入口 (预览): RTV = _UNORM (非 _SRGB) → blend 在 Gamma (sRGB) 空间做,
// 与 AE 默认行为一致. 素材 = Straight Alpha, 配 BlendState SRC_ALPHA/INV_SRC_ALPHA,
// PS 不预乘, 直接输出 (rgb, a).
float4 PSMain(VSOut i) : SV_TARGET
{
    float4 src = gFrame.Sample(gFrameSampler, i.uv);
    if (src.a < 0.001) {
        return float4(0, 0, 0, 0);
    }
    float3 c = applyEffectChain(saturate(src.rgb));
    return float4(c, src.a) * tint;
}

// 烘焙入口: 输出原 sRGB 字节 (写 UNORM RTV → 直存 add_lut PNG)
// gFrame 当作"颜色图.png" (256x16 HALD-CLUT), 不参与 alpha 合成 → 不动.
float4 PSMainBake(VSOut i) : SV_TARGET
{
    float4 src = gFrame.Sample(gFrameSampler, i.uv);
    float3 c   = applyEffectChain(src.rgb);
    return float4(c, src.a);
}
