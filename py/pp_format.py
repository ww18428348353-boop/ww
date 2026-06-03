"""
pp_format.py  ─  "大话造型区域变色配色工具" .pp 文件读写器

============================================================
文件结构 (已根据真实样本逆向校验, 100% 与原工具二进制对齐):

    行 1 : magic  ui_index  alpha          ← 头部 (例: "1001 0 255")
    行 2 : region_count                    ← 区域数量 N (例: "7")
    行 3 起, 每 4 行一组, 共 N 组:
        矩阵行1 :  Rx  Gx  Bx              ← 新R 对 (R,G,B) 的权重 (Q8: 256=1.0)
        矩阵行2 :  Ry  Gy  By              ← 新G 权重
        矩阵行3 :  Rz  Gz  Bz              ← 新B 权重
        HSL 行  :  H  S  L  LineBright  Contrast
                   │  │  │  │           │
                   │  │  │  │           └─ 对比度      (Q7:   128 = 1.0x)
                   │  │  │  └───────────── 线亮度(%)   (100   = 100%)
                   │  │  └──────────────── 明度        (Q500: 500 = 1.0)
                   │  └─────────────────── 饱和度      (Q500: 500 = 1.0)
                   └────────────────────── 色相偏移(°) (0~360, 可大于 360)

字段与 3D_Tool / "大话造型区域变色配色工具" 的 UI 控件一一对应:
    ┌──────────────────────┬────────────────────────────┐
    │  UI 控件             │  .pp 字段                  │
    ├──────────────────────┼────────────────────────────┤
    │  红-红 / 红-绿 / 红-蓝│  矩阵行1  (Rx Gx Bx)       │
    │  绿-红 / 绿-绿 / 绿-蓝│  矩阵行2  (Ry Gy By)       │
    │  蓝-红 / 蓝-绿 / 蓝-蓝│  矩阵行3  (Rz Gz Bz)       │
    │  HSL  H              │  HSL行[0]                  │
    │  HSL  S              │  HSL行[1]                  │
    │  HSL  L              │  HSL行[2]                  │
    │  区域 线亮度          │  HSL行[3]                  │
    │  区域 对比度          │  HSL行[4]                  │
    └──────────────────────┴────────────────────────────┘

写出策略:
    - 默认 "严格模式" : 保留原格式约定 → 每个字段后加空格、行尾 '\n',
      这样 diff 读取-再写出的 byte 级结果与常见样本一致.
    - 允许 strict=False 时去掉尾部空格 (更干净, 但部分旧版解析器会挑剔).
============================================================
"""
from __future__ import annotations

import os
from dataclasses import dataclass, field
from typing import List, Tuple, Union


# ────────────────────────────────────────────────────────────
# 实证取值边界
#
# 数据源 1) D:\DH2client\npc\res_art_png\res2d\shape 下 14 个真实 .pp
#         / 41 个区域样本统计
# 数据源 2) 2025-05 用户提供的 12 组 "UI 滑块值 ↔ .pp 文本" 完美对照样本
#         (彻底确认 .pp 字段与 UI 控件的 1:1 映射关系)
#
# 结论: UI 滑块的所有合法值都直接整数落盘, 无额外缩放.
# 允许全 0 (样本 #12) 与全满值 (样本 #11).
# ────────────────────────────────────────────────────────────
RANGE_H  = (0, 500)      # 色相偏移  UI 滑块 [0, 500]   (样本 8=250, 9/10/11=500)
RANGE_S  = (0, 1000)     # 饱和度    UI 滑块 [0, 1000]  (样本 10/11=1000)
RANGE_L  = (0, 1000)     # 亮度      UI 滑块 [0, 1000]  (样本 10/11=1000)
RANGE_LB = (0, 200)      # 线亮度    UI 滑块 [0, 200]   (样本 11=200)
RANGE_C  = (0, 256)      # 对比度    UI 滑块 [0, 256]   (样本 11=256)
RANGE_MATRIX_ELEM = (0, 512)  # 矩阵分量 UI 滑块 [0, 512] (样本 2~11 大量命中 512)

