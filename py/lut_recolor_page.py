"""
LUT变色 模块 - 召唤兽/特效 TGA 序列帧 实时预览 + 色彩调整 + 批量导出

功能:
  1. 导入目录结构 (body / 00 / 01 三层), 每层包含 N 个动作目录
  2. 动作切换 + 方向切换 (2/4/8方向)
  3. 层级叠加合成预览 (body <- 00 <- 01 从下往上)
  4. 循环播放预览 + 可自定义背景 (纯色/图片)
  5. 每个层级独立开启/关闭色彩调整, 独立调参
     - HSB (色相/饱和度/明度)
     - 曲线
     - 色阶
     - 色彩平衡 (高光/中间调/阴影)
     - LUT 文件导入 (.cube 1D/3D)
  6. 实时预览变色效果
  7. 一键导出所有变色后的 TGA 到新目录
"""

import os
import re
import shutil
from pathlib import Path

from PyQt5.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
    QLineEdit, QGroupBox, QFileDialog, QMessageBox,
    QTabWidget, QComboBox, QPlainTextEdit, QTableWidget,
    QTableWidgetItem, QHeaderView, QAbstractItemView,
    QGridLayout, QProgressBar, QApplication, QSlider,
    QSpinBox, QDoubleSpinBox, QCheckBox, QColorDialog,
    QListWidget, QListWidgetItem, QScrollArea, QFrame,
    QSizePolicy, QSplitter, QButtonGroup, QRadioButton,
    QStackedWidget
)
from PyQt5.QtGui import (
    QFont, QColor, QPainter, QPen, QBrush, QImage,
    QPixmap, QLinearGradient, QPainterPath, QPolygon
)
from PyQt5.QtCore import (
    Qt, QTimer, QSize, QPoint, QRect, QPointF, QRectF,
    pyqtSignal, QSettings
)

import json
from modules.path_widgets import PathLineEdit, LayoutMixin

# numpy 用于向量化色彩处理 (50~100x 提速, 无 numpy 时自动回退纯 Python)
try:
    import numpy as _np
    _HAS_NUMPY = True
except ImportError:
    _np = None
    _HAS_NUMPY = False


# ============================================================================
# 动作方向规范
# ============================================================================

# 根据特效输出规范
ACTION_DIRECTIONS = {
    'guard':   (2, 4),     # 2方向或4方向
    'stand':   (2, 4),
    'stand2':  (4,),
    'walk':    (4,),
    'run':     (4,),
    'rush':    (4,),
    'attack':  (4,),
    'attack2': (4,),
    'attack3': (4,),
    'attack4': (4,),
    'defend':  (2,),
    'magic':   (2,),
    'die':     (2,),
    'hit':     (2,),
    'addon':   (8, 4, 2),  # 附加特效，任意方向
}

# 默认层级顺序 (从下到上)
DEFAULT_LAYER_ORDER = ['body', '00', '01']


# ============================================================================
# 图像处理工具 - 纯 Python 实现 (不依赖 numpy/PIL 之外)
# ============================================================================

def load_tga(path):
    """加载 TGA 为 QImage (ARGB32)。
    优先 Qt plugin, 失败时 fallback 到自行解析 RLE/uncompressed 32bpp TGA。
    兼容中文路径与游戏引擎 RLE-TGA。
    """
    # 1) 优先 Qt (中文路径要走 bytes)
    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError:
        return None
    img = QImage()
    img.loadFromData(data)
    if not img.isNull() and img.width() > 0:
        if img.format() != QImage.Format_ARGB32:
            img = img.convertToFormat(QImage.Format_ARGB32)
        return img
    # 2) Fallback: 纯 Python 解析 (type=2 or type=10 RLE, 32bpp)
    return _parse_tga_fallback(data)


def _parse_tga_fallback(data):
    """纯 Python TGA 解析: 支持 type=2/10, 32bpp BGRA → QImage.Format_ARGB32"""
    if not data or len(data) < 18:
        return None
    id_len = data[0]
    img_type = data[2]
    width = int.from_bytes(data[12:14], "little")
    height = int.from_bytes(data[14:16], "little")
    depth = data[16]
    descriptor = data[17]
    origin_top = bool(descriptor & 0x20)
    hdr = 18 + id_len
    if depth != 32 or width <= 0 or height <= 0:
        return None

    if img_type == 2:
        raw = data[hdr:hdr + width * height * 4]
    elif img_type == 10:
        raw = bytearray()
        i = hdr
        total = width * height
        cur = 0
        while cur < total and i < len(data):
            hb = data[i]; i += 1
            cnt = (hb & 0x7F) + 1
            if hb & 0x80:
                pixel = data[i:i + 4]; i += 4
                raw.extend(pixel * cnt)
            else:
                raw.extend(data[i:i + 4 * cnt]); i += 4 * cnt
            cur += cnt
        raw = bytes(raw)
    else:
        return None

    img = QImage(width, height, QImage.Format_ARGB32)
    row_bytes = width * 4
    for y in range(height):
        dst_y = (height - 1 - y) if not origin_top else y
        ptr = img.scanLine(dst_y)
        ptr.setsize(row_bytes)
        memoryview(ptr).cast('B')[:] = raw[y * row_bytes:(y + 1) * row_bytes]
    return img


def save_tga(img, path):
    """导出 QImage 为 uncompressed 32bpp TGA (保 Alpha)。
    Qt 自带的 TGA writer 在 PyInstaller 环境下不稳定, 所以自行实现。
    """
    if img is None or img.isNull():
        return False
    img = img.convertToFormat(QImage.Format_ARGB32)
    w, h = img.width(), img.height()
    header = bytearray(18)
    header[2] = 2                      # uncompressed true-color
    header[12] = w & 0xff; header[13] = (w >> 8) & 0xff
    header[14] = h & 0xff; header[15] = (h >> 8) & 0xff
    header[16] = 32                    # bpp
    header[17] = 0x08                  # 8-bit alpha, bottom-left origin
    body = bytearray()
    row_bytes = w * 4
    for y in range(h - 1, -1, -1):     # bottom-up
        ptr = img.scanLine(y)
        ptr.setsize(row_bytes)
        body.extend(bytes(ptr))
    try:
        with open(path, "wb") as f:
            f.write(bytes(header))
            f.write(bytes(body))
        return True
    except OSError:
        return False


def qimage_to_bytearray(img):
    """QImage -> bytearray (BGRA)"""
    if img is None or img.isNull():
        return None
    img = img.convertToFormat(QImage.Format_ARGB32)
    width = img.width()
    height = img.height()
    ptr = img.bits()
    ptr.setsize(height * width * 4)
    return bytearray(ptr)


def bytearray_to_qimage(buf, width, height):
    """bytearray (BGRA) -> QImage"""
    img = QImage(bytes(buf), width, height, QImage.Format_ARGB32)
    return img.copy()  # 必须 copy, 否则 buf 回收后 img 失效


# ============================================================================
# 曲线插值 (单调三次样条 / Catmull-Rom, 用于 PS 风格平滑曲线)
# ============================================================================

def curve_smooth_lut(points):
    """根据控制点生成 256 级平滑 LUT (单调三次样条, 不越界不回卷)。

    实现: 为每个控制点估计斜率 m_i, 对相邻两点做 Hermite 三次插值
          (Fritsch-Carlson), 保证单调性 → 曲线不会因插值出现"上翘后回卷"。
    points: [(x, y), ...] x,y ∈ [0, 255], 至少 2 个点
    """
    pts = sorted(points)
    if len(pts) < 2:
        return [max(0, min(255, int(y))) for _, y in [(0, 0)] * 256]
    xs = [float(p[0]) for p in pts]
    ys = [float(p[1]) for p in pts]
    n = len(pts)

    # 1) 计算每段斜率 d_k = (y_{k+1} - y_k) / (x_{k+1} - x_k)
    d = []
    for k in range(n - 1):
        dx = xs[k + 1] - xs[k]
        d.append((ys[k + 1] - ys[k]) / dx if dx != 0 else 0.0)

    # 2) 初步端点斜率
    m = [0.0] * n
    m[0] = d[0]
    m[-1] = d[-1]
    for k in range(1, n - 1):
        if d[k - 1] * d[k] <= 0:
            m[k] = 0.0  # 极值点切平, 防止超调
        else:
            m[k] = (d[k - 1] + d[k]) / 2.0

    # 3) Fritsch-Carlson 单调性修正
    for k in range(n - 1):
        if d[k] == 0:
            m[k] = 0.0
            m[k + 1] = 0.0
        else:
            a = m[k] / d[k]
            b = m[k + 1] / d[k]
            s = a * a + b * b
            if s > 9.0:
                t = 3.0 / (s ** 0.5)
                m[k] = t * a * d[k]
                m[k + 1] = t * b * d[k]

    # 4) 对 i=0..255 采样 (Hermite 三次)
    lut = [0] * 256
    seg = 0
    for i in range(256):
        x = float(i)
        # 找到 x 所在区间
        while seg < n - 2 and x > xs[seg + 1]:
            seg += 1
        if x <= xs[0]:
            lut[i] = max(0, min(255, int(round(ys[0]))))
            continue
        if x >= xs[-1]:
            lut[i] = max(0, min(255, int(round(ys[-1]))))
            continue
        x1 = xs[seg]; x2 = xs[seg + 1]
        y1 = ys[seg]; y2 = ys[seg + 1]
        m1 = m[seg];  m2 = m[seg + 1]
        h = x2 - x1
        t = (x - x1) / h if h != 0 else 0.0
        t2 = t * t
        t3 = t2 * t
        # Hermite basis
        h00 = 2 * t3 - 3 * t2 + 1
        h10 = t3 - 2 * t2 + t
        h01 = -2 * t3 + 3 * t2
        h11 = t3 - t2
        y = h00 * y1 + h10 * h * m1 + h01 * y2 + h11 * h * m2
        lut[i] = max(0, min(255, int(round(y))))
    return lut


