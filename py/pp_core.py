"""
pp_core.py  ─  PP 14 字段调色管线 (纯算法层, 零 UI 依赖)

本模块从 pp_pipeline_page.py 抽出, 让 pp_pipeline_page.py 专注 UI,
并且让 tcp_loader / 命令行批处理 / 单元测试 都能复用算法.

提供两条调用路径:

  1) apply_pp_adjust(rgba_img, adj)           ← 老路径, 任意 RGBA 图
       适用于: 已经解码到 RGBA 的 TGA / PNG 预览图.

  2) apply_pp_adjust_palette(palette, adj)    ← 新路径, 仅变换调色板
       适用于: TCP 这类索引图 — 调色板才 256 色, 一次变换抵 N×N×4 字节,
       这是大话原版工具的真实做法 (索引图本身 1 个像素 1 字节, 变色 = 改调色板).

变换公式 (与 .pp 文件 1:1 同步, 见模块顶部注释链):
    步骤 1) 通道混合矩阵 (Q256 定点)
    步骤 2) RGB → HSL → 修改 H/S/L → RGB
    步骤 3) 线亮度 (Q100)  →  RGB *= lb/100
    步骤 4) 对比度  (Q128)  →  (RGB - 128) * c/128 + 128
"""
from __future__ import annotations

import numpy as np

from modules import pp_format


# ═══════════════════════════════════════════════════════════════════
# PPAdjust — 14 字段数据模型 (与 .pp 一组方案 1:1)
# ═══════════════════════════════════════════════════════════════════
class PPAdjust:
    """PP官方管线 调色参数 (14 个字段, 严格对应 .pp 文件一个 Region/方案)"""

    def __init__(self):
        # 3x3 通道混合矩阵 (Q256 定点, 范围 [0, 512])
        # 默认: 单位阵 [[256,0,0],[0,256,0],[0,0,256]] ← .pp 文件的中性状态
        self.rr, self.rg, self.rb = 256,   0,   0   # 红通道
        self.gr, self.gg, self.gb =   0, 256,   0   # 绿通道
        self.br, self.bg, self.bb =   0,   0, 256   # 蓝通道
        # HSL 后处理 (5 字段)
        self.h  = 0       # 色相偏移 [0, 500]  (默认 0 = 无偏移)
        self.s  = 500     # 饱和度   [0, 1000] (Q500, 默认 500 = 100%)
        self.l  = 500     # 亮度     [0, 1000] (Q500, 默认 500 = 100%)
        self.lb = 100     # 线亮度   [0, 200]  (Q100, 默认 100 = 100%)
        self.c  = 128     # 对比度   [0, 256]  (Q128, 默认 128 = 100%)

    # ────────────────────────────────────────────────────────────
    # 状态判断
    # ────────────────────────────────────────────────────────────
    def has_any_adjustment(self) -> bool:
        """是否有任何非中性调整"""
        if (self.rr, self.rg, self.rb,
            self.gr, self.gg, self.gb,
            self.br, self.bg, self.bb) != (256, 0, 0, 0, 256, 0, 0, 0, 256):
            return True
        if self.h != 0:
            return True
        if self.s != 500:
            return True
        if self.l != 500:
            return True
        if self.lb != 100:
            return True
        if self.c != 128:
            return True
        return False

    def reset(self) -> None:
        """复位为中性 (= .pp 默认方案)"""
        self.rr, self.rg, self.rb = 256, 0, 0
        self.gr, self.gg, self.gb = 0, 256, 0
        self.br, self.bg, self.bb = 0, 0, 256
        self.h, self.s, self.l = 0, 500, 500
        self.lb, self.c = 100, 128

    def clone(self) -> "PPAdjust":
        a = PPAdjust()
        a.rr, a.rg, a.rb = self.rr, self.rg, self.rb
        a.gr, a.gg, a.gb = self.gr, self.gg, self.gb
        a.br, a.bg, a.bb = self.br, self.bg, self.bb
        a.h, a.s, a.l = self.h, self.s, self.l
        a.lb, a.c = self.lb, self.c
        return a

    # ────────────────────────────────────────────────────────────
    # 与 pp_format 互转
    # ────────────────────────────────────────────────────────────
    def to_pp_region(self, index: int = 0) -> "pp_format.PPRegion":
        return pp_format.PPRegion(
            index=index,
            matrix=[
                [self.rr, self.rg, self.rb],
                [self.gr, self.gg, self.gb],
                [self.br, self.bg, self.bb],
            ],
            h=self.h, s=self.s, l=self.l,
            line_bright=self.lb, contrast=self.c,
        )

    @classmethod
    def from_pp_region(cls, r: "pp_format.PPRegion") -> "PPAdjust":
        a = cls()
        a.rr, a.rg, a.rb = r.matrix[0]
        a.gr, a.gg, a.gb = r.matrix[1]
        a.br, a.bg, a.bb = r.matrix[2]
        a.h, a.s, a.l = r.h, r.s, r.l
        a.lb, a.c = r.line_bright, r.contrast
        return a


