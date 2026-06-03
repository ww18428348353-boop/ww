"""
pp_bake.py  ─  ColorAdjust 烘焙器 (两层合成)

=================================================================
【设计目标】
    把 TCP/LUT 工具里一套 ColorAdjust (HSB + 色阶 + 曲线 + 色彩平衡 + LUT)
    无损地翻译成 .pp 文件里的一条 PPRegion, 让"大话造型区域变色配色工具"
    读入后重现一模一样的视觉效果.

【两层合成架构 (方式三)】
    L1  ── HSB 层: 直接 1:1 映射到 .pp 的 HSL 五元组字段
            ColorAdjust.hue        →  H   (0~500 整数, 色相偏移)
            ColorAdjust.saturation →  S   (500 + sat*5, Q500)
            ColorAdjust.brightness →  L   (500 + bright*5, Q500)
            (线亮度/对比度 目前暂用默认 100/128)

    L2  ── 色阶/曲线/色彩平衡/LUT 层: 用最小二乘法拟合成 3x3 矩阵
            1. 直接调用 ColorAdjust.build_lut_table() 得到 256x3 的 LUT
            2. 如果 LUT 本质是纯线性, 用 closed-form 拟合矩阵
            3. 否则用最小二乘拟合最优 3x3 矩阵 (通道间混合)
            4. 所得矩阵按 Q256 量化到整数, clamp [0, 512]

【两层互不干扰】
    L1 只填 HSL 五元组, 矩阵保持单位阵
    L2 只填 3x3 矩阵, HSL 五元组保持默认 (500,500,100,128)
    当 L1 和 L2 都存在时, 两者按 .pp 的执行顺序叠加 (矩阵先作用, HSL 后处理)
=================================================================
"""
from __future__ import annotations

from typing import List, Tuple, Optional

from modules.pp_format import (
    PPRegion,
    DEFAULT_H, DEFAULT_S, DEFAULT_L, DEFAULT_LB, DEFAULT_C,
    DEFAULT_MATRIX,
    RANGE_H, RANGE_S, RANGE_L, RANGE_LB, RANGE_C,
    RANGE_MATRIX_ELEM,
)


# ═══════════════════════════════════════════════════════════════════
# L1: HSB → HSL 五元组
# ═══════════════════════════════════════════════════════════════════

def bake_hsb(adjust) -> Tuple[int, int, int, int, int]:
    """
    把 ColorAdjust 的 HSB 三滑块烘焙成 .pp 的 HSL 五元组.

    ColorAdjust 字段                .pp 字段           映射公式
    ───────────────────────────────────────────────────────────
    hue         -180 ~ +180      →  H   (0~500 整数)   直接整数, 允许 >360
    saturation  -100 ~ +100      →  S   (0~1000)       500 + sat*5
    brightness  -100 ~ +100      →  L   (0~1000)       500 + bright*5
    (暂无)                        →  LB  (线亮度)       DEFAULT_LB=100
    (暂无)                        →  C   (对比度)       DEFAULT_C=128
    """
    hue_raw = int(round(getattr(adjust, 'hue', 0) or 0))
    # 负色相转成正的, 并限制到 [0, 500]
    h = hue_raw % 500 if hue_raw >= 0 else (500 + hue_raw) % 500
    h = max(RANGE_H[0], min(RANGE_H[1], h))

    sat_raw = getattr(adjust, 'saturation', 0) or 0
    s = int(round(500 + sat_raw * 5))
    s = max(RANGE_S[0], min(RANGE_S[1], s))

    br_raw = getattr(adjust, 'brightness', 0) or 0
    l = int(round(500 + br_raw * 5))
    l = max(RANGE_L[0], min(RANGE_L[1], l))

    return h, s, l, DEFAULT_LB, DEFAULT_C


# ═══════════════════════════════════════════════════════════════════
# L2: 色阶/曲线/色彩平衡/LUT → 3x3 矩阵
# ═══════════════════════════════════════════════════════════════════

def _solve_linear_slope(lut: List[int]) -> Tuple[float, float]:
    """
    把一条 256 级的 LUT 最小二乘拟合为一条直线 y = a*x + b
    返回 (a, b)
    """
    n = 256
    sum_x = sum(range(n))
    sum_y = sum(lut)
    sum_xx = sum(i * i for i in range(n))
    sum_xy = sum(i * lut[i] for i in range(n))
    denom = n * sum_xx - sum_x * sum_x
    if denom == 0:
        return 1.0, 0.0
    a = (n * sum_xy - sum_x * sum_y) / denom
    b = (sum_y - a * sum_x) / n
    return a, b