# ============================================================================
# 色彩调整算法 (纯 Python, 逐像素)
# ============================================================================

class ColorAdjust:
    """色彩调整管道 (HSB + 曲线 + 色阶 + 色彩平衡 + LUT)"""

    def __init__(self):
        self.enabled = False
        # HSB
        self.hue = 0           # -180 ~ 180
        self.saturation = 0    # -100 ~ 100
        self.brightness = 0    # -100 ~ 100
        # 色阶 (input 0-255, output 0-255, gamma 0.1-9.99)
        self.levels_in_black = 0
        self.levels_in_white = 255
        self.levels_gamma = 1.0
        self.levels_out_black = 0
        self.levels_out_white = 255
        # 曲线 (控制点列表, 每个为 (x, y) 0-255)
        self.curve_points = [(0, 0), (255, 255)]
        # 色彩平衡 (R,G,B 三通道各 -100~100, 分别在阴影/中间调/高光)
        self.balance_shadows = [0, 0, 0]
        self.balance_mids    = [0, 0, 0]
        self.balance_highs   = [0, 0, 0]
        # LUT
        self.lut_1d = None  # list of 256 (R,G,B)  或 None
        self.lut_3d = None  # (size, flat_list)  或 None

    def clone(self):
        c = ColorAdjust()
        c.enabled = self.enabled
        c.hue = self.hue
        c.saturation = self.saturation
        c.brightness = self.brightness
        c.levels_in_black = self.levels_in_black
        c.levels_in_white = self.levels_in_white
        c.levels_gamma = self.levels_gamma
        c.levels_out_black = self.levels_out_black
        c.levels_out_white = self.levels_out_white
        c.curve_points = list(self.curve_points)
        c.balance_shadows = list(self.balance_shadows)
        c.balance_mids = list(self.balance_mids)
        c.balance_highs = list(self.balance_highs)
        c.lut_1d = self.lut_1d
        c.lut_3d = self.lut_3d
        return c

    def has_any_adjustment(self):
        """判断是否有任何实际的调整参数 (用于性能优化)"""
        if not self.enabled:
            return False
        if self.hue or self.saturation or self.brightness:
            return True
        if (self.levels_in_black != 0 or self.levels_in_white != 255
                or abs(self.levels_gamma - 1.0) > 0.001
                or self.levels_out_black != 0 or self.levels_out_white != 255):
            return True
        if self.curve_points != [(0, 0), (255, 255)]:
            return True
        if any(self.balance_shadows) or any(self.balance_mids) or any(self.balance_highs):
            return True
        if self.lut_1d or self.lut_3d:
            return True
        return False

    # ------------------------------------------------------------
    # 查找表生成 (快速应用)
    # ------------------------------------------------------------
    def build_lut_table(self):
        """构建 256 级 R/G/B LUT 表 用于快速逐像素处理"""
        if not self.has_any_adjustment():
            return None

        lut_r = list(range(256))
        lut_g = list(range(256))
        lut_b = list(range(256))

        # 1. 色阶
        if (self.levels_in_black != 0 or self.levels_in_white != 255
                or abs(self.levels_gamma - 1.0) > 0.001
                or self.levels_out_black != 0 or self.levels_out_white != 255):
            in_range = max(1, self.levels_in_white - self.levels_in_black)
            out_range = self.levels_out_white - self.levels_out_black
            gamma = max(0.01, self.levels_gamma)
            for i in range(256):
                v = (i - self.levels_in_black) / in_range
                v = max(0.0, min(1.0, v))
                v = v ** (1.0 / gamma)
                v = self.levels_out_black + v * out_range
                v = max(0, min(255, int(v)))
                lut_r[i] = v
                lut_g[i] = v
                lut_b[i] = v

        # 2. 曲线 (PS 风格圆滑样条, Fritsch-Carlson 单调三次 Hermite)
        if self.curve_points != [(0, 0), (255, 255)]:
            curve_lut = curve_smooth_lut(self.curve_points)
            lut_r = [curve_lut[v] for v in lut_r]
            lut_g = [curve_lut[v] for v in lut_g]
            lut_b = [curve_lut[v] for v in lut_b]

        # 3. 色彩平衡 (简化: 对阴影/中间调/高光各通道偏移)
        if any(self.balance_shadows) or any(self.balance_mids) or any(self.balance_highs):
            def balance_delta(i, ch):
                # 阴影权重 (i=0 时 1, i=128 时 0.5, i=255 时 0)
                s_w = max(0.0, 1.0 - (i / 128.0)) if i <= 128 else 0
                # 中间调 (i=128 时 1, 两端 0)
                m_w = 1.0 - abs(i - 128) / 128.0
                m_w = max(0.0, m_w)
                # 高光
                h_w = max(0.0, (i - 128) / 127.0) if i >= 128 else 0
                delta = (self.balance_shadows[ch] * s_w
                         + self.balance_mids[ch] * m_w
                         + self.balance_highs[ch] * h_w)
                return delta

            for i in range(256):
                lut_r[i] = max(0, min(255, int(lut_r[i] + balance_delta(i, 0))))
                lut_g[i] = max(0, min(255, int(lut_g[i] + balance_delta(i, 1))))
                lut_b[i] = max(0, min(255, int(lut_b[i] + balance_delta(i, 2))))

        # 4. 1D LUT (覆盖 R/G/B)
        if self.lut_1d:
            new_r = [self.lut_1d[v][0] for v in lut_r]
            new_g = [self.lut_1d[v][1] for v in lut_g]
            new_b = [self.lut_1d[v][2] for v in lut_b]
            lut_r, lut_g, lut_b = new_r, new_g, new_b

        return (lut_r, lut_g, lut_b)

    def apply_to_image(self, img):
        """对 QImage 应用色彩调整, 返回新 QImage。
        numpy 可用时走向量化路径 (~1ms/500x500), 否则回退纯 Python。
        """
        if not self.has_any_adjustment() or img is None or img.isNull():
            return img

        img = img.convertToFormat(QImage.Format_ARGB32)
        width = img.width()
        height = img.height()

        if _HAS_NUMPY:
            return self._apply_to_image_numpy(img, width, height)
        return self._apply_to_image_python(img, width, height)

    def _apply_to_image_numpy(self, img, width, height):
        """numpy 向量化处理。为避免 QImage.bits() 生命周期问题与视图别名,
        每一步都产生独立的 uint8 连续数组。"""
        # 通过 constBits → bytes → np.frombuffer 避免 bits() 可写指针风险
        try:
            cb = img.constBits()
            cb.setsize(height * width * 4)
            raw = bytes(cb)
        except Exception:
            # fallback: copy 整块数据
            raw = img.bits().asstring(height * width * 4)
        arr = _np.frombuffer(raw, dtype=_np.uint8).reshape((height, width, 4))
        # 分离通道 → 独立 copy 避免后续 in-place 副作用
        b0 = arr[:, :, 0].copy()
        g0 = arr[:, :, 1].copy()
        r0 = arr[:, :, 2].copy()
        a0 = arr[:, :, 3].copy()

        r, g, b = r0, g0, b0

        rgb_lut = self.build_lut_table()
        has_hsb = bool(self.hue or self.saturation or self.brightness)
        has_3d_lut = bool(self.lut_3d)

        # 1) R/G/B LUT (查表 O(1))
        if rgb_lut is not None:
            lut_r = _np.asarray(rgb_lut[0], dtype=_np.uint8)
            lut_g = _np.asarray(rgb_lut[1], dtype=_np.uint8)
            lut_b = _np.asarray(rgb_lut[2], dtype=_np.uint8)
            r = lut_r[r]
            g = lut_g[g]
            b = lut_b[b]

        # 2) HSB
        if has_hsb:
            r, g, b = self._apply_hsb_numpy(r, g, b)

        # 3) 3D LUT
        if has_3d_lut:
            r, g, b = self._apply_3d_lut_numpy(r, g, b)

        # 透明像素保持原样 (a==0 时直接用原 RGB)
        amask = (a0 == 0)
        if amask.any():
            r = _np.where(amask, r0, r)
            g = _np.where(amask, g0, g)
            b = _np.where(amask, b0, b)

        # 合回 BGRA 连续数组
        out = _np.empty((height, width, 4), dtype=_np.uint8)
        out[..., 0] = b
        out[..., 1] = g
        out[..., 2] = r
        out[..., 3] = a0
        # 转回 QImage (必须 .copy() 否则 bytes 会被回收)
        result = QImage(out.tobytes(), width, height, QImage.Format_ARGB32).copy()
        return result

    def _apply_hsb_numpy(self, r, g, b):
        """numpy 向量化 HSB 偏移"""
        rn = r.astype(_np.float32) / 255.0
        gn = g.astype(_np.float32) / 255.0
        bn = b.astype(_np.float32) / 255.0
        mx = _np.maximum(_np.maximum(rn, gn), bn)
        mn = _np.minimum(_np.minimum(rn, gn), bn)
        v = mx
        d = mx - mn
        s = _np.where(mx > 0, d / _np.where(mx > 0, mx, 1.0), 0.0)

        # Hue 计算
        h = _np.zeros_like(rn)
        nz = d > 0
        rc = (mx == rn) & nz
        gc = (mx == gn) & nz & ~rc
        bc = (mx == bn) & nz & ~rc & ~gc
        # mod 6
        h[rc] = ((gn[rc] - bn[rc]) / d[rc]) % 6
        h[gc] = (bn[gc] - rn[gc]) / d[gc] + 2
        h[bc] = (rn[bc] - gn[bc]) / d[bc] + 4
        h = h * 60.0

        # 应用偏移
        h = (h + self.hue) % 360
        s = _np.clip(s + self.saturation / 100.0, 0.0, 1.0)
        v = _np.clip(v + self.brightness / 100.0, 0.0, 1.0)

        # HSV → RGB (np.choose 表驱动, 一次性完成 6 扇区分配)
        c = v * s
        hh = h / 60.0
        x = c * (1 - _np.abs(hh % 2 - 1))
        m = v - c
        z = _np.zeros_like(c)
        seg = _np.clip(hh.astype(_np.int32), 0, 5)
        # R,G,B 在 6 扇区的选择表
        rr = _np.choose(seg, [c, x, z, z, x, c])
        gg = _np.choose(seg, [x, c, c, x, z, z])
        bb = _np.choose(seg, [z, z, x, c, c, x])
        rr = _np.clip((rr + m) * 255, 0, 255).astype(_np.uint8)
        gg = _np.clip((gg + m) * 255, 0, 255).astype(_np.uint8)
        bb = _np.clip((bb + m) * 255, 0, 255).astype(_np.uint8)
        return rr, gg, bb

    def _apply_3d_lut_numpy(self, r, g, b):
        """3D LUT 最近邻采样 (向量化)。self.lut_3d 是 dict 或 3d list。"""
        if self.lut_3d is None:
            return r, g, b
        # lut_3d 被 parse_cube_file 返回成 [N][N][N] = (fr, fg, fb) 的嵌套 list
        lut = self.lut_3d
        try:
            size = len(lut)
            arr_lut = _np.asarray(lut, dtype=_np.float32)  # shape (N,N,N,3), 值 0~1
            # 最近邻索引
            ir = _np.clip((r.astype(_np.float32) / 255.0 * (size - 1) + 0.5).astype(_np.int32), 0, size - 1)
            ig = _np.clip((g.astype(_np.float32) / 255.0 * (size - 1) + 0.5).astype(_np.int32), 0, size - 1)
            ib = _np.clip((b.astype(_np.float32) / 255.0 * (size - 1) + 0.5).astype(_np.int32), 0, size - 1)
            out = arr_lut[ir, ig, ib]  # shape (H,W,3) 值 0~1
            rr = _np.clip(out[..., 0] * 255, 0, 255).astype(_np.uint8)
            gg = _np.clip(out[..., 1] * 255, 0, 255).astype(_np.uint8)
            bb = _np.clip(out[..., 2] * 255, 0, 255).astype(_np.uint8)
            return rr, gg, bb
        except (TypeError, ValueError, IndexError):
            return r, g, b

    def _apply_to_image_python(self, img, width, height):
        """原纯 Python 版本 (fallback, numpy 不可用时使用)"""
        buf = qimage_to_bytearray(img)
        if buf is None:
            return img
        rgb_lut = self.build_lut_table()
        has_hsb = bool(self.hue or self.saturation or self.brightness)
        has_3d_lut = bool(self.lut_3d)
        n = width * height
        i = 0
        for _ in range(n):
            b = buf[i]
            g = buf[i + 1]
            r = buf[i + 2]
            a = buf[i + 3]
            if a == 0:
                i += 4
                continue
            if rgb_lut:
                r = rgb_lut[0][r]; g = rgb_lut[1][g]; b = rgb_lut[2][b]
            if has_hsb:
                r, g, b = self._apply_hsb(r, g, b)
            if has_3d_lut:
                r, g, b = self._apply_3d_lut(r, g, b)
            buf[i] = max(0, min(255, b))
            buf[i + 1] = max(0, min(255, g))
            buf[i + 2] = max(0, min(255, r))
            i += 4
        return bytearray_to_qimage(buf, width, height)

    def _apply_hsb(self, r, g, b):
        """RGB(0-255) -> 应用HSB -> RGB(0-255)"""
        rn, gn, bn = r / 255.0, g / 255.0, b / 255.0
        # RGB -> HSV
        mx = max(rn, gn, bn)
        mn = min(rn, gn, bn)
        v = mx
        d = mx - mn
        s = 0 if mx == 0 else d / mx

        if d == 0:
            h = 0
        elif mx == rn:
            h = 60 * (((gn - bn) / d) % 6)
        elif mx == gn:
            h = 60 * (((bn - rn) / d) + 2)
        else:
            h = 60 * (((rn - gn) / d) + 4)

        # 应用变化
        h = (h + self.hue) % 360
        s = max(0.0, min(1.0, s + self.saturation / 100.0))
        v = max(0.0, min(1.0, v + self.brightness / 100.0))

        # HSV -> RGB
        c = v * s
        x = c * (1 - abs((h / 60.0) % 2 - 1))
        m = v - c
        if h < 60:
            rp, gp, bp = c, x, 0
        elif h < 120:
            rp, gp, bp = x, c, 0
        elif h < 180:
            rp, gp, bp = 0, c, x
        elif h < 240:
            rp, gp, bp = 0, x, c
        elif h < 300:
            rp, gp, bp = x, 0, c
        else:
            rp, gp, bp = c, 0, x

        return int((rp + m) * 255), int((gp + m) * 255), int((bp + m) * 255)

    def _apply_3d_lut(self, r, g, b):
        """3D LUT 查询 (三线性插值简化: 最近邻)"""
        size, data = self.lut_3d
        ri = min(size - 1, int(r * (size - 1) / 255))
        gi = min(size - 1, int(g * (size - 1) / 255))
        bi = min(size - 1, int(b * (size - 1) / 255))
        idx = (bi * size * size + gi * size + ri) * 3
        return (int(data[idx] * 255),
                int(data[idx + 1] * 255),
                int(data[idx + 2] * 255))