# "无调整" 默认值 (最常见于每个文件第一区域)
DEFAULT_H  = 0
DEFAULT_S  = 500
DEFAULT_L  = 500
DEFAULT_LB = 100
DEFAULT_C  = 128
DEFAULT_MATRIX = [[256, 0, 0], [0, 256, 0], [0, 0, 256]]

# "完全归零" 状态 (样本 #12), 某些旧工具把它当作 "禁用该区域" 语义
ZERO_REGION_MATRIX = [[0, 0, 0], [0, 0, 0], [0, 0, 0]]
ZERO_REGION_HSL    = (0, 0, 0, 0, 0)


# ────────────────────────────────────────────────────────────
# 数据类
# ────────────────────────────────────────────────────────────
@dataclass
class PPRegion:
    """一个调色区域 (对应 UI 里 '区域1' ~ '区域7' 其中一块)."""
    index: int
    matrix: List[List[int]] = field(default_factory=lambda: [
        [256, 0, 0], [0, 256, 0], [0, 0, 256]
    ])
    # HSL 五元组 (原始整数, 不做缩放)
    h: int = 0
    s: int = 500
    l: int = 500
    line_bright: int = 100
    contrast: int = 128

    # -------- 语义化访问器 --------
    @property
    def matrix_float(self) -> List[List[float]]:
        return [[v / 256.0 for v in row] for row in self.matrix]

    @property
    def hue_deg(self) -> int:
        return self.h % 360

    @property
    def saturation(self) -> float:
        return self.s / 500.0

    @property
    def lightness(self) -> float:
        return self.l / 500.0

    @property
    def line_bright_pct(self) -> float:
        return self.line_bright / 100.0

    @property
    def contrast_factor(self) -> float:
        return self.contrast / 128.0

    def is_identity(self) -> bool:
        """是否为'不做任何调色'的恒等区域 (常见于区域0)."""
        return (self.matrix == [[256, 0, 0], [0, 256, 0], [0, 0, 256]]
                and self.h == 0 and self.s == 500 and self.l == 500
                and self.line_bright == 100 and self.contrast == 128)

    def clamp_to_valid(self) -> None:
        """就地把所有字段裁剪到 RANGE_* 范围, 保证写出的 .pp 文件合法."""
        def _c(v, lo, hi):
            return max(lo, min(hi, int(v)))
        self.h           = _c(self.h,           *RANGE_H)
        self.s           = _c(self.s,           *RANGE_S)
        self.l           = _c(self.l,           *RANGE_L)
        self.line_bright = _c(self.line_bright, *RANGE_LB)
        self.contrast    = _c(self.contrast,    *RANGE_C)
        for r in range(3):
            for c in range(3):
                self.matrix[r][c] = _c(self.matrix[r][c], *RANGE_MATRIX_ELEM)


@dataclass
class PPFile:
    """整个 .pp 文件."""
    magic: int = 1001
    ui_index: int = 0
    alpha: int = 255
    regions: List[PPRegion] = field(default_factory=list)

    @property
    def region_count(self) -> int:
        return len(self.regions)

    def region(self, i: int) -> PPRegion:
        return self.regions[i]


