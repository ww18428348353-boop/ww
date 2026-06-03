"""
pp_exporter.py  ─  ColorAdjust → .pp 翻译层 (门面/facade)

本模块是对 pp_bake 的薄封装, 提供给 UI 层的简单 API:

  color_adjust_to_region(adjust, region_index=0) -> PPRegion
      把一个 ColorAdjust 翻译成一条 PPRegion

  color_adjust_to_pp(adjust, enabled_region_ids) -> PPFile
      "广播式" 翻译: 同一 ColorAdjust 复制到所有启用区域, 得到完整 PPFile

  diagnose_adjust(adjust) -> List[str]
      导出前的可翻译性诊断, 返回告警列表

  summary_text(pp, region_ids) -> str
      导出成功后的摘要, 用于确认对话框

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
翻译核心算法见 pp_bake.py (方式三: HSB→HSL行 / 其它→3x3矩阵, 两层独立)
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
"""

from __future__ import annotations
from typing import List

from modules.pp_format import PPFile, PPRegion
from modules.pp_bake import bake_region, bake_to_regions


# ═══════════════════════════════════════════════════════════════════
# 单个 ColorAdjust → 单个 PPRegion
# ═══════════════════════════════════════════════════════════════════

def color_adjust_to_region(adjust, region_index: int = 0) -> PPRegion:
    """把一个 ColorAdjust 对象翻译为单个 PPRegion."""
    region, _notes = bake_region(adjust, region_index=region_index)
    return region


# ═══════════════════════════════════════════════════════════════════
# 单个 ColorAdjust → 完整 PPFile (广播到所有启用区域)
# ═══════════════════════════════════════════════════════════════════

def color_adjust_to_pp(
    adjust,
    enabled_region_ids: List[int],
) -> PPFile:
    """
    "广播式" 翻译: 把当前 TCP 工具的一套 ColorAdjust, 复制到每一个启用的区域,
    得到一份完整 .pp 文件, 可直接用 pp_format.write_pp 写到磁盘.

    参数:
      adjust             当前调色参数 (ColorAdjust 或有相同字段的任意对象)
      enabled_region_ids 启用的区域 ID 列表 (通常从 MColorKey 解出来)

    返回:
      PPFile
    """
    regions, _notes = bake_to_regions(adjust, enabled_region_ids)
    return PPFile(magic=1001, ui_index=0, alpha=255, regions=regions)


# ═══════════════════════════════════════════════════════════════════
# 可翻译性诊断 (UI 层导出前提示用户)
# ═══════════════════════════════════════════════════════════════════

def diagnose_adjust(adjust) -> List[str]:
    """
    检测 adjust 里是否有"当前翻译器处理不了 (或需要近似)"的参数,
    返回告警列表. 返回空列表意味着一切都能无损翻译.
    """
    warnings: List[str] = []

    # 色阶/曲线/色彩平衡/LUT 这些会进入 3x3 矩阵拟合, 会有近似损失
    # 运行一次烘焙, 如果 bake_region 给出 notes, 转发给调用方
    _region, notes = bake_region(adjust)
    warnings.extend(notes)

    # 3D LUT 目前完全无法拟合成 3x3 矩阵, 单独告警
    if getattr(adjust, 'lut_3d', None):
        warnings.append("3D LUT 包含非线性颜色映射, 本版 3x3 线性拟合会有较大失真")

    return warnings


# ═══════════════════════════════════════════════════════════════════
# 摘要 (UI 导出成功后显示)
# ═══════════════════════════════════════════════════════════════════

def summary_text(pp: PPFile, region_ids: List[int]) -> str:
    """生成一段人类可读的摘要, 用于确认对话框"""
    lines = []
    lines.append(f"区域数: {pp.region_count}")
    lines.append(f"启用区域 ID: {', '.join(str(i) for i in region_ids)}")
    if pp.regions:
        r0 = pp.regions[0]
        lines.append(
            f"H={r0.h}  S={r0.s} (Q500={r0.saturation:.2f})  "
            f"L={r0.l} (Q500={r0.lightness:.2f})  "
            f"LB={r0.line_bright}%  C={r0.contrast} ({r0.contrast_factor:.2f}x)")
        m = r0.matrix
        lines.append(
            f"矩阵: [[{m[0][0]},{m[0][1]},{m[0][2]}], "
            f"[{m[1][0]},{m[1][1]},{m[1][2]}], "
            f"[{m[2][0]},{m[2][1]},{m[2][2]}]]")
    return "\n".join(lines)