def parse_cube_file(path):
    """解析 .cube LUT 文件, 返回 ('1d', [(r,g,b)*256]) 或 ('3d', (size, flat_list))"""
    try:
        with open(path, 'r', encoding='utf-8') as f:
            text = f.read()
    except Exception:
        return None

    size_1d = None
    size_3d = None
    data = []
    for line in text.split('\n'):
        line = line.strip()
        if not line or line.startswith('#'):
            continue
        if line.upper().startswith('TITLE'):
            continue
        if line.upper().startswith('LUT_1D_SIZE'):
            try:
                size_1d = int(line.split()[1])
            except Exception:
                pass
            continue
        if line.upper().startswith('LUT_3D_SIZE'):
            try:
                size_3d = int(line.split()[1])
            except Exception:
                pass
            continue
        if line.upper().startswith('DOMAIN'):
            continue
        # 数据行
        parts = line.split()
        if len(parts) >= 3:
            try:
                data.append((float(parts[0]), float(parts[1]), float(parts[2])))
            except ValueError:
                pass

    if size_3d:
        flat = []
        for rgb in data:
            flat.extend(rgb)
        return ('3d', (size_3d, flat))
    if size_1d:
        # 转换成 256 级 (最近邻)
        lut = []
        for i in range(256):
            idx = min(size_1d - 1, int(i * (size_1d - 1) / 255))
            r, g, b = data[idx]
            lut.append((int(r * 255), int(g * 255), int(b * 255)))
        return ('1d', lut)
    return None


# ============================================================================
# 曲线编辑器控件
# ============================================================================