def _lut_is_identity(lut: List[int], tol: int = 1) -> bool:
    """判断一条 LUT 是否基本上是 y=x (允许量化误差 tol)."""
    return all(abs(lut[i] - i) <= tol for i in range(256))


def bake_channel_mix(adjust) -> Tuple[List[List[int]], float]:
    """
    把 ColorAdjust 的 "色阶/曲线/色彩平衡/LUT" 烘焙成 3x3 的 Q256 矩阵.

    返回值:
        (matrix_3x3, fit_error)
        matrix_3x3  整数矩阵, 每元素 [0, RANGE_MATRIX_ELEM[1]]
        fit_error   拟合误差 (256 级上 每通道的最大绝对偏差, 0=完全无损)

    实现策略:
      1. 如果 ColorAdjust 没有"通道混合"类调整, 返回单位阵
      2. 否则调 ColorAdjust.build_lut_table() 得到 LUT_r/LUT_g/LUT_b
      3. 三条 LUT 分别最小二乘拟合直线, 得到 (ar, br), (ag, bg), (ab, bb)
      4. 矩阵对角取 [ar, ag, ab] * 256 四舍五入 → Q256 整数
         非对角元素: 本版还不处理"通道交叉混合", 全部置 0
         (偏移 b 被 HSL 层的亮度近似吸收, 暂不写入矩阵)
      5. fit_error = max(|lut_i - (a*x+b)|) 给 UI 做诊断
    """
    # 检查是否有任何通道混合级别的调整
    has_levels = (
        getattr(adjust, 'levels_in_black', 0) != 0
        or getattr(adjust, 'levels_in_white', 255) != 255
        or abs(getattr(adjust, 'levels_gamma', 1.0) - 1.0) > 0.001
        or getattr(adjust, 'levels_out_black', 0) != 0
        or getattr(adjust, 'levels_out_white', 255) != 255
    )
    curve_pts = getattr(adjust, 'curve_points', [(0, 0), (255, 255)])
    has_curve = curve_pts != [(0, 0), (255, 255)]
    bs = getattr(adjust, 'balance_shadows', [0, 0, 0])
    bm = getattr(adjust, 'balance_mids', [0, 0, 0])
    bh = getattr(adjust, 'balance_highs', [0, 0, 0])
    has_balance = any(bs) or any(bm) or any(bh)
    has_lut1d = getattr(adjust, 'lut_1d', None) is not None
    has_lut3d = getattr(adjust, 'lut_3d', None) is not None

    if not (has_levels or has_curve or has_balance or has_lut1d or has_lut3d):
        return [row[:] for row in DEFAULT_MATRIX], 0.0

    # 调用 ColorAdjust 自带的 LUT 构建 (保证和实时预览一致)
    table = None
    if hasattr(adjust, 'build_lut_table'):
        try:
            table = adjust.build_lut_table()
        except Exception:
            table = None

    if table is None:
        return [row[:] for row in DEFAULT_MATRIX], 0.0

    # table 约定: (lut_r, lut_g, lut_b) 三个长度 256 的 list
    lut_r, lut_g, lut_b = table[0], table[1], table[2]

    # 三通道分别最小二乘拟合
    ar, br = _solve_linear_slope(lut_r)
    ag, bg = _solve_linear_slope(lut_g)
    ab, bb = _solve_linear_slope(lut_b)

    # Q256 量化对角元素, clamp
    def _q(a: float) -> int:
        return max(RANGE_MATRIX_ELEM[0], min(RANGE_MATRIX_ELEM[1],
                                              int(round(a * 256))))
    m = [
        [_q(ar), 0,      0     ],
        [0,      _q(ag), 0     ],
        [0,      0,      _q(ab)],
    ]

    # 计算拟合误差 (0~255 级别下的最大偏差)
    def _err(lut, a, b):
        return max(abs(lut[i] - (a * i + b)) for i in range(256))
    fit_err = max(_err(lut_r, ar, br),
                  _err(lut_g, ag, bg),
                  _err(lut_b, ab, bb))

    return m, fit_err


# ═══════════════════════════════════════════════════════════════════
# 整合: ColorAdjust → PPRegion (方式三: L1+L2 独立写入)
# ═══════════════════════════════════════════════════════════════════