# ═══════════════════════════════════════════════════════════════════
# 路径 1: 任意 RGBA 图像 → 应用 PP
# ═══════════════════════════════════════════════════════════════════
def apply_pp_adjust(img: np.ndarray, adj: PPAdjust) -> np.ndarray:
    """
    把 PPAdjust 按照 .pp 官方管线应用到 RGBA 图像上.
    img: HxWx4 uint8 (RGBA) 或 HxWx3 uint8 (RGB)
    返回: 同形 uint8, alpha 原样保留

    防御性策略:
      · 整个 pipeline 在 float32 上做, 全程不依赖布尔掩码赋值
      · 用 np.errstate 吞掉 divide-by-zero 警告, 算完再用 np.nan_to_num 兜底
      · 中间值超出 [0,255] 时立即 clamp, 避免 HSL 转换出现负数/NaN
      · 最终 clip 到 [0,255] 再转 uint8
    """
    if not adj.has_any_adjustment():
        return img.copy()
    if img is None or img.size == 0:
        return img

    if img.ndim == 3 and img.shape[2] == 4:
        rgb   = img[..., :3].astype(np.float32)
        alpha = img[..., 3:4].astype(np.float32)
    elif img.ndim == 3 and img.shape[2] == 3:
        rgb   = img.astype(np.float32)
        alpha = None
    else:
        return img.copy()

    rgb = _pipeline_rgb(rgb, adj)

    if alpha is not None:
        out = np.concatenate([rgb, alpha], axis=-1)
    else:
        out = rgb
    return out.astype(np.uint8)


# ═══════════════════════════════════════════════════════════════════
# 路径 2: 256 色调色板 → 应用 PP (TCP 索引图最快路径)
# ═══════════════════════════════════════════════════════════════════
def apply_pp_adjust_palette(palette: np.ndarray, adj: PPAdjust) -> np.ndarray:
    """
    对 256 色调色板做 PP 变换, 返回新的调色板.
    palette: (256, 3) 或 (256, 4) uint8
    返回: 同形 uint8

    对于 TCP 索引图, 这是大话原版的官方做法:
        - 索引图本身就是 H×W×1 字节 (像素 = 调色板下标)
        - "变色" = 修改调色板, 索引图本身完全不动
        - 256 色 vs 一张 256x256x4 图: 速度快 ~256 倍
    """
    if palette is None or palette.size == 0:
        return palette
    if not adj.has_any_adjustment():
        return palette.copy()

    if palette.ndim == 2 and palette.shape[1] == 4:
        rgb   = palette[:, :3].astype(np.float32)
        alpha = palette[:, 3:4].astype(np.float32)
    elif palette.ndim == 2 and palette.shape[1] == 3:
        rgb   = palette.astype(np.float32)
        alpha = None
    else:
        return palette.copy()

    # 把 (256, 3) 当作 (1, 256, 3) 喂进同一条管线
    rgb = _pipeline_rgb(rgb[np.newaxis, ...], adj)[0]

    if alpha is not None:
        out = np.concatenate([rgb, alpha], axis=-1)
    else:
        out = rgb
    return np.clip(out, 0, 255).astype(np.uint8)