# ────────────────────────────────────────────────────────────
# 读取
# ────────────────────────────────────────────────────────────
def read_pp(path: Union[str, os.PathLike]) -> PPFile:
    """解析 .pp 文件, 返回 PPFile 结构体."""
    with open(path, "r", encoding="ascii") as f:
        tokens_per_line: List[List[str]] = []
        for raw in f:
            parts = raw.strip().split()
            if parts:
                tokens_per_line.append(parts)

    if len(tokens_per_line) < 2:
        raise ValueError(f".pp 文件至少要有 2 行: {path}")

    # 行1
    header = list(map(int, tokens_per_line[0]))
    if len(header) < 3:
        raise ValueError(f"行1 头部需 3 字段, 实际: {header}")
    magic, ui_index, alpha = header[0], header[1], header[2]

    # 行2
    region_count = int(tokens_per_line[1][0])
    if region_count < 0:
        raise ValueError(f"区域数不能为负: {region_count}")

    # 每区 4 行
    expected = 2 + region_count * 4
    if len(tokens_per_line) < expected:
        raise ValueError(
            f"按区域数={region_count} 应该有 {expected} 行, 实际 {len(tokens_per_line)} 行"
        )

    regions: List[PPRegion] = []
    cursor = 2
    for i in range(region_count):
        try:
            row1 = [int(x) for x in tokens_per_line[cursor + 0]]
            row2 = [int(x) for x in tokens_per_line[cursor + 1]]
            row3 = [int(x) for x in tokens_per_line[cursor + 2]]
            hsl  = [int(x) for x in tokens_per_line[cursor + 3]]
        except ValueError as e:
            raise ValueError(f"区域 {i} 解析失败: {e}") from e

        for name, row in (("row1", row1), ("row2", row2), ("row3", row3)):
            if len(row) < 3:
                raise ValueError(f"区域 {i} {name} 需 3 字段, 实际: {row}")
        if len(hsl) < 5:
            raise ValueError(f"区域 {i} HSL 需 5 字段, 实际: {hsl}")

        regions.append(PPRegion(
            index=i,
            matrix=[row1[:3], row2[:3], row3[:3]],
            h=hsl[0], s=hsl[1], l=hsl[2],
            line_bright=hsl[3], contrast=hsl[4],
        ))
        cursor += 4

    return PPFile(magic=magic, ui_index=ui_index, alpha=alpha, regions=regions)


# ────────────────────────────────────────────────────────────
# 写入
# ────────────────────────────────────────────────────────────
def _format_line(values: List[int], strict: bool) -> str:
    """
    strict=True: 保留原工具的 "每字段后加空格 + '\n'" 约定
                 (与我们分析过的样本完全一致)
    strict=False: 紧凑格式 "a b c\n"
    """
    if strict:
        # "1001 0 255 \n" 格式
        return " ".join(str(v) for v in values) + " \n"
    return " ".join(str(v) for v in values) + "\n"


def write_pp(path: Union[str, os.PathLike], pp: PPFile, *, strict: bool = True) -> None:
    """把 PPFile 写回磁盘. strict=True 严格对齐原工具格式."""
    with open(path, "w", encoding="ascii", newline="") as f:
        f.write(_format_line([pp.magic, pp.ui_index, pp.alpha], strict))
        f.write(_format_line([pp.region_count], strict))
        for r in pp.regions:
            f.write(_format_line(r.matrix[0], strict))
            f.write(_format_line(r.matrix[1], strict))
            f.write(_format_line(r.matrix[2], strict))
            f.write(_format_line([r.h, r.s, r.l, r.line_bright, r.contrast], strict))


# ────────────────────────────────────────────────────────────
# 高层便利函数 (供 UI / 外部批处理使用)
# ────────────────────────────────────────────────────────────
def new_identity_file(region_count: int = 1) -> PPFile:
    """创建一个 N 区全恒等的空白 .pp (便于 '新建配色方案' 按钮)."""
    return PPFile(regions=[PPRegion(index=i) for i in range(region_count)])


def validate(pp: PPFile) -> List[str]:
    """
    校验 PPFile 各字段是否落在合法取值范围内, 返回问题列表.
    返回空列表表示完全合法.
    """
    issues: List[str] = []
    if pp.magic != 1001:
        issues.append(f"magic={pp.magic} 非标准值 (应为 1001)")
    if pp.region_count < 1 or pp.region_count > 16:
        issues.append(f"region_count={pp.region_count} 超出合理范围 (1~16)")
    for i, r in enumerate(pp.regions):
        if not (RANGE_H[0] <= r.h <= RANGE_H[1]):
            issues.append(f"区域 #{i}: h={r.h} 越界 {RANGE_H}")
        if not (RANGE_S[0] <= r.s <= RANGE_S[1]):
            issues.append(f"区域 #{i}: s={r.s} 越界 {RANGE_S}")
        if not (RANGE_L[0] <= r.l <= RANGE_L[1]):
            issues.append(f"区域 #{i}: l={r.l} 越界 {RANGE_L}")
        if not (RANGE_LB[0] <= r.line_bright <= RANGE_LB[1]):
            issues.append(f"区域 #{i}: line_bright={r.line_bright} 越界 {RANGE_LB}")
        if not (RANGE_C[0] <= r.contrast <= RANGE_C[1]):
            issues.append(f"区域 #{i}: contrast={r.contrast} 越界 {RANGE_C}")
        for rr in range(3):
            for cc in range(3):
                v = r.matrix[rr][cc]
                if not (RANGE_MATRIX_ELEM[0] <= v <= RANGE_MATRIX_ELEM[1]):
                    issues.append(
                        f"区域 #{i}: matrix[{rr}][{cc}]={v} 越界 {RANGE_MATRIX_ELEM}")
    return issues


