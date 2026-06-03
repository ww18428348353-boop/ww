"""
mcolorkey.py  ─  纯 Python 版 "GetMColorKey"

等价于桌面那个 2.5MB 的 GetMColorKey.exe, 作用是:
    扫描一张 TCP 贴图, 返回一个 16 位的复合色编号 (MColorKey),
    告诉 "大话造型区域变色配色工具" 这张图实际启用了哪些 '区域' (0..15).

核心约定 (与原工具一致):
    像素.R  = (region_idx << 4) | palette_idx     ← 高 4 位=区域, 低 4 位=调色索引
    若像素.A == 0                                  → 视为透明, 不参与
    key = bit-OR over { 1 << region }             → 最多 16 位, 通常 <= 0xFFFF

常见输入:
    - .tcp 文件  (大话老格式, 头 4 字节为宽/高 little-endian + 每像素 R/G/B/A)
      * 不同版本头部略有差异, 下面的 read_tcp() 做了 "宽松探测":
        先按 8 字节头 (w, h little-endian 32bit) 尝试, 若像素数对得上就采纳;
        否则按 16 字节头再试.
    - .tga 文件  (直接调用 Pillow, 取 RGBA)
    - .png/.bmp  (同 Pillow)

对外 API:
    compute_mcolorkey(pixels_rgba: Iterable[Tuple[R,G,B,A]]) -> int
    compute_mcolorkey_from_image(path: str) -> int
    compute_mcolorkey_from_tcp(path: str) -> int
    enabled_regions(key: int) -> List[int]         # 0x8  -> [3]
    format_key(key: int) -> str                    # "0x08 / 8"

自测:
    python -m modules.mcolorkey <图片或tcp路径>
"""
from __future__ import annotations

import os
import struct
import sys
from typing import Iterable, List, Tuple


# ────────────────────────────────────────────────────────────
# 核心位运算
# ────────────────────────────────────────────────────────────
def compute_mcolorkey(pixels_rgba: Iterable[Tuple[int, int, int, int]]) -> int:
    """
    给定一组 (R,G,B,A) 像素, 返回 MColorKey (0~0xFFFF 的位掩码).
    alpha==0 的像素不计.
    """
    mask = 0
    for r, _g, _b, a in pixels_rgba:
        if a == 0:
            continue
        region = (r >> 4) & 0x0F
        mask |= (1 << region)
    return mask


def compute_mcolorkey_with_diagnostic(
    pixels_rgba: Iterable[Tuple[int, int, int, int]]
):
    """
    增强版: 同时收集 R 通道的 16 桶直方图, 用于判断传入图是否为
    '区域索引图' (少数桶非 0) 还是 '已渲染成品图' (16 桶都非 0).

    返回 (key:int, diag:dict).
        diag = {
            'r_hist': List[16]     # 高 4 位直方图
            'opaque_pixels': int   # 非透明像素数
            'nonzero_bins': int    # 非 0 桶数 (1~16)
            'looks_like_index': bool  # True = 像索引图
        }
    """
    mask = 0
    hist = [0] * 16
    opaque = 0
    for r, _g, _b, a in pixels_rgba:
        if a == 0:
            continue
        region = (r >> 4) & 0x0F
        hist[region] += 1
        mask |= (1 << region)
        opaque += 1

    nonzero = sum(1 for v in hist if v > 0)
    looks_like_index = (nonzero <= 8) and (opaque > 0)
    return mask, {
        "r_hist": hist,
        "opaque_pixels": opaque,
        "nonzero_bins": nonzero,
        "looks_like_index": looks_like_index,
    }


def enabled_regions(key: int) -> List[int]:
    """把 key 展开成启用的区域索引列表.  0x8 -> [3], 0x7 -> [0,1,2]"""
    return [i for i in range(16) if (key >> i) & 1]


def format_key(key: int) -> str:
    """统一的展示格式, 与原工具 '0x8 / 8' 风格一致."""
    return f"0x{key:X} / {key}"


# ────────────────────────────────────────────────────────────
# 从通用图片读取 (PNG/BMP/TGA 任选)
# ────────────────────────────────────────────────────────────
def compute_mcolorkey_from_image(path: str) -> int:
    """用 Pillow 读图, 转 RGBA, 计算 MColorKey."""
    from PIL import Image  # type: ignore
    with Image.open(path) as im:
        im = im.convert("RGBA")
        data = im.tobytes()              # len = w*h*4
    # 用生成器按 4 字节切, 避免构建完整 list
    def iter_px():
        for i in range(0, len(data), 4):
            yield data[i], data[i + 1], data[i + 2], data[i + 3]
    return compute_mcolorkey(iter_px())