# ═══════════════════════════════════════════════════════════════════
# 内部: 共用的 (H, W, 3) float32 管线
# ═══════════════════════════════════════════════════════════════════
def _pipeline_rgb(rgb: np.ndarray, adj: PPAdjust) -> np.ndarray:
    """
    rgb: (..., 3) float32, 值域 [0, 255]
    返回: 同形 float32 (未 clip), 调用方负责 clamp + uint8.
    """
    # ─────── 步骤 1: 3x3 通道混合 (Q256 定点) ───────
    M = np.array([
        [adj.rr, adj.rg, adj.rb],
        [adj.gr, adj.gg, adj.gb],
        [adj.br, adj.bg, adj.bb],
    ], dtype=np.float32) / 256.0
    rgb = rgb @ M.T
    rgb = np.clip(rgb, 0.0, 255.0)

    # ─────── 步骤 2~4: RGB → HSL → 修改 → RGB ───────
    if adj.h != 0 or adj.s != 500 or adj.l != 500:
        rgb = _apply_hsl_adjust(rgb, adj.h, adj.s, adj.l)

    # ─────── 步骤 5: 线亮度 (Q100) ───────
    if adj.lb != 100:
        rgb = rgb * (adj.lb / 100.0)

    # ─────── 步骤 6: 对比度 (Q128) ───────
    if adj.c != 128:
        rgb = (rgb - 128.0) * (adj.c / 128.0) + 128.0

    return np.clip(rgb, 0.0, 255.0)


def _apply_hsl_adjust(rgb: np.ndarray, h_slider: int, s_slider: int, l_slider: int) -> np.ndarray:
    """在 RGB float32 [0,255] 上应用 .pp 的 H/S/L 修改, 返回同形 float32."""
    r = rgb[..., 0] / 255.0
    g = rgb[..., 1] / 255.0
    b = rgb[..., 2] / 255.0

    cmax = np.maximum(np.maximum(r, g), b)
    cmin = np.minimum(np.minimum(r, g), b)
    diff = cmax - cmin
    L = (cmax + cmin) * 0.5

    with np.errstate(divide='ignore', invalid='ignore'):
        safe_diff = np.where(diff > 1e-6, diff, 1.0)
        h_r = ((g - b) / safe_diff) % 6.0
        h_g = ((b - r) / safe_diff) + 2.0
        h_b = ((r - g) / safe_diff) + 4.0
        H = np.where(cmax == r, h_r,
            np.where(cmax == g, h_g, h_b))
        H = np.where(diff > 1e-6, H, 0.0) * 60.0   # 0~360

        denom_low  = cmax + cmin
        denom_high = 2.0 - cmax - cmin
        s_low  = np.where(denom_low  > 1e-6, diff / np.where(denom_low  > 1e-6, denom_low,  1.0), 0.0)
        s_high = np.where(denom_high > 1e-6, diff / np.where(denom_high > 1e-6, denom_high, 1.0), 0.0)
        S = np.where(L <= 0.5, s_low, s_high)
        S = np.where(diff > 1e-6, S, 0.0)

    H = np.nan_to_num(H, nan=0.0, posinf=360.0, neginf=0.0)
    S = np.nan_to_num(S, nan=0.0, posinf=1.0,   neginf=0.0)
    L = np.nan_to_num(L, nan=0.0, posinf=1.0,   neginf=0.0)
    S = np.clip(S, 0.0, 1.0)
    L = np.clip(L, 0.0, 1.0)

    if h_slider != 0:
        H = (H + (h_slider / 500.0) * 360.0) % 360.0
    if s_slider != 500:
        S = np.clip(S * (s_slider / 500.0), 0.0, 1.0)
    if l_slider != 500:
        L = np.clip(L * (l_slider / 500.0), 0.0, 1.0)

    # HSL → RGB
    C = (1.0 - np.abs(2.0 * L - 1.0)) * S
    Hp = H / 60.0
    X = C * (1.0 - np.abs((Hp % 2.0) - 1.0))
    m = L - C * 0.5

    i = np.floor(Hp).astype(np.int32) % 6
    zero = np.zeros_like(H, dtype=np.float32)
    r_out = np.where(i == 0, C,
            np.where(i == 1, X,
            np.where(i == 2, zero,
            np.where(i == 3, zero,
            np.where(i == 4, X,    C)))))
    g_out = np.where(i == 0, X,
            np.where(i == 1, C,
            np.where(i == 2, C,
            np.where(i == 3, X,
            np.where(i == 4, zero, zero)))))
    b_out = np.where(i == 0, zero,
            np.where(i == 1, zero,
            np.where(i == 2, X,
            np.where(i == 3, C,
            np.where(i == 4, C,    X)))))
    r_out = np.nan_to_num(r_out + m, nan=0.0)
    g_out = np.nan_to_num(g_out + m, nan=0.0)
    b_out = np.nan_to_num(b_out + m, nan=0.0)

    return np.stack([r_out, g_out, b_out], axis=-1) * 255.0