class CurveEditor(QWidget):
    """曲线编辑器 (类似 Photoshop 曲线工具)"""

    curveChanged = pyqtSignal(list)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setMinimumSize(200, 200)
        self.setMaximumHeight(220)
        self._points = [(0, 0), (255, 255)]  # (input, output) 0-255
        self._dragging = -1
        self._grid = True

    def set_points(self, pts):
        self._points = sorted(pts)
        self.update()

    def get_points(self):
        return list(self._points)

    def reset(self):
        self._points = [(0, 0), (255, 255)]
        self.update()
        self.curveChanged.emit(self._points)

    def paintEvent(self, e):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        r = self.rect().adjusted(6, 6, -6, -6)
        # 背景
        p.fillRect(r, QColor("#fafafa"))
        p.setPen(QPen(QColor("#cccccc"), 1))
        p.drawRect(r)

        # 网格
        if self._grid:
            p.setPen(QPen(QColor("#e0e0e0"), 1, Qt.DotLine))
            for i in range(1, 4):
                y = r.top() + r.height() * i / 4
                p.drawLine(r.left(), int(y), r.right(), int(y))
                x = r.left() + r.width() * i / 4
                p.drawLine(int(x), r.top(), int(x), r.bottom())
            # 对角线
            p.setPen(QPen(QColor("#dddddd"), 1, Qt.DashLine))
            p.drawLine(r.bottomLeft(), r.topRight())

        # 曲线 — 用 Fritsch-Carlson 样条 LUT 高密度采样, 得到圆滑曲线
        pts = sorted(self._points)
        path = QPainterPath()
        try:
            smooth = curve_smooth_lut(pts)  # len 256, i -> output
        except Exception:
            smooth = [v for v in range(256)]
        # 对 0..255 每一个 x 采样一个点, 得到最平滑的曲线
        x0 = r.left()
        y0 = r.bottom()
        w = r.width()
        h = r.height()
        path.moveTo(x0, y0 - smooth[0] / 255.0 * h)
        for i in range(1, 256):
            x = x0 + i / 255.0 * w
            y = y0 - smooth[i] / 255.0 * h
            path.lineTo(x, y)
        p.setPen(QPen(QColor("#2563eb"), 2))
        p.setRenderHint(QPainter.Antialiasing, True)
        p.drawPath(path)

        # 控制点
        for inp, outp in pts:
            x = r.left() + inp / 255.0 * r.width()
            y = r.bottom() - outp / 255.0 * r.height()
            p.setBrush(QColor("#ffffff"))
            p.setPen(QPen(QColor("#2563eb"), 2))
            p.drawEllipse(QPointF(x, y), 5, 5)

    def _pos_to_value(self, pos):
        r = self.rect().adjusted(6, 6, -6, -6)
        x = max(0, min(r.width(), pos.x() - r.left()))
        y = max(0, min(r.height(), pos.y() - r.top()))
        inp = int(x / r.width() * 255)
        outp = int((1 - y / r.height()) * 255)
        return inp, outp

    def _find_point_at(self, pos):
        r = self.rect().adjusted(6, 6, -6, -6)
        for i, (inp, outp) in enumerate(self._points):
            x = r.left() + inp / 255.0 * r.width()
            y = r.bottom() - outp / 255.0 * r.height()
            if abs(pos.x() - x) < 8 and abs(pos.y() - y) < 8:
                return i
        return -1

    def mousePressEvent(self, e):
        if e.button() == Qt.LeftButton:
            idx = self._find_point_at(e.pos())
            if idx >= 0:
                self._dragging = idx
            else:
                # 添加新点
                inp, outp = self._pos_to_value(e.pos())
                self._points.append((inp, outp))
                self._points.sort()
                self._dragging = self._points.index((inp, outp))
                self.update()
                self.curveChanged.emit(list(self._points))
        elif e.button() == Qt.RightButton:
            idx = self._find_point_at(e.pos())
            # 端点不能删除
            if idx > 0 and idx < len(self._points) - 1:
                self._points.pop(idx)
                self.update()
                self.curveChanged.emit(list(self._points))

    def mouseMoveEvent(self, e):
        if self._dragging >= 0:
            inp, outp = self._pos_to_value(e.pos())
            # 端点锁定 x
            if self._dragging == 0:
                inp = 0
            elif self._dragging == len(self._points) - 1:
                inp = 255
            self._points[self._dragging] = (inp, outp)
            self._points.sort()
            self.update()
            self.curveChanged.emit(list(self._points))

    def mouseReleaseEvent(self, e):
        self._dragging = -1


# ============================================================================
# 预览视图 (支持背景色/背景图 + 当前帧叠加显示)
# ============================================================================

class PreviewCanvas(QWidget):
    """预览画布 - 显示合成后的当前帧"""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setMinimumSize(400, 400)
        self.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)
        self._bg_color = QColor(60, 60, 70)
        self._bg_image = None
        self._current_frame = None  # QImage

    def set_bg_color(self, color):
        self._bg_color = color
        self._bg_image = None
        self.update()

    def set_bg_image(self, path):
        # 支持中文路径 + 常见位图格式
        try:
            with open(path, "rb") as f:
                data = f.read()
        except OSError:
            return
        img = QImage()
        img.loadFromData(data)
        if img.isNull() and path.lower().endswith(".tga"):
            img = _parse_tga_fallback(data) or QImage()
        if not img.isNull():
            self._bg_image = img
            self.update()

    def set_frame(self, img):
        self._current_frame = img
        self.update()

    def paintEvent(self, e):
        p = QPainter(self)
        # 关键: 缩放走平滑插值, 否则 nearest 会让光晕薄边变色块
        p.setRenderHint(QPainter.SmoothPixmapTransform, True)
        p.setRenderHint(QPainter.Antialiasing, True)
        r = self.rect()
        # 背景
        if self._bg_image:
            p.drawImage(r, self._bg_image)
        else:
            p.fillRect(r, self._bg_color)

        # 帧
        if self._current_frame and not self._current_frame.isNull():
            img = self._current_frame
            # straight-alpha 的 ARGB32 在缩放时, 半透明像素的 RGB 不会被 alpha
            # 加权 → 高亮 RGB 透过低 alpha 仍参与插值, 出现"色块/硬边"。
            # 转 Premultiplied 后, Qt 内部按 src.rgb*src.a 参与混合, 与 AE 一致。
            if img.format() != QImage.Format_ARGB32_Premultiplied:
                img = img.convertToFormat(QImage.Format_ARGB32_Premultiplied)
            cw, ch = r.width(), r.height()
            iw, ih = img.width(), img.height()
            scale = min(cw / iw, ch / ih, 1.0)  # 不放大
            tw, th = int(iw * scale), int(ih * scale)
            tx = (cw - tw) // 2
            ty = (ch - th) // 2
            p.drawImage(QRect(tx, ty, tw, th), img)

        # 边框
        p.setPen(QPen(QColor(200, 200, 200), 1))
        p.drawRect(r.adjusted(0, 0, -1, -1))


# ============================================================================
# 层级数据结构
# ============================================================================

class LayerData:
    """单个层级(body/00/01)的数据和色彩调整参数"""

    def __init__(self, name, root_dir):
        self.name = name
        self.root_dir = root_dir     # 层级的根目录 (包含各动作目录)
        self.actions = {}            # {action_name: {direction_id: [frame_paths]}}
        self.adjust = ColorAdjust()
        self.visible = True
        self._scan()

    def _scan(self):
        """扫描根目录下的所有动作/方向/帧"""
        if not self.root_dir or not os.path.isdir(self.root_dir):
            return
        for item in os.listdir(self.root_dir):
            sub = os.path.join(self.root_dir, item)
            if not os.path.isdir(sub):
                continue
            # 动作目录
            dirs = {}  # direction_id -> list of (frame_idx, path)
            for fname in os.listdir(sub):
                if not fname.lower().endswith('.tga'):
                    continue
                m = re.match(r'^(\d)(\d{3})\.tga$', fname, re.IGNORECASE)
                if not m:
                    continue
                dir_id = int(m.group(1))
                frame_id = int(m.group(2))
                dirs.setdefault(dir_id, []).append(
                    (frame_id, os.path.join(sub, fname)))
            if dirs:
                # 按帧号排序
                sorted_dirs = {}
                for d, frames in dirs.items():
                    frames.sort(key=lambda x: x[0])
                    sorted_dirs[d] = [p for _, p in frames]
                self.actions[item] = sorted_dirs

    def get_frames(self, action, direction):
        return self.actions.get(action, {}).get(direction, [])


# ============================================================================
# LUT变色 主页面
# ============================================================================