def roundtrip_check(path: str) -> Tuple[bool, str]:
    """
    读 → 写 → 再读, 判断结构级是否完全还原
    (返回 True 表示无损, 同时尝试字节级对比, 若严格一致则给出 'byte-exact' 提示)
    """
    import io
    pp1 = read_pp(path)

    # 写到内存
    buf = io.StringIO()
    buf.write(_format_line([pp1.magic, pp1.ui_index, pp1.alpha], strict=True))
    buf.write(_format_line([pp1.region_count], strict=True))
    for r in pp1.regions:
        buf.write(_format_line(r.matrix[0], strict=True))
        buf.write(_format_line(r.matrix[1], strict=True))
        buf.write(_format_line(r.matrix[2], strict=True))
        buf.write(_format_line([r.h, r.s, r.l, r.line_bright, r.contrast], strict=True))
    written = buf.getvalue().encode("ascii")

    with open(path, "rb") as f:
        original = f.read()

    # 结构级再解析一次
    tmp_path = path + ".rtcheck.tmp"
    with open(tmp_path, "wb") as f:
        f.write(written)
    try:
        pp2 = read_pp(tmp_path)
    finally:
        try:
            os.remove(tmp_path)
        except OSError:
            pass

    structure_equal = (
        pp1.magic == pp2.magic and pp1.ui_index == pp2.ui_index
        and pp1.alpha == pp2.alpha
        and pp1.region_count == pp2.region_count
        and all(
            a.matrix == b.matrix and (a.h, a.s, a.l, a.line_bright, a.contrast) ==
            (b.h, b.s, b.l, b.line_bright, b.contrast)
            for a, b in zip(pp1.regions, pp2.regions)
        )
    )
    if not structure_equal:
        return False, "structure diff after roundtrip"

    if written == original:
        return True, "byte-exact"
    # 允许仅行尾差异
    norm_a = b"\n".join(line.rstrip() for line in written.splitlines())
    norm_b = b"\n".join(line.rstrip() for line in original.splitlines())
    if norm_a == norm_b:
        return True, "structure-equal (仅行尾空白差异)"
    return True, "structure-equal (字节级有差异, 但所有字段一致)"


# ────────────────────────────────────────────────────────────
# 自测入口
#   python -m modules.pp_format
# ────────────────────────────────────────────────────────────
if __name__ == "__main__":
    import sys

    SAMPLES = [
        r"D:\DH2client\npc\res_art_png\res2d\shape\char\7484\addon\01\00.pp",
        r"D:\DH2client\npc\res_art_png\res2d\shape\8326\fengxiong\00\00.pp",
    ]

    any_fail = False
    for p in SAMPLES:
        print(f"\n=== {p}")
        if not os.path.isfile(p):
            print("  [SKIP] 文件不存在")
            continue
        try:
            pp = read_pp(p)
        except Exception as e:
            any_fail = True
            print(f"  [FAIL] 读取失败: {e}")
            continue

        print(f"  magic={pp.magic}  ui_index={pp.ui_index}  alpha={pp.alpha}  regions={pp.region_count}")
        for r in pp.regions:
            tag = " (identity)" if r.is_identity() else ""
            print(f"    region[{r.index}]: H={r.hue_deg:>3}°  S={r.saturation:.2f}  L={r.lightness:.2f}"
                  f"  线亮度={int(r.line_bright_pct*100):>3}%  对比={r.contrast_factor:.2f}x"
                  f"  M={r.matrix}{tag}")

        ok, detail = roundtrip_check(p)
        flag = "OK " if ok else "FAIL"
        print(f"  [{flag}] roundtrip: {detail}")
        if not ok:
            any_fail = True

    sys.exit(1 if any_fail else 0)