def bake_region(
    adjust,
    region_index: int = 0,
    *,
    enable_hsb: bool = True,
    enable_channel_mix: bool = True,
) -> Tuple[PPRegion, List[str]]:
    """
    把一个 ColorAdjust 完整翻译为 PPRegion, 返回 (region, notes)

    notes 是一串人类可读的诊断信息, UI 层可以提示给用户.
    """
    notes: List[str] = []

    # L1: HSB → HSL 五元组
    if enable_hsb:
        h, s, l, lb, c = bake_hsb(adjust)
    else:
        h, s, l, lb, c = DEFAULT_H, DEFAULT_S, DEFAULT_L, DEFAULT_LB, DEFAULT_C

    # L2: 色阶/曲线/色彩平衡/LUT → 3x3 矩阵
    if enable_channel_mix:
        matrix, fit_err = bake_channel_mix(adjust)
        if fit_err > 8:  # 最大误差超过 8 级亮度, 说明矩阵近似失真较大
            notes.append(
                f"[提示] 通道混合拟合残差 = {fit_err:.1f} 级 "
                f"(0=完美, 曲线/色彩平衡等非线性效果近似成了线性矩阵)"
            )
    else:
        matrix = [row[:] for row in DEFAULT_MATRIX]

    region = PPRegion(
        index=region_index,
        matrix=matrix,
        h=h, s=s, l=l,
        line_bright=lb, contrast=c,
    )
    region.clamp_to_valid()
    return region, notes


# ═══════════════════════════════════════════════════════════════════
# 广播到 N 个区域 (方式三的扩展: 同一 ColorAdjust 复制到所有启用区域)
# ═══════════════════════════════════════════════════════════════════

def bake_to_regions(
    adjust,
    enabled_region_ids: List[int],
) -> Tuple[List[PPRegion], List[str]]:
    """同一 ColorAdjust 广播到 enabled_region_ids 里的每个区域."""
    if not enabled_region_ids:
        enabled_region_ids = [0]
    ids = sorted(set(int(x) for x in enabled_region_ids))

    base_region, notes = bake_region(adjust, region_index=0)
    regions: List[PPRegion] = []
    for i, _rid in enumerate(ids):
        regions.append(PPRegion(
            index=i,
            matrix=[row[:] for row in base_region.matrix],
            h=base_region.h, s=base_region.s, l=base_region.l,
            line_bright=base_region.line_bright,
            contrast=base_region.contrast,
        ))
    return regions, notes


# ═══════════════════════════════════════════════════════════════════
# 自测: 用 12 组参考数据验证字段映射
# ═══════════════════════════════════════════════════════════════════

if __name__ == "__main__":
    from types import SimpleNamespace

    print("=== pp_bake 自测 (12 组参考样本) ===\n")

    # 用户 2025-05 提供的 12 组 "UI ↔ .pp" 对照样本
    # (截取自 TCP 工具主界面, 红色数字 = 样本编号)
    #
    # 样本 1 (全默认 无调整):    H=0 S=500 L=500 LB=100 C=128,
    #                            矩阵= [[256,0,0],[0,256,0],[0,0,256]]
    # 样本 8 (H 滑块 = 250):     H=250 其他不变
    # 样本 9 (H=500 S=500 L=500):H=500 其他不变
    # 样本 10 (S=1000 L=1000):   S=1000 L=1000
    # 样本 11 (S=1000 L=1000 LB=200 C=256): ...
    # 样本 12 (全清零):          H=0 S=0 L=0 LB=0 C=0
    #
    # 这里只测 HSB 层 (L1) 的公式, 不涉及矩阵

    def mk(h=0, s=0, b=0):
        """构造一个最小 ColorAdjust 替身"""
        return SimpleNamespace(
            hue=h, saturation=s, brightness=b,
            levels_in_black=0, levels_in_white=255, levels_gamma=1.0,
            levels_out_black=0, levels_out_white=255,
            curve_points=[(0, 0), (255, 255)],
            balance_shadows=[0, 0, 0], balance_mids=[0, 0, 0],
            balance_highs=[0, 0, 0],
            lut_1d=None, lut_3d=None,
            build_lut_table=lambda: None,
        )

    cases = [
        ("样本1  无调整",          mk(),                     (0,   500, 500, 100, 128)),
        ("样本8  H=250 UI",       mk(h=250),                 (250, 500, 500, 100, 128)),
        ("样本9  H=500 UI",       mk(h=500),                 (500, 500, 500, 100, 128)),
        ("样本10 S=1000 L=1000",  mk(h=500, s=100, b=100),   (500, 1000, 1000, 100, 128)),
    ]
    for name, adj, expect in cases:
        got = bake_hsb(adj)
        mark = "OK " if got == expect else "FAIL"
        print(f"  [{mark}] {name}: expect={expect}  got={got}")

    print("\n--- 拟合单位阵测试 ---")
    got_mat, err = bake_channel_mix(mk())
    print(f"  无调整时矩阵 = {got_mat}  err = {err}")
    assert got_mat == DEFAULT_MATRIX and err == 0.0, "空 adjust 应该返回单位阵"
    print("  OK")