# ────────────────────────────────────────────────────────────
# 从 .tcp 读取 (大话老格式)
# ────────────────────────────────────────────────────────────
def _try_decode_tcp(buf: bytes, header_size: int) -> Tuple[int, int, bytes] | None:
    """
    尝试按指定头长度解析 .tcp:
    头前 8 字节认为是 uint32 width, uint32 height (little-endian),
    剩余部分应为 width*height*4 字节的 RGBA / BGRA 像素.
    能对上则返回 (w, h, pixel_bytes), 否则返回 None.
    """
    if len(buf) <= header_size + 16:
        return None
    try:
        w, h = struct.unpack_from("<II", buf, 0)
    except struct.error:
        return None
    if w <= 0 or h <= 0 or w > 32768 or h > 32768:
        return None
    expected = w * h * 4
    payload = buf[header_size:]
    if len(payload) == expected:
        return w, h, payload
    # 有些版本多了 CRC/填充, 允许尾部 <= 32 字节冗余
    if 0 <= len(payload) - expected <= 32:
        return w, h, payload[:expected]
    return None


def read_tcp_rgba(path: str) -> Tuple[int, int, bytes]:
    """
    读一个 .tcp 文件, 返回 (width, height, rgba_bytes).
    自动探测 BGRA/RGBA:  若 R 通道 (index 0) 出现 '高位 region' 值较合理, 则认为是 RGBA;
    否则尝试 swap B<->R.
    """
    with open(path, "rb") as f:
        buf = f.read()

    # 试不同头长度
    for hs in (8, 12, 16, 20, 24, 32, 48, 64):
        r = _try_decode_tcp(buf, hs)
        if r is not None:
            w, h, px = r
            # 启发: 统计 (r>>4) 的出现种数; RGBA 排列下通常 region ∈ [0..15], 种数少
            def bit_spread(offset: int) -> int:
                s = set()
                # 每 4 字节采样一次, 最多扫 10000 像素够判断
                step = max(4, (len(px) // 4) // 10000 * 4 or 4)
                for i in range(0, len(px), step):
                    s.add((px[i + offset] >> 4) & 0xF)
                return len(s)
            if bit_spread(0) <= bit_spread(2):
                return w, h, px             # 已是 RGBA
            # 否则 BGRA -> 翻到 RGBA
            arr = bytearray(px)
            for i in range(0, len(arr), 4):
                arr[i], arr[i + 2] = arr[i + 2], arr[i]
            return w, h, bytes(arr)

    raise ValueError(f"无法识别的 .tcp 文件格式: {path} (size={len(buf)})")


def compute_mcolorkey_from_tcp(path: str) -> int:
    _w, _h, px = read_tcp_rgba(path)
    def iter_px():
        for i in range(0, len(px), 4):
            yield px[i], px[i + 1], px[i + 2], px[i + 3]
    return compute_mcolorkey(iter_px())


# ────────────────────────────────────────────────────────────
# 统一入口: 自动按扩展名分发
# ────────────────────────────────────────────────────────────
def get_mcolorkey(path: str) -> int:
    ext = os.path.splitext(path)[1].lower()
    if ext == ".tcp":
        return compute_mcolorkey_from_tcp(path)
    if ext in {".tga", ".png", ".bmp", ".jpg", ".jpeg", ".webp"}:
        return compute_mcolorkey_from_image(path)
    # 未知扩展: 先尝试 Pillow
    try:
        return compute_mcolorkey_from_image(path)
    except Exception:
        return compute_mcolorkey_from_tcp(path)


def get_mcolorkey_report(path: str):
    """
    不只返回 key, 还返回诊断信息 + 人类可读的建议.
    返回 (key:int, human_report:str).
    """
    ext = os.path.splitext(path)[1].lower()
    if ext == ".tcp":
        _w, _h, px = read_tcp_rgba(path)
        def iter_px():
            for i in range(0, len(px), 4):
                yield px[i], px[i + 1], px[i + 2], px[i + 3]
    else:
        from PIL import Image  # type: ignore
        with Image.open(path) as im:
            data = im.convert("RGBA").tobytes()
        def iter_px():
            for i in range(0, len(data), 4):
                yield data[i], data[i + 1], data[i + 2], data[i + 3]

    key, diag = compute_mcolorkey_with_diagnostic(iter_px())
    lines = [
        f"MColorKey : {format_key(key)}",
        f"启用区域  : {enabled_regions(key)}",
        f"非透明像素: {diag['opaque_pixels']}",
        f"R 高4位 非零桶: {diag['nonzero_bins']}/16",
    ]
    if not diag["looks_like_index"]:
        lines.append(
            "⚠ 此文件像是'已渲染成品图'(R 通道 16 个桶都非零). "
            "MColorKey 对它没有语义 — 请找原始'TCP 索引贴图'(通常 .tcp 或"
            "'未经配色'的那张底图)再执行."
        )
    return key, "\n".join(lines)


# ────────────────────────────────────────────────────────────
# 自测入口
# ────────────────────────────────────────────────────────────
if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("用法: python -m modules.mcolorkey <图片/tcp 路径>")
        print("示例: python -m modules.mcolorkey sample.tga")
        sys.exit(2)

    for p in sys.argv[1:]:
        if not os.path.isfile(p):
            print(f"[SKIP] 不存在: {p}")
            continue
        try:
            _key, report = get_mcolorkey_report(p)
        except Exception as e:
            print(f"[FAIL] {p} :: {e}")
            continue
        print(f"[OK] {p}")
        for ln in report.splitlines():
            print(f"     {ln}")