class LutRecolorPage(QWidget):
    """LUT变色 模块主页面"""

    def __init__(self, parent=None):
        super().__init__(parent)
        self._source_root = ""
        self._layers = []            # [LayerData, ...] 从下到上
        self._current_action = None
        self._current_direction = 0
        self._current_num_dirs = 4   # 当前可用方向数
        self._current_frame_idx = 0
        self._total_frames = 1
        self._playing = True
        self._fps = 12
        self._bg_color = QColor(60, 60, 70)
        self._bg_image_path = ""
        self._frame_cache = {}        # (layer, action, dir, idx, adjust_key) -> QImage
        self._current_layer_idx = 0   # 当前编辑的层级索引
        self._init_ui()

        # 播放定时器
        self._play_timer = QTimer(self)
        self._play_timer.timeout.connect(self._on_tick)
        self._play_timer.start(int(1000 / self._fps))

        # 滑块节流定时器: 连续滑动只触发一次缓存失效 (默认 60ms 合并)
        self._adj_debounce = QTimer(self)
        self._adj_debounce.setSingleShot(True)
        self._adj_debounce.setInterval(60)
        self._adj_debounce.timeout.connect(self._flush_dirty_layers)
        self._dirty_layers = set()  # 待失效的 layer 索引

    # ================================================================
    # UI 构建
    # ================================================================
    _LAYOUT_KEY = "lut_recolor/ui_layout"

    def _init_ui(self):
        main_layout = QVBoxLayout(self)
        main_layout.setContentsMargins(20, 15, 20, 15)
        main_layout.setSpacing(12)

        # ===== 保存/重置布局按钮行 (参考 "相同贴图检查" 模块实现) =====
        btn_row = QHBoxLayout()
        btn_row.setSpacing(6)
        btn_row.addStretch()

        self._save_layout_btn = QPushButton("保存UI布局")
        self._save_layout_btn.setFixedWidth(100)
        self._save_layout_btn.setFont(QFont("Microsoft YaHei UI", 9))
        self._save_layout_btn.setToolTip("保存当前窗口尺寸、路径、分栏比例")
        self._save_layout_btn.setStyleSheet("""
            QPushButton { background-color: #8e44ad; color: white;
                          border: none; border-radius: 4px; padding: 6px; }
            QPushButton:hover { background-color: #7d3c98; }
        """)
        self._save_layout_btn.clicked.connect(self._save_ui_layout)
        btn_row.addWidget(self._save_layout_btn)

        self._reset_layout_btn = QPushButton("重置UI布局")
        self._reset_layout_btn.setFixedWidth(100)
        self._reset_layout_btn.setFont(QFont("Microsoft YaHei UI", 9))
        self._reset_layout_btn.setToolTip("恢复默认的窗口尺寸和分栏比例")
        self._reset_layout_btn.setStyleSheet("""
            QPushButton { background-color: #95a5a6; color: white;
                          border: none; border-radius: 4px; padding: 6px; }
            QPushButton:hover { background-color: #7f8c8d; }
        """)
        self._reset_layout_btn.clicked.connect(self._reset_ui_layout)
        btn_row.addWidget(self._reset_layout_btn)

        main_layout.addLayout(btn_row)

        # QSettings 公用命名空间 (和 tex_duplicate_page 保持一致以便统一管理)
        self._layout_settings = QSettings("ToolDevelopment", "DH2ResourceExport")

        # ===== 顶部: 路径设置 =====
        path_group = QGroupBox("路径设置")
        path_group.setFont(QFont("Microsoft YaHei UI", 10, QFont.Bold))
        path_layout = QVBoxLayout(path_group)
        path_layout.setContentsMargins(15, 20, 15, 15)
        path_layout.setSpacing(8)

        # 源目录
        src_row = QHBoxLayout()
        src_label = QLabel("源目录:")
        src_label.setFixedWidth(70)
        self._src_edit = PathLineEdit(mode="dir", title="选择源目录")
        self._src_edit.setPlaceholderText(
            "选择包含 body / 00 / 01 三层的根目录...")
        self._src_edit.pathChanged.connect(self._on_src_changed)
        src_browse = QPushButton("浏览...")
        src_browse.setFixedWidth(80)
        src_browse.clicked.connect(self._browse_src)
        load_btn = QPushButton("加载")
        load_btn.setFixedWidth(80)
        load_btn.clicked.connect(self._load_source)
        src_row.addWidget(src_label)
        src_row.addWidget(self._src_edit)
        src_row.addWidget(src_browse)
        src_row.addWidget(load_btn)
        path_layout.addLayout(src_row)

        # 输出目录
        out_row = QHBoxLayout()
        out_label = QLabel("输出目录:")
        out_label.setFixedWidth(70)
        self._out_edit = PathLineEdit(mode="dir", title="选择输出目录")
        self._out_edit.setPlaceholderText("变色后的 TGA 序列保存目录...")
        out_browse = QPushButton("浏览...")
        out_browse.setFixedWidth(80)
        out_browse.clicked.connect(self._browse_out)
        out_row.addWidget(out_label)
        out_row.addWidget(self._out_edit)
        out_row.addWidget(out_browse)
        out_row.addSpacing(86)
        path_layout.addLayout(out_row)

        main_layout.addWidget(path_group)

        # ===== 中部: 左(预览) + 右(控制面板) =====
        self._splitter = QSplitter(Qt.Horizontal)
        self._splitter.setHandleWidth(6)

        # 左: 预览
        left_wrap = QWidget()
        left_layout = QVBoxLayout(left_wrap)
        left_layout.setContentsMargins(0, 0, 0, 0)
        left_layout.setSpacing(8)

        preview_group = QGroupBox("预览 (实时循环播放)")
        preview_group.setFont(QFont("Microsoft YaHei UI", 10, QFont.Bold))
        preview_layout = QVBoxLayout(preview_group)
        preview_layout.setContentsMargins(10, 20, 10, 10)

        # 预览画布
        self._canvas = PreviewCanvas()
        preview_layout.addWidget(self._canvas, 1)

        # 动作/方向选择
        ctrl_row1 = QHBoxLayout()
        ctrl_row1.setSpacing(8)
        ctrl_row1.addWidget(QLabel("动作:"))
        self._action_combo = QComboBox()
        self._action_combo.setMinimumWidth(140)
        self._action_combo.currentTextChanged.connect(self._on_action_changed)
        ctrl_row1.addWidget(self._action_combo)

        ctrl_row1.addWidget(QLabel("方向数:"))
        self._dir_mode_combo = QComboBox()
        self._dir_mode_combo.addItems(["自动", "2方向", "4方向", "8方向"])
        self._dir_mode_combo.setFixedWidth(90)
        self._dir_mode_combo.currentIndexChanged.connect(self._on_dir_mode_changed)
        ctrl_row1.addWidget(self._dir_mode_combo)

        ctrl_row1.addStretch()
        preview_layout.addLayout(ctrl_row1)

        # 方向按钮
        ctrl_row2 = QHBoxLayout()
        ctrl_row2.setSpacing(4)
        ctrl_row2.addWidget(QLabel("方向:"))
        self._dir_btn_group = QButtonGroup(self)
        self._dir_btns = []
        for i in range(8):
            btn = QPushButton(str(i + 1))
            btn.setCheckable(True)
            btn.setFixedSize(30, 28)
            btn.setProperty("dir_idx", i)
            btn.clicked.connect(lambda _, idx=i: self._switch_direction(idx))
            self._dir_btn_group.addButton(btn, i)
            self._dir_btns.append(btn)
            ctrl_row2.addWidget(btn)
        ctrl_row2.addStretch()
        preview_layout.addLayout(ctrl_row2)

        # 播放控制
        ctrl_row3 = QHBoxLayout()
        self._play_btn = QPushButton("⏸ 暂停")
        self._play_btn.setFixedWidth(80)
        self._play_btn.clicked.connect(self._toggle_play)
        ctrl_row3.addWidget(self._play_btn)

        ctrl_row3.addWidget(QLabel("帧率:"))
        self._fps_spin = QSpinBox()
        self._fps_spin.setRange(1, 60)
        self._fps_spin.setValue(self._fps)
        self._fps_spin.setFixedWidth(60)
        self._fps_spin.valueChanged.connect(self._on_fps_changed)
        ctrl_row3.addWidget(self._fps_spin)

        ctrl_row3.addWidget(QLabel("当前帧:"))
        self._frame_label = QLabel("0/0")
        self._frame_label.setFixedWidth(60)
        ctrl_row3.addWidget(self._frame_label)

        ctrl_row3.addStretch()
        preview_layout.addLayout(ctrl_row3)

        # 背景控制
        ctrl_row4 = QHBoxLayout()
        ctrl_row4.addWidget(QLabel("背景:"))
        self._bg_color_btn = QPushButton("选择颜色")
        self._bg_color_btn.setFixedWidth(90)
        self._bg_color_btn.clicked.connect(self._pick_bg_color)
        ctrl_row4.addWidget(self._bg_color_btn)
        bg_img_btn = QPushButton("选择背景图")
        bg_img_btn.setFixedWidth(100)
        bg_img_btn.clicked.connect(self._pick_bg_image)
        ctrl_row4.addWidget(bg_img_btn)
        bg_clear_btn = QPushButton("清除背景图")
        bg_clear_btn.setFixedWidth(100)
        bg_clear_btn.clicked.connect(self._clear_bg_image)
        ctrl_row4.addWidget(bg_clear_btn)
        ctrl_row4.addStretch()
        preview_layout.addLayout(ctrl_row4)

        left_layout.addWidget(preview_group, 1)
        self._splitter.addWidget(left_wrap)

        # 右: 控制面板 (每层一个 Tab)
        right_wrap = QWidget()
        right_layout = QVBoxLayout(right_wrap)
        right_layout.setContentsMargins(0, 0, 0, 0)
        right_layout.setSpacing(8)

        # 层级 Tab
        self._layer_tabs = QTabWidget()
        self._layer_tabs.currentChanged.connect(self._on_layer_tab_changed)
        right_layout.addWidget(self._layer_tabs, 1)

        self._splitter.addWidget(right_wrap)
        self._splitter.setSizes([600, 500])

        main_layout.addWidget(self._splitter, 1)

        # ===== 底部: 进度条 + 导出 =====
        bottom = QHBoxLayout()
        self._progress = QProgressBar()
        self._progress.setFixedHeight(24)
        self._progress.setValue(0)
        bottom.addWidget(self._progress, 1)

        self._export_btn = QPushButton("  导出TGA序列  ")
        self._export_btn.setObjectName("exportBtn")
        self._export_btn.setFont(QFont("Microsoft YaHei UI", 11, QFont.Bold))
        self._export_btn.setFixedHeight(38)
        self._export_btn.setFixedWidth(160)
        self._export_btn.clicked.connect(self._do_export)
        bottom.addWidget(self._export_btn)

        main_layout.addLayout(bottom)

        # 默认 splitter 比例 (供重置使用)
        self._default_splitter_sizes = [600, 500]

        # 启动时自动恢复上次的布局
        self.restore_saved_window_geometry()

    def _create_adjust_panel(self, layer_idx):
        """为某个层级创建色彩调整面板"""
        panel = QWidget()
        outer = QVBoxLayout(panel)
        outer.setContentsMargins(0, 0, 0, 0)

        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.NoFrame)
        content = QWidget()
        layout = QVBoxLayout(content)
        layout.setContentsMargins(10, 10, 10, 10)
        layout.setSpacing(10)

        layer = self._layers[layer_idx]

        # 总开关
        enable_check = QCheckBox(f"启用色彩调整 (对 {layer.name} 层)")
        enable_check.setChecked(layer.adjust.enabled)
        enable_check.stateChanged.connect(
            lambda st, i=layer_idx: self._on_layer_enable(i, bool(st)))
        layout.addWidget(enable_check)

        # ==== HSB ====
        hsb_group = QGroupBox("HSB 换色 (色相/饱和度/明度)")
        hsb_group.setFont(QFont("Microsoft YaHei UI", 10, QFont.Bold))
        hsb_layout = QGridLayout(hsb_group)
        hsb_layout.setContentsMargins(12, 20, 12, 10)

        def add_slider(row, label, minv, maxv, val, cb):
            hsb_layout.addWidget(QLabel(label), row, 0)
            sl = QSlider(Qt.Horizontal)
            sl.setRange(minv, maxv)
            sl.setValue(val)
            sp = QSpinBox()
            sp.setRange(minv, maxv)
            sp.setValue(val)
            sp.setFixedWidth(60)
            sl.valueChanged.connect(sp.setValue)
            sp.valueChanged.connect(sl.setValue)
            sl.valueChanged.connect(cb)
            hsb_layout.addWidget(sl, row, 1)
            hsb_layout.addWidget(sp, row, 2)
            return sl

        add_slider(0, "色相:", -180, 180, layer.adjust.hue,
                   lambda v, i=layer_idx: self._on_adj(i, 'hue', v))
        add_slider(1, "饱和度:", -100, 100, layer.adjust.saturation,
                   lambda v, i=layer_idx: self._on_adj(i, 'saturation', v))
        add_slider(2, "明度:", -100, 100, layer.adjust.brightness,
                   lambda v, i=layer_idx: self._on_adj(i, 'brightness', v))

        layout.addWidget(hsb_group)

        # ==== 色阶 ====
        lv_group = QGroupBox("色阶")
        lv_group.setFont(QFont("Microsoft YaHei UI", 10, QFont.Bold))
        lv_layout = QGridLayout(lv_group)
        lv_layout.setContentsMargins(12, 20, 12, 10)

        lv_layout.addWidget(QLabel("输入黑:"), 0, 0)
        in_b = QSpinBox()
        in_b.setRange(0, 254)
        in_b.setValue(layer.adjust.levels_in_black)
        in_b.valueChanged.connect(
            lambda v, i=layer_idx: self._on_adj(i, 'levels_in_black', v))
        lv_layout.addWidget(in_b, 0, 1)

        lv_layout.addWidget(QLabel("输入白:"), 0, 2)
        in_w = QSpinBox()
        in_w.setRange(1, 255)
        in_w.setValue(layer.adjust.levels_in_white)
        in_w.valueChanged.connect(
            lambda v, i=layer_idx: self._on_adj(i, 'levels_in_white', v))
        lv_layout.addWidget(in_w, 0, 3)

        lv_layout.addWidget(QLabel("Gamma:"), 1, 0)
        gm = QDoubleSpinBox()
        gm.setRange(0.01, 9.99)
        gm.setSingleStep(0.05)
        gm.setDecimals(2)
        gm.setValue(layer.adjust.levels_gamma)
        gm.valueChanged.connect(
            lambda v, i=layer_idx: self._on_adj(i, 'levels_gamma', v))
        lv_layout.addWidget(gm, 1, 1)

        lv_layout.addWidget(QLabel("输出黑:"), 2, 0)
        out_b = QSpinBox()
        out_b.setRange(0, 254)
        out_b.setValue(layer.adjust.levels_out_black)
        out_b.valueChanged.connect(
            lambda v, i=layer_idx: self._on_adj(i, 'levels_out_black', v))
        lv_layout.addWidget(out_b, 2, 1)

        lv_layout.addWidget(QLabel("输出白:"), 2, 2)
        out_w = QSpinBox()
        out_w.setRange(1, 255)
        out_w.setValue(layer.adjust.levels_out_white)
        out_w.valueChanged.connect(
            lambda v, i=layer_idx: self._on_adj(i, 'levels_out_white', v))
        lv_layout.addWidget(out_w, 2, 3)

        layout.addWidget(lv_group)

        # ==== 曲线 ====
        curve_group = QGroupBox("曲线")
        curve_group.setFont(QFont("Microsoft YaHei UI", 10, QFont.Bold))
        curve_layout = QVBoxLayout(curve_group)
        curve_layout.setContentsMargins(12, 20, 12, 10)
        curve_tip = QLabel("左键添加/拖动控制点，右键删除")
        curve_tip.setStyleSheet("color: #888; font-size: 11px;")
        curve_layout.addWidget(curve_tip)
        curve_edit = CurveEditor()
        curve_edit.set_points(layer.adjust.curve_points)
        curve_edit.curveChanged.connect(
            lambda pts, i=layer_idx: self._on_adj(i, 'curve_points', pts))
        curve_layout.addWidget(curve_edit)
        curve_reset = QPushButton("重置曲线")
        curve_reset.setFixedWidth(90)
        curve_reset.clicked.connect(curve_edit.reset)
        curve_layout.addWidget(curve_reset)
        layout.addWidget(curve_group)

        # ==== 色彩平衡 ====
        bal_group = QGroupBox("色彩平衡 (高光 / 中间调 / 阴影)")
        bal_group.setFont(QFont("Microsoft YaHei UI", 10, QFont.Bold))
        bal_layout = QGridLayout(bal_group)
        bal_layout.setContentsMargins(12, 20, 12, 10)

        def add_bal_row(row, label, vals, key):
            bal_layout.addWidget(QLabel(label), row, 0)
            for ch_idx, ch_name in enumerate(['R', 'G', 'B']):
                bal_layout.addWidget(QLabel(ch_name), row, 1 + ch_idx * 2)
                sp = QSpinBox()
                sp.setRange(-100, 100)
                sp.setValue(vals[ch_idx])
                sp.setFixedWidth(60)
                sp.valueChanged.connect(
                    lambda v, i=layer_idx, k=key, c=ch_idx:
                    self._on_bal(i, k, c, v))
                bal_layout.addWidget(sp, row, 2 + ch_idx * 2)

        add_bal_row(0, "高光:", layer.adjust.balance_highs, 'balance_highs')
        add_bal_row(1, "中间调:", layer.adjust.balance_mids, 'balance_mids')
        add_bal_row(2, "阴影:", layer.adjust.balance_shadows, 'balance_shadows')

        layout.addWidget(bal_group)

        # ==== LUT ====
        lut_group = QGroupBox("LUT文件导入 (.cube)")
        lut_group.setFont(QFont("Microsoft YaHei UI", 10, QFont.Bold))
        lut_layout = QHBoxLayout(lut_group)
        lut_layout.setContentsMargins(12, 20, 12, 10)

        self_lut_label = QLabel("未加载")
        self_lut_label.setStyleSheet("color: #888;")
        lut_load_btn = QPushButton("加载LUT...")
        lut_load_btn.clicked.connect(
            lambda _, i=layer_idx, lbl=self_lut_label: self._load_lut(i, lbl))
        lut_clear_btn = QPushButton("清除")
        lut_clear_btn.clicked.connect(
            lambda _, i=layer_idx, lbl=self_lut_label: self._clear_lut(i, lbl))
        lut_layout.addWidget(lut_load_btn)
        lut_layout.addWidget(lut_clear_btn)
        lut_layout.addWidget(self_lut_label, 1)

        layout.addWidget(lut_group)

        # 重置所有
        reset_all_btn = QPushButton("重置当前层所有参数")
        reset_all_btn.setFixedHeight(32)
        reset_all_btn.clicked.connect(
            lambda _, i=layer_idx: self._reset_adjust(i))
        layout.addWidget(reset_all_btn)

        layout.addStretch()
        scroll.setWidget(content)
        outer.addWidget(scroll)
        return panel

    # ================================================================
    # 主题
    # ================================================================
    def apply_theme(self):
        try:
            from Tool_Development import ThemeManager
            self.setStyleSheet(ThemeManager.get_page_style())
        except ImportError:
            pass

    # ================================================================
    # 路径 / 加载
    # ================================================================
    def _on_src_changed(self, path):
        self._source_root = path

    def _browse_src(self):
        d = QFileDialog.getExistingDirectory(self, "选择源目录", self._source_root or "")
        if d:
            self._src_edit.setText(d)
            self._source_root = d

    def _browse_out(self):
        d = QFileDialog.getExistingDirectory(self, "选择输出目录")
        if d:
            self._out_edit.setText(d)

    def _load_source(self):
        if not self._source_root or not os.path.isdir(self._source_root):
            QMessageBox.warning(self, "提示", "请先选择有效的源目录！")
            return

        # 扫描三个层级
        self._layers = []
        for layer_name in DEFAULT_LAYER_ORDER:
            layer_dir = os.path.join(self._source_root, layer_name)
            if os.path.isdir(layer_dir):
                self._layers.append(LayerData(layer_name, layer_dir))

        if not self._layers:
            QMessageBox.warning(self, "提示",
                f"源目录下未找到任何层级目录 ({'/'.join(DEFAULT_LAYER_ORDER)})！")
            return

        # 刷新 UI
        self._frame_cache.clear()

        # 重建 Tab
        self._layer_tabs.clear()
        for i, layer in enumerate(self._layers):
            panel = self._create_adjust_panel(i)
            self._layer_tabs.addTab(panel, f"{layer.name} 层")

        # 刷新动作列表 (取所有层级的动作并集)
        all_actions = set()
        for l in self._layers:
            all_actions.update(l.actions.keys())
        self._action_combo.blockSignals(True)
        self._action_combo.clear()
        self._action_combo.addItems(sorted(all_actions))
        self._action_combo.blockSignals(False)

        if all_actions:
            self._current_action = sorted(all_actions)[0]
            self._action_combo.setCurrentText(self._current_action)
            self._refresh_direction()

    # ================================================================
    # 动作 / 方向
    # ================================================================
    def _on_action_changed(self, action):
        self._current_action = action
        self._current_frame_idx = 0
        self._refresh_direction()

    def _on_dir_mode_changed(self):
        self._refresh_direction()

    def _refresh_direction(self):
        """根据当前动作确定可用的方向数, 并更新按钮"""
        if not self._current_action or not self._layers:
            return

        # 收集所有层级中该动作的方向 ID
        available_dirs = set()
        for l in self._layers:
            available_dirs.update(l.actions.get(self._current_action, {}).keys())

        mode = self._dir_mode_combo.currentText()
        if mode == "2方向":
            num = 2
        elif mode == "4方向":
            num = 4
        elif mode == "8方向":
            num = 8
        else:  # 自动
            if any(d > 3 for d in available_dirs):
                num = 8
            elif any(d > 1 for d in available_dirs):
                num = 4
            else:
                num = 2

        self._current_num_dirs = num
        for i, btn in enumerate(self._dir_btns):
            btn.setVisible(i < num)
            btn.setEnabled(i in available_dirs)
            btn.setChecked(i == self._current_direction
                           and i in available_dirs)

        if self._current_direction >= num or self._current_direction not in available_dirs:
            # 选第一个可用方向
            avail_sorted = sorted(available_dirs)
            if avail_sorted:
                self._current_direction = avail_sorted[0]
                if self._current_direction < len(self._dir_btns):
                    self._dir_btns[self._current_direction].setChecked(True)

        self._refresh_total_frames()

    def _switch_direction(self, idx):
        self._current_direction = idx
        self._current_frame_idx = 0
        self._refresh_total_frames()

    def _refresh_total_frames(self):
        """计算当前动作/方向的最大帧数"""
        max_frames = 1
        for l in self._layers:
            frames = l.get_frames(self._current_action, self._current_direction)
            if len(frames) > max_frames:
                max_frames = len(frames)
        self._total_frames = max_frames
        self._current_frame_idx = 0

    # ================================================================
    # 播放控制
    # ================================================================
    def _on_tick(self):
        if not self._layers or not self._current_action:
            return
        if self._playing:
            self._current_frame_idx = (self._current_frame_idx + 1) % max(1, self._total_frames)
        self._frame_label.setText(f"{self._current_frame_idx + 1}/{self._total_frames}")
        self._render_current_frame()

    def _toggle_play(self):
        self._playing = not self._playing
        self._play_btn.setText("⏸ 暂停" if self._playing else "▶ 播放")

    def _on_fps_changed(self, v):
        self._fps = v
        self._play_timer.setInterval(int(1000 / max(1, v)))

    # ================================================================
    # 背景
    # ================================================================
    def _pick_bg_color(self):
        c = QColorDialog.getColor(self._bg_color, self, "选择背景颜色")
        if c.isValid():
            self._bg_color = c
            self._canvas.set_bg_color(c)

    def _pick_bg_image(self):
        path, _ = QFileDialog.getOpenFileName(
            self, "选择背景图片", "",
            "图片 (*.png *.jpg *.jpeg *.bmp *.tga)")
        if path:
            self._bg_image_path = path
            self._canvas.set_bg_image(path)

    def _clear_bg_image(self):
        self._bg_image_path = ""
        self._canvas.set_bg_color(self._bg_color)

    # ================================================================
    # 色彩调整
    # ================================================================
    def _on_layer_tab_changed(self, idx):
        self._current_layer_idx = idx

    def _on_layer_enable(self, idx, enabled):
        if 0 <= idx < len(self._layers):
            self._layers[idx].adjust.enabled = enabled
            self._invalidate_cache_for_layer(idx)

    def _on_adj(self, idx, key, val):
        if 0 <= idx < len(self._layers):
            setattr(self._layers[idx].adjust, key, val)
            # 节流: 拖动滑块时 60ms 内多次调用合并为一次缓存失效
            self._dirty_layers.add(idx)
            self._adj_debounce.start()

    def _on_bal(self, idx, key, channel, val):
        if 0 <= idx < len(self._layers):
            lst = getattr(self._layers[idx].adjust, key)
            lst[channel] = val
            self._dirty_layers.add(idx)
            self._adj_debounce.start()

    def _flush_dirty_layers(self):
        """节流后批量失效缓存 + 触发单次重渲染"""
        for idx in self._dirty_layers:
            self._invalidate_cache_for_layer(idx)
        self._dirty_layers.clear()
        # 立即重渲染当前帧 (不等下一次播放 tick)
        self._render_current_frame()

    def _load_lut(self, idx, label):
        path, _ = QFileDialog.getOpenFileName(
            self, "选择LUT文件", "",
            "LUT文件 (*.cube);;所有文件 (*.*)")
        if not path:
            return
        result = parse_cube_file(path)
        if not result:
            QMessageBox.warning(self, "错误", "LUT文件解析失败！")
            return
        mode, data = result
        if mode == '1d':
            self._layers[idx].adjust.lut_1d = data
            self._layers[idx].adjust.lut_3d = None
        else:
            self._layers[idx].adjust.lut_3d = data
            self._layers[idx].adjust.lut_1d = None
        label.setText(f"已加载: {os.path.basename(path)} ({mode})")
        label.setStyleSheet("color: #2563eb;")
        self._invalidate_cache_for_layer(idx)

    def _clear_lut(self, idx, label):
        self._layers[idx].adjust.lut_1d = None
        self._layers[idx].adjust.lut_3d = None
        label.setText("未加载")
        label.setStyleSheet("color: #888;")
        self._invalidate_cache_for_layer(idx)

    def _reset_adjust(self, idx):
        if 0 <= idx < len(self._layers):
            self._layers[idx].adjust = ColorAdjust()
            self._invalidate_cache_for_layer(idx)
            # 重建面板
            self._layer_tabs.removeTab(idx)
            panel = self._create_adjust_panel(idx)
            self._layer_tabs.insertTab(idx, panel, f"{self._layers[idx].name} 层")
            self._layer_tabs.setCurrentIndex(idx)

    def _invalidate_cache_for_layer(self, idx):
        """失效该层级相关缓存"""
        keys_to_remove = []
        for k in self._frame_cache:
            if k[0] == idx:
                keys_to_remove.append(k)
        for k in keys_to_remove:
            del self._frame_cache[k]

    # ================================================================
    # 渲染
    # ================================================================
    def _adj_cache_key(self, adj):
        """为 ColorAdjust 生成简单的哈希值"""
        if not adj.enabled:
            return 'off'
        return (adj.hue, adj.saturation, adj.brightness,
                adj.levels_in_black, adj.levels_in_white, adj.levels_gamma,
                adj.levels_out_black, adj.levels_out_white,
                tuple(adj.curve_points),
                tuple(adj.balance_shadows), tuple(adj.balance_mids),
                tuple(adj.balance_highs),
                id(adj.lut_1d), id(adj.lut_3d))

    def _get_processed_frame(self, layer_idx, frame_path):
        """取得处理后的单层单帧 (带缓存)"""
        layer = self._layers[layer_idx]
        key = (layer_idx, frame_path, self._adj_cache_key(layer.adjust))
        if key in self._frame_cache:
            return self._frame_cache[key]

        img = load_tga(frame_path)
        if img is None:
            return None

        if layer.adjust.has_any_adjustment():
            img = layer.adjust.apply_to_image(img)

        # 缓存限制 (最多 200 张)
        if len(self._frame_cache) > 200:
            # 简单淘汰: 清空一半
            keys = list(self._frame_cache.keys())
            for k in keys[:100]:
                del self._frame_cache[k]
        self._frame_cache[key] = img
        return img

    def _render_current_frame(self):
        """合成并显示当前帧"""
        if not self._layers or not self._current_action:
            return

        # 各层取当前帧, 从下到上 (body -> 00 -> 01)
        composite = None
        for i, layer in enumerate(self._layers):
            if not layer.visible:
                continue
            frames = layer.get_frames(self._current_action, self._current_direction)
            if not frames:
                continue
            # 循环帧号 (若层的帧数 < 总帧数, 取模)
            idx = self._current_frame_idx % len(frames)
            img = self._get_processed_frame(i, frames[idx])
            if img is None:
                continue

            if composite is None:
                composite = QImage(img.size(), QImage.Format_ARGB32)
                composite.fill(Qt.transparent)
            elif composite.size() != img.size():
                # 以首个图像尺寸为准
                pass

            p = QPainter(composite)
            p.setCompositionMode(QPainter.CompositionMode_SourceOver)
            p.drawImage(0, 0, img)
            p.end()

        self._canvas.set_frame(composite)

    # ================================================================
    # UI 布局持久化 (参考 tex_duplicate_page 的实现)
    # ================================================================
    def _save_ui_layout(self):
        """保存完整 UI 布局: 窗口大小、路径、Splitter 比例、背景色"""
        layout_data = {}

        # 1) 应用窗口尺寸
        main_win = self.window()
        if main_win:
            geo = main_win.geometry()
            layout_data['window'] = {
                'x': geo.x(), 'y': geo.y(),
                'w': geo.width(), 'h': geo.height(),
            }

        # 2) 路径设置
        try:
            layout_data['src_dir'] = self._src_edit.get_path()
        except Exception:
            pass
        try:
            layout_data['out_dir'] = self._out_edit.get_path()
        except Exception:
            pass

        # 3) 主 Splitter 比例 (左:预览 / 右:调整面板)
        if hasattr(self, '_splitter') and self._splitter:
            layout_data['splitter'] = list(self._splitter.sizes())

        # 4) 背景色 / FPS (顺带持久化, 方便日常工作)
        try:
            layout_data['bg_color'] = self._bg_color.name() if self._bg_color else ""
        except Exception:
            pass
        try:
            layout_data['fps'] = int(self._fps_spin.value())
        except Exception:
            pass

        self._layout_settings.setValue(self._LAYOUT_KEY, json.dumps(layout_data))
        self._layout_settings.sync()

        # 详细保存信息弹窗
        saved_items = []
        if 'window' in layout_data:
            saved_items.append(
                f"  • 窗口大小: {layout_data['window']['w']}×{layout_data['window']['h']}")
        if layout_data.get('src_dir'):
            saved_items.append(f"  • 源目录: {layout_data['src_dir']}")
        if layout_data.get('out_dir'):
            saved_items.append(f"  • 输出目录: {layout_data['out_dir']}")
        if 'splitter' in layout_data:
            saved_items.append(f"  • 预览/调整分栏比例: {layout_data['splitter']}")
        if layout_data.get('bg_color'):
            saved_items.append(f"  • 预览背景色: {layout_data['bg_color']}")
        if 'fps' in layout_data:
            saved_items.append(f"  • 播放帧率: {layout_data['fps']} fps")

        QMessageBox.information(
            self, "保存成功",
            "当前 UI 布局已保存！\n\n" + "\n".join(saved_items))

    def _reset_ui_layout(self):
        """重置完整 UI 布局为默认状态"""
        try:
            # 1) 恢复 Splitter 默认比例
            if hasattr(self, '_splitter') and self._splitter:
                self._splitter.setSizes(list(self._default_splitter_sizes))

            # 2) 删除已保存的布局数据
            self._layout_settings.remove(self._LAYOUT_KEY)
            self._layout_settings.sync()

            QMessageBox.information(self, "重置成功",
                                    "UI 布局已恢复为默认设置。\n\n"
                                    "• 预览/调整分栏比例恢复为默认\n"
                                    "• 已清除保存的布局数据")
        except Exception as e:
            QMessageBox.critical(self, "重置失败", f"重置布局时出错:\n{e}")

    def restore_saved_window_geometry(self, main_window=None):
        """由主窗口挂载后调用或页面初始化末尾调用, 恢复布局。
        返回 True 表示成功恢复了窗口尺寸。

        ⚠ 防御加强(2025-04): 任何子步骤抛异常都不能让页面构造失败,
        否则会看到 STATUS_STACK_BUFFER_OVERRUN 类 native 崩溃。
        """
        try:
            raw = self._layout_settings.value(self._LAYOUT_KEY, None)
            if raw is None or raw == "":
                return False
            if not isinstance(raw, str):
                raw = str(raw)
            try:
                layout_data = json.loads(raw)
            except Exception:
                # JSON 破损 — 直接清空该键避免下次再触发
                try:
                    self._layout_settings.remove(self._LAYOUT_KEY)
                    self._layout_settings.sync()
                except Exception:
                    pass
                return False
            if not isinstance(layout_data, dict):
                return False

            restored_window = False

            # 1) 窗口尺寸
            win_data = layout_data.get('window')
            if win_data and main_window:
                from PyQt5.QtCore import QRect
                screen = QApplication.primaryScreen()
                if screen:
                    avail = screen.availableGeometry()
                    w = min(int(win_data.get('w', 1500)), avail.width())
                    h = min(int(win_data.get('h', 1050)), avail.height())
                    x = int(win_data.get('x', avail.x()))
                    y = int(win_data.get('y', avail.y()))
                    x = max(avail.x(), min(x, avail.right() - w))
                    y = max(avail.y(), min(y, avail.bottom() - h))
                    main_window.setGeometry(QRect(x, y, w, h))
                    restored_window = True

            # 2) Splitter — 严格校验 sp_data 必须是 list/tuple 且元素都 > 0
            sp_data = layout_data.get('splitter')
            if (sp_data and isinstance(sp_data, (list, tuple))
                    and hasattr(self, '_splitter') and self._splitter):
                try:
                    sizes = [max(50, int(s)) for s in sp_data]  # 防止 0/负值崩溃
                    if len(sizes) >= 2:
                        self._splitter.setSizes(sizes)
                except (TypeError, ValueError, OverflowError):
                    pass

            # 3) 路径
            src = layout_data.get('src_dir')
            if src:
                try:
                    self._src_edit.set_path(src)
                except Exception:
                    pass
            out = layout_data.get('out_dir')
            if out:
                try:
                    self._out_edit.set_path(out)
                except Exception:
                    pass

            # 4) 背景色 / fps
            bg = layout_data.get('bg_color')
            if bg:
                try:
                    self._bg_color = QColor(bg)
                    self._canvas.set_bg_color(self._bg_color)
                except Exception:
                    pass
            fps = layout_data.get('fps')
            if fps:
                try:
                    self._fps_spin.setValue(int(fps))
                except Exception:
                    pass

            return restored_window
        except Exception:
            return False

    # ================================================================
    # 导出
    # ================================================================
    def _do_export(self):
        if not self._layers:
            QMessageBox.warning(self, "提示", "请先加载源目录！")
            return

        out_dir = self._out_edit.text().strip()
        if not out_dir:
            QMessageBox.warning(self, "提示", "请先选择输出目录！")
            return

        if not os.path.isdir(out_dir):
            try:
                os.makedirs(out_dir, exist_ok=True)
            except Exception as e:
                QMessageBox.critical(self, "错误", f"创建输出目录失败:\n{e}")
                return

        # 统计总帧数
        total = 0
        for layer in self._layers:
            for action, dirs in layer.actions.items():
                for d, frames in dirs.items():
                    total += len(frames)

        if total == 0:
            QMessageBox.information(self, "提示", "没有可导出的帧。")
            return

        self._progress.setMaximum(total)
        self._progress.setValue(0)

        count = 0
        errors = 0
        for layer in self._layers:
            layer_out = os.path.join(out_dir, layer.name)
            for action, dirs in layer.actions.items():
                action_out = os.path.join(layer_out, action)
                os.makedirs(action_out, exist_ok=True)
                for d, frames in dirs.items():
                    for fp in frames:
                        fname = os.path.basename(fp)
                        out_fp = os.path.join(action_out, fname)
                        img = load_tga(fp)
                        if img is None:
                            errors += 1
                        else:
                            if layer.adjust.has_any_adjustment():
                                img = layer.adjust.apply_to_image(img)
                            # 用自定义 writer 保证 Alpha + 中文路径 + RLE 兼容
                            if not save_tga(img, out_fp):
                                errors += 1
                        count += 1
                        if count % 5 == 0:
                            self._progress.setValue(count)
                            QApplication.processEvents()

        self._progress.setValue(total)
        QMessageBox.information(
            self, "完成",
            f"导出完成！\n总计: {count} 张\n失败: {errors} 张\n"
            f"输出目录: {out_dir}")
