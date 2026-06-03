"""
pp_pipeline_page.py  ─  PP官方管线 调色工具 (PyQt5 重构版)

对标"大话造型区域变色配色工具", 在 14 字段调色基础上扩展:
    · 多方案 (Skin) 管理 — 一个 .pp 文件对应一个角色的所有变色版本
    · 多层 / 单层 / 单帧 TGA 加载 (与 LUT变色 模块对齐)
    · 自动循环播放 + 动作/方向/帧率/背景 控制 (与 LUT变色 模块对齐)
    · TCP 二进制加载 — 调起 tcp_2_tga.exe 转 TGA (备用路径, 主推 TGA 序列)

界面:
    ┌──────────────────────────────────────────────────────────────────────┐
    │ 顶部: 源目录 + 加载按钮 / .pp 路径 + 状态                            │
    ├────────────────────┬───────────────────────────────────────────────┤
    │  方案画廊            │   预览 (TGA × PP 实时变换, 自动循环播放)        │
    │  + 14 字段滑块       │   动作 / 方向 / 帧率 / 背景                     │
    │  + 多方案管理        │                                                │
    └────────────────────┴───────────────────────────────────────────────┘
    底部: 导入 .pp / 保存 .pp / 状态栏

加载支持的 4 种结构 (自动识别):
    1) 多层序列  : 源目录/{body|00|01}/<动作>/<方向><帧>.tga
    2) 单层序列  : 源目录/<动作>/<方向><帧>.tga
    3) 平铺序列  : 源目录/<方向><帧>.tga                       → 包成"default"动作
    4) 单帧 TGA  : 选中一个 .tga 文件                          → 包成 1 帧

算法在 modules/pp_core.py, TCP IO 在 modules/tcp_loader.py.
"""
from __future__ import annotations

import os
import re
import traceback
from typing import List, Optional, Dict

import numpy as np

from PyQt5.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
    QGroupBox, QFileDialog, QMessageBox, QComboBox,
    QSlider, QSpinBox, QListWidget, QListWidgetItem,
    QScrollArea, QFrame, QSplitter, QApplication, QInputDialog,
    QSizePolicy, QButtonGroup, QCheckBox, QColorDialog, QTabWidget,
)
from PyQt5.QtGui import QFont, QImage, QPixmap, QIcon, QColor, QPainter
from PyQt5.QtCore import Qt, QSize, QTimer, QThread, pyqtSignal

from modules import pp_format, pp_core, tcp_loader
from modules.path_widgets import PathLineEdit, LayoutMixin
from modules.lut_recolor_page import load_tga, PreviewCanvas


# 与 LUT变色 模块完全一致的多层结构定义
DEFAULT_LAYER_ORDER = ['body', '00', '01']
PSEUDO_ACTION = 'default'   # 平铺序列 / 单帧 TGA 时的伪动作名


# ═══════════════════════════════════════════════════════════════════
# 多层 TGA 数据描述
# ═══════════════════════════════════════════════════════════════════
class _TgaLayer:
    """
    单个层 (body / 00 / 01 …) 的帧索引.

    actions 结构: { action_name: { dir_id (int): [tga_path, ...] } }
        dir_id : 0..7 (按文件名首位数字)
        list   : 已按帧号排序的 TGA 路径
    """
    __slots__ = ('name', 'root', 'actions')

    def __init__(self, name: str, root: str):
        self.name: str = name
        self.root: str = root
        self.actions: Dict[str, Dict[int, List[str]]] = {}

    def directions(self, action: str) -> List[int]:
        return sorted(self.actions.get(action, {}).keys())

    def frame_count(self, action: str, direction: int) -> int:
        return len(self.actions.get(action, {}).get(direction, []))

    def get(self, action: str, direction: int, frame: int) -> Optional[str]:
        frames = self.actions.get(action, {}).get(direction, [])
        if not frames:
            return None
        return frames[min(frame, len(frames) - 1)]


# ═══════════════════════════════════════════════════════════════════
# QImage ↔ numpy 桥接
# ═══════════════════════════════════════════════════════════════════
def qimage_to_rgba(img: QImage) -> Optional[np.ndarray]:
    """把 QImage 转成 (H, W, 4) uint8 RGBA numpy 数组. 失败返回 None."""
    if img is None or img.isNull():
        return None
    if img.format() != QImage.Format_RGBA8888:
        img = img.convertToFormat(QImage.Format_RGBA8888)
    w, h = img.width(), img.height()
    if w <= 0 or h <= 0:
        return None
    ptr = img.constBits()
    ptr.setsize(h * img.bytesPerLine())
    # 注意: bytesPerLine 可能 > w*4 (有 padding)
    arr = np.frombuffer(ptr, dtype=np.uint8).reshape(h, img.bytesPerLine())[:, :w*4]
    return arr.reshape(h, w, 4).copy()


def rgba_to_qimage(arr: np.ndarray) -> QImage:
    """(H, W, 4) uint8 RGBA → QImage.Format_RGBA8888 (.copy() 防止 buffer 失效)"""
    if arr is None or arr.ndim != 3 or arr.shape[2] != 4:
        return QImage()
    h, w = arr.shape[:2]
    arr = np.ascontiguousarray(arr, dtype=np.uint8)
    img = QImage(arr.tobytes(), w, h, w * 4, QImage.Format_RGBA8888)
    return img.copy()


def make_color_swatch(adj: pp_core.PPAdjust, size: int = 40) -> QPixmap:
    """
    为方案生成一个色块缩略图 — 把 1x6 的色谱 (R/Y/G/C/B/M) 喂进 PP 管线,
    出来什么颜色就是这个方案的"色彩指纹".
    """
    # 6 色 + 黑白 = 8 色 (排成 2x4)
    src = np.array([
        [255,   0,   0,  255],
        [255, 255,   0,  255],
        [  0, 255,   0,  255],
        [  0, 255, 255,  255],
        [  0,   0, 255,  255],
        [255,   0, 255,  255],
        [128, 128, 128, 255],
        [255, 255, 255, 255],
    ], dtype=np.uint8).reshape(2, 4, 4)
    out = pp_core.apply_pp_adjust(src, adj)
    qi = rgba_to_qimage(out)
    return QPixmap.fromImage(qi.scaled(size, size, Qt.IgnoreAspectRatio,
                                        Qt.FastTransformation))


# ═══════════════════════════════════════════════════════════════════
# 单条「标签 + 滑块 + 数字框」
# ═══════════════════════════════════════════════════════════════════
class _SliderRow(QWidget):
    def __init__(self, label_text: str, min_val: int, max_val: int,
                 default_val: int, on_change=None, parent=None):
        super().__init__(parent)
        self._on_change = on_change
        self._updating = False

        layout = QHBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(6)

        lbl = QLabel(label_text)
        lbl.setFixedWidth(58)
        lbl.setFont(QFont("Microsoft YaHei UI", 10))
        layout.addWidget(lbl)

        self._slider = QSlider(Qt.Horizontal)
        self._slider.setRange(min_val, max_val)
        self._slider.setValue(default_val)
        self._slider.setFixedHeight(20)
        layout.addWidget(self._slider, 1)

        self._spin = QSpinBox()
        self._spin.setRange(min_val, max_val)
        self._spin.setValue(default_val)
        self._spin.setFixedWidth(70)
        self._spin.setButtonSymbols(QSpinBox.UpDownArrows)
        layout.addWidget(self._spin)

        self._slider.valueChanged.connect(self._on_slider_change)
        self._spin.valueChanged.connect(self._on_spin_change)

    def _on_slider_change(self, v):
        if self._updating:
            return
        self._updating = True
        self._spin.setValue(v)
        self._updating = False
        if self._on_change:
            self._on_change(v)

    def _on_spin_change(self, v):
        if self._updating:
            return
        self._updating = True
        self._slider.setValue(v)
        self._updating = False
        if self._on_change:
            self._on_change(v)

    def value(self) -> int:
        return self._spin.value()

    def set_value(self, v: int):
        self._updating = True
        self._slider.setValue(v)
        self._spin.setValue(v)
        self._updating = False


# ═══════════════════════════════════════════════════════════════════
# PP 14 字段 调色面板 — 与 .pp 文件 1:1
# ═══════════════════════════════════════════════════════════════════
class PPSliderPanel(QWidget):
    """PP官方管线 14 字段滑块面板 (按截图布局 1:1 复刻)"""

    def __init__(self, on_change=None, parent=None):
        super().__init__(parent)
        self._on_change = on_change
        self._adjust = pp_core.PPAdjust()
        self._init_ui()

    def _init_ui(self):
        root = QVBoxLayout(self)
        root.setContentsMargins(6, 6, 6, 6)
        root.setSpacing(8)

        # 矩阵组
        mat_grp = QGroupBox("通道混合 (矩阵 × 输入 ÷ 256)")
        mat_grp.setFont(QFont("Microsoft YaHei UI", 10, QFont.Bold))
        mat_layout = QVBoxLayout(mat_grp)
        mat_layout.setSpacing(3)

        rows_config = [
            ("红-红", "rr", 256),  ("红-绿", "rg",   0),  ("红-蓝", "rb",   0),
            ("绿-红", "gr",   0),  ("绿-绿", "gg", 256),  ("绿-蓝", "gb",   0),
            ("蓝-红", "br",   0),  ("蓝-绿", "bg",   0),  ("蓝-蓝", "bb", 256),
        ]
        self._rows = {}
        for label, key, default in rows_config:
            row = _SliderRow(label, 0, 512, default, on_change=self._slot_field(key))
            mat_layout.addWidget(row)
            self._rows[key] = row
        root.addWidget(mat_grp)

        # HSL 组
        hsl_grp = QGroupBox("HSL 后处理")
        hsl_grp.setFont(QFont("Microsoft YaHei UI", 10, QFont.Bold))
        hsl_layout = QVBoxLayout(hsl_grp)
        hsl_layout.setSpacing(3)

        for label, key, lo, hi, default in [
            ("H",    "h",  0,  500,   0),
            ("S",    "s",  0, 1000, 500),
            ("L",    "l",  0, 1000, 500),
            ("线亮度", "lb", 0,  200, 100),
            ("对比度", "c",  0,  256, 128),
        ]:
            row = _SliderRow(label, lo, hi, default, on_change=self._slot_field(key))
            hsl_layout.addWidget(row)
            self._rows[key] = row
        root.addWidget(hsl_grp)

        # 按钮
        btn_row = QHBoxLayout()
        btn_row.setSpacing(6)
        self._reset_btn = QPushButton("中性")
        self._reset_btn.setToolTip("恢复到中性默认 (单位矩阵 + HSL 0/500/500/100/128)")
        self._reset_btn.clicked.connect(self._on_reset)
        btn_row.addWidget(self._reset_btn)
        self._zero_btn = QPushButton("全部清零")
        self._zero_btn.setToolTip("14 滑块全部归 0 (对应 .pp 样本 #12, 旧工具语义=禁用)")
        self._zero_btn.clicked.connect(self._on_zero)
        btn_row.addWidget(self._zero_btn)
        self._rand_btn = QPushButton("🎲 随机换色")
        self._rand_btn.setToolTip(
            "随机生成一组合理参数:\n"
            "  · 矩阵每个元素在合理范围内浮动 (主对角线偏强, 对角线偏弱)\n"
            "  · HSL 在标准中性附近随机偏移\n"
            "可反复点击直到出现满意配色, 再保存为新方案.")
        self._rand_btn.setStyleSheet(
            "QPushButton { background:#8e44ad; color:white; font-weight:bold; }"
            "QPushButton:hover { background:#7d3c98; }")
        self._rand_btn.clicked.connect(self._on_random)
        btn_row.addWidget(self._rand_btn)
        btn_row.addStretch()
        root.addLayout(btn_row)

        root.addStretch()

    def _slot_field(self, key):
        def slot(v):
            setattr(self._adjust, key, v)
            if self._on_change:
                self._on_change()
        return slot

    def _on_reset(self):
        self._set_adjust(pp_core.PPAdjust())

    def _on_zero(self):
        z = pp_core.PPAdjust()
        for k in ("rr", "rg", "rb", "gr", "gg", "gb", "br", "bg", "bb",
                  "h", "s", "l", "lb", "c"):
            setattr(z, k, 0)
        self._set_adjust(z)

    def _on_random(self):
        """生成一个"看起来像换肤而非乱码"的随机配色.

        矩阵: 主对角线 180~330 (单位 256), 副对角线 -60~+60 → 整体仍接近彩度保持
        HSL : H 0~500 任意, S 350~700, L 400~600, 线亮度 80~140, 对比度 100~180
        """
        import random
        adj = pp_core.PPAdjust()
        # 主对角线
        adj.rr = random.randint(180, 330)
        adj.gg = random.randint(180, 330)
        adj.bb = random.randint(180, 330)
        # 副对角线 (允许负值表示通道相减, _SliderRow 范围 0~512, 0=未启用)
        # 旧管线 .pp 字段是 uint16, 这里只取正向偏移
        adj.rg = random.randint(0, 80)
        adj.rb = random.randint(0, 80)
        adj.gr = random.randint(0, 80)
        adj.gb = random.randint(0, 80)
        adj.br = random.randint(0, 80)
        adj.bg = random.randint(0, 80)
        # HSL
        adj.h  = random.randint(0, 500)
        adj.s  = random.randint(350, 700)
        adj.l  = random.randint(400, 600)
        adj.lb = random.randint(80, 140)
        adj.c  = random.randint(100, 180)
        self._set_adjust(adj)

    def _set_adjust(self, adj: pp_core.PPAdjust):
        """把 adj 写入 UI (会触发 on_change 回调)"""
        old = self._on_change
        self._on_change = None      # 静默批量赋值, 避免 14 次重绘
        try:
            for key in ("rr", "rg", "rb", "gr", "gg", "gb", "br", "bg", "bb",
                        "h", "s", "l", "lb", "c"):
                self._rows[key].set_value(getattr(adj, key))
            self._adjust = adj.clone()
        finally:
            self._on_change = old
        if self._on_change:
            self._on_change()

    def get_adjust(self) -> pp_core.PPAdjust:
        return self._adjust

    def set_adjust(self, adj: pp_core.PPAdjust):
        self._set_adjust(adj)


# ═══════════════════════════════════════════════════════════════════
# 多方案数据模型
# ═══════════════════════════════════════════════════════════════════
class PPProject:
    """
    一个完整 .pp 工程: 一组方案 (Region) + 元数据.

    与 pp_format.PPFile 一一对应:
        magic / ui_index / alpha → 头部
        regions[i]               → 第 i 个变色方案 (=一个角色的一个皮肤版本)
    """
    def __init__(self):
        self.magic    = 1001
        self.ui_index = 0
        self.alpha    = 255
        self.adjusts: List[pp_core.PPAdjust] = [pp_core.PPAdjust()]
        self.names:   List[str]              = ["方案 1 (默认)"]

    # ───── 工厂 ─────
    @classmethod
    def from_pp_file(cls, pp: pp_format.PPFile) -> "PPProject":
        proj = cls()
        proj.magic    = pp.magic
        proj.ui_index = pp.ui_index
        proj.alpha    = pp.alpha
        proj.adjusts  = []
        proj.names    = []
        for i, region in enumerate(pp.regions):
            proj.adjusts.append(pp_core.PPAdjust.from_pp_region(region))
            proj.names.append(f"方案 {i + 1}")
        if not proj.adjusts:
            proj.adjusts = [pp_core.PPAdjust()]
            proj.names   = ["方案 1 (默认)"]
        return proj

    @classmethod
    def new_default(cls) -> "PPProject":
        return cls()

    def to_pp_file(self) -> pp_format.PPFile:
        regions = [adj.to_pp_region(index=i) for i, adj in enumerate(self.adjusts)]
        for r in regions:
            r.clamp_to_valid()
        return pp_format.PPFile(
            magic=self.magic, ui_index=self.ui_index, alpha=self.alpha,
            regions=regions,
        )

    # ───── 增删改 ─────
    def add_default(self, name: Optional[str] = None) -> int:
        self.adjusts.append(pp_core.PPAdjust())
        self.names.append(name or f"方案 {len(self.adjusts)}")
        return len(self.adjusts) - 1

    def duplicate(self, idx: int) -> int:
        if not (0 <= idx < len(self.adjusts)):
            return -1
        self.adjusts.append(self.adjusts[idx].clone())
        self.names.append(self.names[idx] + " 副本")
        return len(self.adjusts) - 1

    def remove(self, idx: int) -> bool:
        if len(self.adjusts) <= 1:
            return False
        if not (0 <= idx < len(self.adjusts)):
            return False
        self.adjusts.pop(idx)
        self.names.pop(idx)
        return True

    def rename(self, idx: int, new_name: str) -> None:
        if 0 <= idx < len(self.adjusts):
            self.names[idx] = new_name


# ═══════════════════════════════════════════════════════════════════
# 主页面
# ═══════════════════════════════════════════════════════════════════
class PPPipelinePage(QWidget, LayoutMixin):
    """
    PP官方管线 调色工具 (重构版 — 大话二代 区域配色 PyQt5 实现)
    """
    # LayoutMixin 用的页面唯一键 (子类可覆盖)
    _LAYOUT_KEY = "pp_pipeline_tab1"

    def __init__(self, parent=None):
        super().__init__(parent)
        self._project: PPProject = PPProject.new_default()
        self._current_scheme_idx: int = 0
        self._tcp_path: Optional[str] = None
        self._tcp_header: Optional[tcp_loader.TcpHeader] = None
        self._pp_path: Optional[str] = None
        # ─ 与 LUT变色 模块对齐: 多层数据 + 自动循环播放 + 缓存
        self._source_root: str = ""
        self._layers: List[_TgaLayer] = []        # body / 00 / 01 多层
        self._current_action: Optional[str] = None
        self._current_direction: int = 0
        self._current_num_dirs: int = 4           # 当前可用方向数 (2/4/8)
        self._current_frame_idx: int = 0
        self._total_frames: int = 1
        self._playing: bool = True
        self._fps: int = 10
        self._bg_color = QColor(60, 60, 60)
        self._bg_image_path: str = ""
        # 缓存: (layer_idx, frame_path, scheme_idx, pp_key) → QImage 处理后单层单帧
        self._frame_cache: Dict[tuple, QImage] = {}
        # 旧的预览状态 (TCP 模式仍然保留)
        self._preview_qimage: Optional[QImage] = None
        self._preview_rgba: Optional[np.ndarray] = None
        self._tga_root: Optional[str] = None
        self._tga_index: dict = {}
        # 调色刷新节流
        self._refresh_timer = QTimer(self)
        self._refresh_timer.setSingleShot(True)
        self._refresh_timer.setInterval(30)
        self._refresh_timer.timeout.connect(self._do_refresh_preview)
        self._init_ui()
        # 自动循环播放定时器
        self._play_timer = QTimer(self)
        self._play_timer.timeout.connect(self._on_tick)
        self._play_timer.start(int(1000 / self._fps))
        self._sync_scheme_list()
        self._sync_panel_from_current()
        self._update_status()

    # ───── UI ────────────────────────────────────────────────────────
    def _init_ui(self):
        outer = QVBoxLayout(self)
        outer.setContentsMargins(10, 10, 10, 10)
        outer.setSpacing(6)

        # ── 顶部: TCP 路径行 ──
        tcp_row = QHBoxLayout()
        tcp_row.setSpacing(6)
        tcp_row.addWidget(QLabel("素材:"))
        self._tcp_edit = PathLineEdit()
        self._tcp_edit.setPlaceholderText(
            "选择一个 .tcp 文件 或 一个含 0000.tga 的 TGA 序列目录")
        tcp_row.addWidget(self._tcp_edit, 1)
        self._load_tcp_btn = QPushButton("加载 TCP")
        self._load_tcp_btn.setToolTip("把 .tcp 文件解码成 TGA 后预览")
        self._load_tcp_btn.clicked.connect(self._do_load_tcp)
        tcp_row.addWidget(self._load_tcp_btn)
        self._load_tga_btn = QPushButton("加载 TGA 序列")
        self._load_tga_btn.setToolTip(
            "选择一个目录, 里面应有形如 0000.tga / 1003.tga 的 4 位数字 TGA 文件\n"
            "(目录名/方向号/帧号 自动识别)")
        self._load_tga_btn.clicked.connect(self._do_load_tga)
        tcp_row.addWidget(self._load_tga_btn)
        outer.addLayout(tcp_row)

        # ── 顶部: PP 路径行 ──
        pp_row = QHBoxLayout()
        pp_row.setSpacing(6)
        pp_row.addWidget(QLabel(".pp:"))
        self._pp_label = QLabel("(尚未关联)")
        self._pp_label.setStyleSheet("color: #888;")
        pp_row.addWidget(self._pp_label, 1)
        self._import_pp_btn = QPushButton("导入 .pp")
        self._import_pp_btn.clicked.connect(self._do_import_pp)
        pp_row.addWidget(self._import_pp_btn)
        self._save_pp_btn = QPushButton("保存 .pp")
        self._save_pp_btn.clicked.connect(self._do_save_pp)
        pp_row.addWidget(self._save_pp_btn)
        self._save_as_btn = QPushButton("另存为…")
        self._save_as_btn.clicked.connect(self._do_save_pp_as)
        pp_row.addWidget(self._save_as_btn)
        outer.addLayout(pp_row)

        # ── 中部: 三栏 QSplitter ──
        splitter = QSplitter(Qt.Horizontal)

        # 左: TCP 信息卡 + 操作
        left = self._build_left_panel()
        splitter.addWidget(left)

        # 中: 方案画廊 + 14 滑块
        middle = self._build_middle_panel()
        splitter.addWidget(middle)

        # 右: 预览 + 帧切换
        right = self._build_right_panel()
        splitter.addWidget(right)

        splitter.setStretchFactor(0, 0)
        splitter.setStretchFactor(1, 0)
        splitter.setStretchFactor(2, 1)
        splitter.setSizes([240, 380, 700])
        outer.addWidget(splitter, 1)

        # ── 底部状态栏 ──
        self._status_label = QLabel("就绪")
        self._status_label.setStyleSheet(
            "QLabel { background:#2b2b2b; color:#bbb; padding:4px 8px; "
            "border-top:1px solid #444; font: 9pt 'Microsoft YaHei UI'; }")
        outer.addWidget(self._status_label)

        # ── LayoutMixin: 保存/重置 UI 布局 ──
        self._layout_key = self._LAYOUT_KEY
        self._layout_splitters = [splitter]
        self._layout_tables = []
        self._layout_defaults = {
            'splitters': [[240, 380, 700]],
            'tables': [],
        }
        self._init_layout_buttons(outer)   # 自动插到顶部
        self._restore_layout()

    def _build_left_panel(self) -> QWidget:
        w = QFrame()
        w.setFrameShape(QFrame.StyledPanel)
        w.setMinimumWidth(220)
        w.setMaximumWidth(280)
        lay = QVBoxLayout(w)
        lay.setContentsMargins(8, 8, 8, 8)
        lay.setSpacing(8)

        title = QLabel("TCP 信息")
        title.setFont(QFont("Microsoft YaHei UI", 11, QFont.Bold))
        lay.addWidget(title)

        self._tcp_info = QLabel("(未加载)")
        self._tcp_info.setWordWrap(True)
        self._tcp_info.setStyleSheet(
            "QLabel { background:#1e1e1e; color:#dcdcdc; "
            "border:1px solid #3a3a3a; border-radius:4px; padding:8px; "
            "font: 9pt 'Consolas','Microsoft YaHei UI'; }")
        self._tcp_info.setMinimumHeight(110)
        lay.addWidget(self._tcp_info)

        # 预览尺寸 + 帧选择
        ctl = QGroupBox("预览设置")
        ctl_l = QVBoxLayout(ctl)
        ctl_l.setSpacing(4)

        size_row = QHBoxLayout()
        size_row.addWidget(QLabel("画布:"))
        self._size_combo = QComboBox()
        for s in ("320", "500", "1000", "1500"):
            self._size_combo.addItem(s, int(s))
        self._size_combo.setCurrentText("500")
        self._size_combo.currentIndexChanged.connect(self._on_size_change)
        size_row.addWidget(self._size_combo, 1)
        ctl_l.addLayout(size_row)

        dir_row = QHBoxLayout()
        dir_row.addWidget(QLabel("方向:"))
        self._dir_combo = QComboBox()
        for i in range(8):
            self._dir_combo.addItem(f"{i}", i)
        self._dir_combo.currentIndexChanged.connect(self._on_dir_change)
        dir_row.addWidget(self._dir_combo, 1)
        ctl_l.addLayout(dir_row)

        frame_row = QHBoxLayout()
        frame_row.addWidget(QLabel("帧:"))
        self._frame_spin = QSpinBox()
        self._frame_spin.setRange(0, 0)
        self._frame_spin.valueChanged.connect(self._on_frame_change)
        frame_row.addWidget(self._frame_spin, 1)
        ctl_l.addLayout(frame_row)

        lay.addWidget(ctl)

        # 工具按钮
        self._clear_cache_btn = QPushButton("清空 TGA 缓存")
        self._clear_cache_btn.clicked.connect(self._do_clear_cache)
        lay.addWidget(self._clear_cache_btn)

        self._open_folder_btn = QPushButton("在资源管理器中打开")
        self._open_folder_btn.clicked.connect(self._do_open_folder)
        lay.addWidget(self._open_folder_btn)

        lay.addStretch()
        return w

    def _build_middle_panel(self) -> QWidget:
        w = QFrame()
        w.setFrameShape(QFrame.StyledPanel)
        w.setMinimumWidth(360)
        w.setMaximumWidth(420)
        lay = QVBoxLayout(w)
        lay.setContentsMargins(6, 6, 6, 6)
        lay.setSpacing(6)

        # 方案画廊
        gallery_grp = QGroupBox("方案 (Skin)  — 一个 .pp 可含多组变色")
        gallery_grp.setFont(QFont("Microsoft YaHei UI", 10, QFont.Bold))
        gv = QVBoxLayout(gallery_grp)
        gv.setSpacing(4)

        self._scheme_list = QListWidget()
        self._scheme_list.setIconSize(QSize(36, 36))
        self._scheme_list.setFixedHeight(140)
        self._scheme_list.currentRowChanged.connect(self._on_scheme_select)
        self._scheme_list.itemDoubleClicked.connect(self._on_scheme_rename)
        gv.addWidget(self._scheme_list)

        btn_row = QHBoxLayout()
        btn_row.setSpacing(4)
        self._add_scheme_btn = QPushButton("➕ 新建")
        self._add_scheme_btn.clicked.connect(self._do_add_scheme)
        btn_row.addWidget(self._add_scheme_btn)
        self._dup_scheme_btn = QPushButton("📋 复制")
        self._dup_scheme_btn.clicked.connect(self._do_dup_scheme)
        btn_row.addWidget(self._dup_scheme_btn)
        self._del_scheme_btn = QPushButton("🗑 删除")
        self._del_scheme_btn.clicked.connect(self._do_del_scheme)
        btn_row.addWidget(self._del_scheme_btn)
        self._rename_scheme_btn = QPushButton("✎ 改名")
        self._rename_scheme_btn.clicked.connect(self._do_rename_scheme)
        btn_row.addWidget(self._rename_scheme_btn)
        gv.addLayout(btn_row)

        lay.addWidget(gallery_grp)

        # 14 字段滑块
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        self._panel = PPSliderPanel(on_change=self._on_adjust_change)
        scroll.setWidget(self._panel)
        lay.addWidget(scroll, 1)

        return w

    def _build_right_panel(self) -> QWidget:
        w = QWidget()
        lay = QVBoxLayout(w)
        lay.setContentsMargins(6, 6, 6, 6)
        lay.setSpacing(4)

        head = QHBoxLayout()
        head.addWidget(QLabel("预览  (TGA × PP 实时变换)"))
        head.addStretch()
        self._refresh_btn = QPushButton("↻ 重新解码")
        self._refresh_btn.setToolTip("强制重跑 tcp_2_tga.exe (跳过缓存)")
        self._refresh_btn.clicked.connect(lambda: self._reload_preview(force=True))
        head.addWidget(self._refresh_btn)
        lay.addLayout(head)

        self._canvas = PreviewCanvas()
        self._canvas.setMinimumSize(420, 420)
        self._canvas.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)
        # 初始化背景色
        try:
            self._canvas.set_bg_color(self._bg_color)
        except Exception:
            pass
        lay.addWidget(self._canvas, 1)

        # ── 与 LUT变色 模块对齐的 4 行播放/方向/背景控制 ──

        # ① 动作 + 方向数
        ctrl1 = QHBoxLayout()
        ctrl1.setSpacing(8)
        ctrl1.addWidget(QLabel("动作:"))
        self._action_combo = QComboBox()
        self._action_combo.setMinimumWidth(140)
        self._action_combo.currentTextChanged.connect(self._on_action_changed)
        ctrl1.addWidget(self._action_combo)
        ctrl1.addWidget(QLabel("方向数:"))
        self._dir_mode_combo = QComboBox()
        self._dir_mode_combo.addItems(["自动", "2方向", "4方向", "8方向"])
        self._dir_mode_combo.setFixedWidth(90)
        self._dir_mode_combo.currentIndexChanged.connect(self._on_dir_mode_changed)
        ctrl1.addWidget(self._dir_mode_combo)
        ctrl1.addStretch()
        lay.addLayout(ctrl1)

        # ② 方向按钮 1-8
        ctrl2 = QHBoxLayout()
        ctrl2.setSpacing(4)
        ctrl2.addWidget(QLabel("方向:"))
        self._dir_btn_group = QButtonGroup(self)
        self._dir_btns: List[QPushButton] = []
        for i in range(8):
            btn = QPushButton(str(i + 1))
            btn.setCheckable(True)
            btn.setFixedSize(30, 28)
            btn.clicked.connect(lambda _=False, idx=i: self._switch_direction(idx))
            self._dir_btn_group.addButton(btn, i)
            self._dir_btns.append(btn)
            ctrl2.addWidget(btn)
        ctrl2.addStretch()
        lay.addLayout(ctrl2)

        # ③ 暂停 + 帧率 + 当前帧
        ctrl3 = QHBoxLayout()
        self._play_btn = QPushButton("⏸ 暂停")
        self._play_btn.setFixedWidth(80)
        self._play_btn.clicked.connect(self._toggle_play)
        ctrl3.addWidget(self._play_btn)
        ctrl3.addWidget(QLabel("帧率:"))
        self._fps_spin = QSpinBox()
        self._fps_spin.setRange(1, 60)
        self._fps_spin.setValue(self._fps)
        self._fps_spin.setFixedWidth(60)
        self._fps_spin.valueChanged.connect(self._on_fps_changed)
        ctrl3.addWidget(self._fps_spin)
        ctrl3.addWidget(QLabel("当前帧:"))
        self._frame_label = QLabel("0/0")
        self._frame_label.setFixedWidth(60)
        ctrl3.addWidget(self._frame_label)
        ctrl3.addStretch()
        lay.addLayout(ctrl3)

        # ④ 背景控制
        ctrl4 = QHBoxLayout()
        ctrl4.addWidget(QLabel("背景:"))
        self._bg_color_btn = QPushButton("选择颜色")
        self._bg_color_btn.setFixedWidth(90)
        self._bg_color_btn.clicked.connect(self._pick_bg_color)
        ctrl4.addWidget(self._bg_color_btn)
        bg_img_btn = QPushButton("选择背景图")
        bg_img_btn.setFixedWidth(100)
        bg_img_btn.clicked.connect(self._pick_bg_image)
        ctrl4.addWidget(bg_img_btn)
        bg_clear_btn = QPushButton("清除背景图")
        bg_clear_btn.setFixedWidth(100)
        bg_clear_btn.clicked.connect(self._clear_bg_image)
        ctrl4.addWidget(bg_clear_btn)
        ctrl4.addStretch()
        lay.addLayout(ctrl4)

        return w

    # ───── TGA 序列加载 (TCP 解码失败时的兜底验证路径) ────────────────
    def _do_load_tga(self):
        """让用户选一个目录, 扫里面的 [方向][帧].tga 文件"""
        import re
        path = self._tcp_edit.text().strip()
        if not path or not os.path.isdir(path):
            path = QFileDialog.getExistingDirectory(
                self, "选择 TGA 序列目录 (含 0000.tga / 1003.tga 等)",
                os.path.dirname(self._tcp_path or self._pp_path or os.path.expanduser("~")))
            if not path:
                return
            self._tcp_edit.setText(path)

        # 扫描 (方向, 帧) -> 文件路径
        pattern = re.compile(r'^(\d)(\d{3})\.tga$', re.IGNORECASE)
        index: dict = {}
        try:
            for fn in os.listdir(path):
                m = pattern.match(fn)
                if m:
                    d = int(m.group(1))
                    f = int(m.group(2))
                    index.setdefault(d, []).append((f, os.path.join(path, fn)))
        except OSError as e:
            QMessageBox.critical(self, "读取目录失败", str(e))
            return
        if not index:
            QMessageBox.warning(
                self, "未找到 TGA 序列",
                f"在 {path} 下没有找到符合命名规范 [方向][帧号].tga 的文件 "
                f"(例如 0000.tga, 1003.tga, 7011.tga)")
            return

        # 排序帧
        for d in index:
            index[d].sort(key=lambda x: x[0])
            index[d] = [p for _, p in index[d]]
        # 进入 TGA 模式
        self._tcp_path   = None
        self._tcp_header = None
        self._tga_root   = path
        self._tga_index  = index
        max_frames = max(len(v) for v in index.values())
        # 信息卡
        dirs = sorted(index.keys())
        self._tcp_info.setText(
            f"模式 : TGA 序列\n"
            f"目录 : {os.path.basename(path)}\n"
            f"方向 : {len(dirs)} 个 ({','.join(str(d) for d in dirs)})\n"
            f"帧数 : {max_frames} (最长方向)\n"
            f"路径 : {path}"
        )
        # 方向/帧下拉
        self._dir_combo.blockSignals(True)
        self._dir_combo.clear()
        for d in dirs:
            self._dir_combo.addItem(f"{d}", d)
        self._dir_combo.blockSignals(False)
        self._frame_spin.blockSignals(True)
        self._frame_spin.setRange(0, max(0, max_frames - 1))
        # 默认从第 1 帧开始 (= UI 显示 "帧: 1") — 0 帧通常是首帧/起手, 第 1 帧更具代表性
        self._frame_spin.setValue(min(1, max(0, max_frames - 1)))
        self._frame_spin.blockSignals(False)
        # 关联 .pp (同目录)
        existing = None
        # 优先找当前目录, 然后找上级目录 (常见: png/stand/0000.tga 而 .pp 在 png/00.pp)
        for cand_dir in (path, os.path.dirname(path)):
            for fn in ('00.pp', os.path.basename(path) + '.pp'):
                p = os.path.join(cand_dir, fn)
                if os.path.isfile(p):
                    existing = p
                    break
            if existing:
                break
            try:
                for fn in sorted(os.listdir(cand_dir)):
                    if fn.lower().endswith('.pp'):
                        existing = os.path.join(cand_dir, fn)
                        break
            except OSError:
                pass
            if existing:
                break

        if existing:
            try:
                pp = pp_format.read_pp(existing)
                self._project = PPProject.from_pp_file(pp)
                self._pp_path = existing
                self._set_pp_status(f"{existing}  ({len(self._project.adjusts)} 个方案)")
            except Exception as e:
                QMessageBox.warning(self, ".pp 读取失败",
                                    f"找到 {existing} 但读取失败:\n{e}\n\n将创建默认方案.")
                self._project = PPProject.new_default()
                self._pp_path = os.path.join(path, "00.pp")
                self._set_pp_status(f"{self._pp_path}  (新建, 尚未保存)")
        else:
            self._project = PPProject.new_default()
            self._pp_path = os.path.join(path, "00.pp")
            self._set_pp_status(f"{self._pp_path}  (新建, 尚未保存)")

        self._current_scheme_idx = 0
        self._sync_scheme_list()
        self._sync_panel_from_current()
        self._reload_preview(force=False)
        self._sync_action_dirs_after_load(action_name="TGA序列")
        self._update_status(f"已加载 TGA 序列: {os.path.basename(path)}")

    # ───── TCP 加载 ──────────────────────────────────────────────────
    def _do_load_tcp(self):
        path = self._tcp_edit.text().strip()
        if not path:
            path, _ = QFileDialog.getOpenFileName(
                self, "选择 TCP 文件", os.path.expanduser("~"),
                "TCP Sprite (*.tcp);;所有文件 (*)")
            if not path:
                return
            self._tcp_edit.setText(path)
        self._load_tcp_path(path)

    def _load_tcp_path(self, path: str):
        try:
            header = tcp_loader.read_tcp_header(path)
        except tcp_loader.TcpFormatError as e:
            QMessageBox.critical(self, "TCP 解析失败", str(e))
            return
        # 进入 TCP 模式 — 清空 TGA 模式状态
        self._tga_root  = None
        self._tga_index = {}
        self._tcp_path   = path
        self._tcp_header = header

        # 更新 TCP 信息卡
        self._tcp_info.setText(
            f"文件 : {os.path.basename(path)}\n"
            f"版本 : v{header.version}\n"
            f"尺寸 : {header.width} × {header.height}\n"
            f"方向 : {header.num_dirs}\n"
            f"帧数 : {header.num_frames} / 方向\n"
            f"总帧 : {header.total_frames}\n"
            f"锚点 : ({header.center_x}, {header.center_y})\n"
            f"大小 : {header.file_size:,} B"
        )
        # 更新方向 / 帧 上限
        self._dir_combo.blockSignals(True)
        self._dir_combo.clear()
        for i in range(header.num_dirs):
            self._dir_combo.addItem(f"{i}", i)
        self._dir_combo.blockSignals(False)

        self._frame_spin.blockSignals(True)
        self._frame_spin.setRange(0, max(0, header.num_frames - 1))
        # 默认从第 1 帧开始 (与 TGA 序列模式一致)
        self._frame_spin.setValue(min(1, max(0, header.num_frames - 1)))
        self._frame_spin.blockSignals(False)

        # 关联 .pp
        existing = tcp_loader.find_companion_pp(path)
        if existing:
            try:
                pp = pp_format.read_pp(existing)
                self._project = PPProject.from_pp_file(pp)
                self._pp_path = existing
                self._set_pp_status(f"{existing}  ({len(self._project.adjusts)} 个方案)")
            except Exception as e:
                QMessageBox.warning(self, ".pp 读取失败",
                                    f"找到 {existing} 但读取失败:\n{e}\n\n将创建默认方案.")
                self._project = PPProject.new_default()
                self._pp_path = tcp_loader.default_pp_path_for_tcp(path)
                self._set_pp_status(f"{self._pp_path}  (新建, 尚未保存)")
        else:
            self._project = PPProject.new_default()
            self._pp_path = tcp_loader.default_pp_path_for_tcp(path)
            self._set_pp_status(f"{self._pp_path}  (新建, 尚未保存)")

        self._current_scheme_idx = 0
        self._sync_scheme_list()
        self._sync_panel_from_current()
        self._reload_preview(force=False)
        self._sync_action_dirs_after_load(action_name=os.path.splitext(os.path.basename(path))[0])
        self._update_status(f"已加载 TCP: {os.path.basename(path)}")

    # ───── 预览 ──────────────────────────────────────────────────────
    def _reload_preview(self, *, force: bool):
        """根据当前模式 (TCP / TGA 序列) 装载预览图."""
        d_idx = int(self._dir_combo.currentData() or 0)
        f_idx = int(self._frame_spin.value())

        # 模式 1: TGA 序列 — 直接从磁盘读
        if self._tga_root and self._tga_index:
            frames = self._tga_index.get(d_idx, [])
            if not frames:
                self._update_status(f"方向 {d_idx} 没有任何 TGA 帧")
                self._preview_qimage = None
                self._preview_rgba   = None
                self._canvas.set_frame(QImage())
                return
            f_idx = min(f_idx, len(frames) - 1)
            tga = frames[f_idx]
            qimg = load_tga(tga)
            if qimg is None or qimg.isNull():
                QMessageBox.critical(self, "TGA 加载失败", f"无法读取: {tga}")
                self._update_status("TGA 加载失败")
                return
            self._preview_qimage = qimg
            self._preview_rgba   = qimage_to_rgba(qimg)
            self._update_status(f"已加载: {os.path.basename(tga)}  "
                                f"{qimg.width()}x{qimg.height()}")
            self._schedule_refresh()
            return

        # 模式 2: TCP — 调起 tcp_2_tga.exe 解码
        if not self._tcp_path:
            return
        size = int(self._size_combo.currentData() or 500)
        self._update_status(f"正在解码 TCP (size={size}) …")
        QApplication.processEvents()
        try:
            tga = tcp_loader.extract_first_frame_to_tga(
                self._tcp_path, size=size, direction=d_idx, frame=f_idx, force=force)
        except Exception as e:
            traceback.print_exc()
            QMessageBox.critical(self, "TGA 解码失败", str(e))
            self._update_status("解码失败")
            return
        if not tga:
            exe = tcp_loader.find_tcp2tga_exe()
            if not exe:
                QMessageBox.warning(
                    self, "找不到 tcp_2_tga.exe",
                    "请将 tcp_2_tga.exe (含同目录 dll) 放到以下任一位置:\n\n"
                    "  R:\\QAutoEditor\\packages\\res_tools\\bin\\tcp_2_tga.exe\n"
                    "  X:\\DH2-Qauto\\sed_hf_260421\\_unpack_work\\tcp_2_tga_portable\\bin\\\n\n"
                    "或者改用「加载 TGA 序列」按钮直接加载已解码的 TGA 目录.")
            else:
                QMessageBox.warning(
                    self, "解码失败",
                    f"tcp_2_tga.exe 没有产出目标 TGA.\n\n"
                    f"exe={exe}\nTCP={self._tcp_path}\n\n"
                    f"建议: 改用「加载 TGA 序列」按钮, 直接选择已经解码出来的 TGA 目录\n"
                    f"(例如 X:\\DH2-Qauto\\...\\tcp_2_tga_portable\\output_tga\\stand-人\\)")
            self._update_status("解码失败")
            return

        # 加载 TGA
        qimg = load_tga(tga)
        if qimg is None or qimg.isNull():
            QMessageBox.critical(self, "TGA 加载失败", f"无法读取: {tga}")
            self._update_status("TGA 加载失败")
            return
        self._preview_qimage = qimg
        self._preview_rgba   = qimage_to_rgba(qimg)
        self._update_status(f"已解码: {os.path.basename(tga)}  "
                            f"{qimg.width()}x{qimg.height()}")
        self._schedule_refresh()

    def _schedule_refresh(self):
        """合并连续滑块刷新 (debounce 30ms)"""
        if self._refresh_timer.isActive():
            return
        self._refresh_timer.start()

    def _do_refresh_preview(self):
        if self._preview_rgba is None:
            self._canvas.set_frame(self._preview_qimage or QImage())
            return
        adj = self._panel.get_adjust()
        try:
            out = pp_core.apply_pp_adjust(self._preview_rgba, adj)
            self._canvas.set_frame(rgba_to_qimage(out))
        except Exception as e:
            traceback.print_exc()
            print(f"[PP] 应用 PP 失败: {e}")
            # 回退: 直接显示原图
            self._canvas.set_frame(self._preview_qimage or QImage())

    # ───── 方案画廊 ──────────────────────────────────────────────────
    def _sync_scheme_list(self):
        self._scheme_list.blockSignals(True)
        self._scheme_list.clear()
        for i, (name, adj) in enumerate(zip(self._project.names, self._project.adjusts)):
            item = QListWidgetItem(name)
            item.setIcon(QIcon(make_color_swatch(adj, size=32)))
            self._scheme_list.addItem(item)
        if self._project.adjusts:
            self._scheme_list.setCurrentRow(
                min(self._current_scheme_idx, len(self._project.adjusts) - 1))
        self._scheme_list.blockSignals(False)

    def _sync_panel_from_current(self):
        if 0 <= self._current_scheme_idx < len(self._project.adjusts):
            self._panel.set_adjust(self._project.adjusts[self._current_scheme_idx])

    def _on_scheme_select(self, row: int):
        if row < 0 or row >= len(self._project.adjusts):
            return
        # 1) 把 panel 的当前值落回当前方案
        old = self._current_scheme_idx
        if 0 <= old < len(self._project.adjusts):
            self._project.adjusts[old] = self._panel.get_adjust().clone()
        # 2) 切到新方案
        self._current_scheme_idx = row
        self._sync_panel_from_current()
        self._refresh_swatch(old)

    def _on_scheme_rename(self, item: QListWidgetItem):
        row = self._scheme_list.row(item)
        if row < 0:
            return
        old = self._project.names[row]
        new, ok = QInputDialog.getText(
            self, "重命名方案", "新名称:", text=old)
        if ok and new.strip():
            self._project.rename(row, new.strip())
            item.setText(new.strip())

    def _do_add_scheme(self):
        idx = self._project.add_default()
        self._sync_scheme_list()
        self._scheme_list.setCurrentRow(idx)

    def _do_dup_scheme(self):
        # 先把当前 panel 落回
        if 0 <= self._current_scheme_idx < len(self._project.adjusts):
            self._project.adjusts[self._current_scheme_idx] = self._panel.get_adjust().clone()
        idx = self._project.duplicate(self._current_scheme_idx)
        if idx >= 0:
            self._sync_scheme_list()
            self._scheme_list.setCurrentRow(idx)

    def _do_del_scheme(self):
        if len(self._project.adjusts) <= 1:
            QMessageBox.information(self, "提示", "至少保留一个方案.")
            return
        if QMessageBox.question(
                self, "确认", f"删除方案 [{self._project.names[self._current_scheme_idx]}]?"
                ) != QMessageBox.Yes:
            return
        if self._project.remove(self._current_scheme_idx):
            self._current_scheme_idx = max(0, self._current_scheme_idx - 1)
            self._sync_scheme_list()
            self._sync_panel_from_current()

    def _do_rename_scheme(self):
        row = self._scheme_list.currentRow()
        if row >= 0:
            self._on_scheme_rename(self._scheme_list.item(row))

    def _refresh_swatch(self, idx: int):
        if 0 <= idx < self._scheme_list.count():
            adj = self._project.adjusts[idx]
            self._scheme_list.item(idx).setIcon(QIcon(make_color_swatch(adj, size=32)))

    # ───── 滑块变化 ──────────────────────────────────────────────────
    def _on_adjust_change(self):
        """滑块任意变动 → ① 同步到当前方案 ② 防抖刷新预览 ③ 更新色块"""
        idx = self._current_scheme_idx
        if 0 <= idx < len(self._project.adjusts):
            self._project.adjusts[idx] = self._panel.get_adjust().clone()
            self._refresh_swatch(idx)
        self._schedule_refresh()

    # ───── 帧 / 方向 / 尺寸切换 ──────────────────────────────────────
    def _on_size_change(self, _):
        self._reload_preview(force=False)

    def _on_dir_change(self, _):
        self._reload_preview(force=False)

    def _on_frame_change(self, _):
        # 同步当前帧标签
        if hasattr(self, '_frame_label'):
            total = self._frame_spin.maximum() + 1
            self._frame_label.setText(f"{self._frame_spin.value() + 1}/{total}")
        self._reload_preview(force=False)

    # ───── 自动循环播放 (与 LUT变色 模块一致) ─────────────────────────
    def _on_tick(self):
        """定时器触发: 自动推进一帧 → 触发预览刷新 + 更新当前帧标签."""
        max_idx = self._frame_spin.maximum()
        total = max_idx + 1
        if total <= 1:
            # 仅 1 帧时, 把标签也刷一下就够了
            if hasattr(self, '_frame_label'):
                self._frame_label.setText(f"{1 if total else 0}/{total}")
            return
        cur = self._frame_spin.value()
        if self._playing:
            nxt = (cur + 1) % total
            # setValue 会触发 _on_frame_change → _reload_preview
            self._frame_spin.setValue(nxt)
            cur = nxt
        if hasattr(self, '_frame_label'):
            self._frame_label.setText(f"{cur + 1}/{total}")

    def _toggle_play(self):
        self._playing = not self._playing
        self._play_btn.setText("⏸ 暂停" if self._playing else "▶ 播放")

    def _on_fps_changed(self, v: int):
        self._fps = max(1, int(v))
        self._play_timer.setInterval(int(1000 / self._fps))

    # ───── 方向 / 动作 (LUT 模块对齐) ───────────────────────────────
    def _switch_direction(self, idx: int):
        """方向按钮 1-8 被点击."""
        # 同步左侧"方向"下拉 (它是底层数据源)
        # 在 _dir_combo 里找到 data == idx 的项
        for i in range(self._dir_combo.count()):
            if int(self._dir_combo.itemData(i) or -1) == idx:
                self._dir_combo.setCurrentIndex(i)
                break
        # 视觉上把按钮 checked
        if 0 <= idx < len(self._dir_btns):
            self._dir_btns[idx].setChecked(True)

    def _on_dir_mode_changed(self):
        """方向数下拉变化: 自动 / 2 / 4 / 8 — 控制方向按钮启用范围."""
        self._refresh_direction_buttons()

    def _on_action_changed(self, action: str):
        """动作下拉变化 — 暂时无动作目录扫描, 仅记录."""
        self._current_action = action or None
        # 后续接入 _layers 多层时, 这里要重扫方向/帧

    def _refresh_direction_buttons(self):
        """根据当前可用方向 + 方向数下拉, 启用/禁用 1-8 按钮."""
        # 当前 _dir_combo 里的方向 ID 集合 (来自实际加载的素材)
        avail = set()
        for i in range(self._dir_combo.count()):
            avail.add(int(self._dir_combo.itemData(i) or -1))
        # 方向数下拉
        mode = self._dir_mode_combo.currentText()
        if mode == "2方向":
            allow = {0, 4}
        elif mode == "4方向":
            allow = {0, 1, 2, 3}
        elif mode == "8方向":
            allow = set(range(8))
        else:  # 自动
            allow = avail
        for i, btn in enumerate(self._dir_btns):
            btn.setEnabled(i in avail and i in allow)
        # 当前方向若被禁用, 切到第一个可用
        cur = int(self._dir_combo.currentData() or 0)
        if cur not in (avail & allow):
            for i in sorted(avail & allow):
                self._switch_direction(i)
                break

    # ───── 背景 (LUT 模块对齐) ─────────────────────────────────────
    def _pick_bg_color(self):
        c = QColorDialog.getColor(self._bg_color, self, "选择背景颜色")
        if c.isValid():
            self._bg_color = c
            try:
                self._canvas.set_bg_color(c)
            except Exception:
                traceback.print_exc()

    def _pick_bg_image(self):
        path, _ = QFileDialog.getOpenFileName(
            self, "选择背景图片", "",
            "图片 (*.png *.jpg *.jpeg *.bmp *.tga)")
        if path:
            self._bg_image_path = path
            try:
                self._canvas.set_bg_image(path)
            except Exception:
                traceback.print_exc()

    def _clear_bg_image(self):
        self._bg_image_path = ""
        try:
            self._canvas.set_bg_image("")
            self._canvas.set_bg_color(self._bg_color)
        except Exception:
            traceback.print_exc()

    def _sync_action_dirs_after_load(self, action_name: str = ""):
        """素材加载完毕后, 同步动作下拉 + 方向 1-8 按钮 + 当前帧标签."""
        # 动作下拉 (当前阶段还不扫多动作目录, 用单一名占位)
        if hasattr(self, '_action_combo'):
            self._action_combo.blockSignals(True)
            self._action_combo.clear()
            self._action_combo.addItem(action_name or PSEUDO_ACTION)
            self._action_combo.setCurrentIndex(0)
            self._action_combo.blockSignals(False)
            self._current_action = self._action_combo.currentText()
        # 方向按钮可用性
        if hasattr(self, '_dir_btns'):
            self._refresh_direction_buttons()
            cur = int(self._dir_combo.currentData() or 0)
            if 0 <= cur < len(self._dir_btns):
                self._dir_btns[cur].setChecked(True)
        # 当前帧标签
        if hasattr(self, '_frame_label'):
            total = self._frame_spin.maximum() + 1
            self._frame_label.setText(f"{self._frame_spin.value() + 1}/{total}")

    # ───── .pp 导入/保存 ─────────────────────────────────────────────
    def _do_import_pp(self):
        default_dir = os.path.dirname(self._tcp_path) if self._tcp_path \
                      else os.path.expanduser("~")
        path, _ = QFileDialog.getOpenFileName(
            self, "导入 .pp 文件", default_dir,
            "PP 配置文件 (*.pp);;所有文件 (*)")
        if not path:
            return
        try:
            pp = pp_format.read_pp(path)
            self._project = PPProject.from_pp_file(pp)
            self._pp_path = path
            self._current_scheme_idx = 0
            self._sync_scheme_list()
            self._sync_panel_from_current()
            self._set_pp_status(f"{path}  ({len(self._project.adjusts)} 个方案)")
            self._update_status(f"已导入 .pp: {os.path.basename(path)}")
        except Exception as e:
            QMessageBox.critical(self, "导入失败", f"{type(e).__name__}: {e}")

    def _do_save_pp(self):
        if not self._pp_path:
            return self._do_save_pp_as()
        self._write_pp_to(self._pp_path)

    def _do_save_pp_as(self):
        default_dir = os.path.dirname(self._tcp_path) if self._tcp_path \
                      else os.path.expanduser("~")
        default_name = "00.pp"
        if self._tcp_path:
            default_name = os.path.splitext(os.path.basename(self._tcp_path))[0] + ".pp"
        save_path, _ = QFileDialog.getSaveFileName(
            self, "另存为 .pp", os.path.join(default_dir, default_name),
            "PP 配置文件 (*.pp);;所有文件 (*)")
        if not save_path:
            return
        if not save_path.lower().endswith(".pp"):
            save_path += ".pp"
        self._write_pp_to(save_path)

    def _write_pp_to(self, save_path: str):
        # 把 panel 当前值落回当前方案
        idx = self._current_scheme_idx
        if 0 <= idx < len(self._project.adjusts):
            self._project.adjusts[idx] = self._panel.get_adjust().clone()
        try:
            pp = self._project.to_pp_file()
            issues = pp_format.validate(pp)
            if issues:
                QMessageBox.critical(
                    self, "校验失败",
                    "生成的 .pp 文件不合法:\n\n" + "\n".join(issues[:5]))
                return
            pp_format.write_pp(save_path, pp, strict=True)
            # 回读校验
            pp_back = pp_format.read_pp(save_path)
            if len(pp_back.regions) != len(pp.regions):
                QMessageBox.warning(
                    self, "回读不一致",
                    f"已保存但回读 region 数对不上 ({len(pp_back.regions)} vs {len(pp.regions)}).")
            self._pp_path = save_path
            self._set_pp_status(f"{save_path}  ({len(pp.regions)} 个方案, 已保存)")
            self._update_status(f"已保存: {save_path}")
        except Exception as e:
            traceback.print_exc()
            QMessageBox.critical(self, "保存失败", f"{type(e).__name__}: {e}")

    # ───── 工具 ──────────────────────────────────────────────────────
    def _do_clear_cache(self):
        n = tcp_loader.clear_tcp_cache()
        QMessageBox.information(self, "缓存已清", f"删除了 {n} 个缓存目录.")
        self._update_status("TGA 缓存已清空")

    def _do_open_folder(self):
        path = self._tcp_path or self._pp_path
        if not path:
            return
        folder = os.path.dirname(path)
        if os.path.isdir(folder):
            os.startfile(folder)

    # ───── 状态栏 ───────────────────────────────────────────────────
    def _set_pp_status(self, text: str):
        self._pp_label.setText(text)
        self._pp_label.setStyleSheet("color: #ddd;")

    # ───── 主窗口尺寸: 由主程序 (Tool_Development.py) 切到本页面后调用 ─────
    # 截图基准: 主窗口约 1370 × 910, 居中
    DEFAULT_WIN_W = 1370
    DEFAULT_WIN_H = 910

    def restore_saved_window_geometry(self, main_window=None):
        """主窗口挂载本页面后调用. 把主窗口设到 PP 工具的"出厂默认尺寸".

        返回 True 表示成功接管了窗口尺寸 (主程序就不再走 _WIDE_APPS 的兜底拉宽).

        防御: 任何步骤抛异常都不能让页面构造/切换失败.
        """
        if main_window is None:
            return False
        try:
            from PyQt5.QtCore import QRect
            from PyQt5.QtWidgets import QApplication
            screen = QApplication.primaryScreen()
            if not screen:
                main_window.resize(self.DEFAULT_WIN_W, self.DEFAULT_WIN_H)
                return True
            avail = screen.availableGeometry()
            w = min(self.DEFAULT_WIN_W, avail.width()  - 40)
            h = min(self.DEFAULT_WIN_H, avail.height() - 40)
            x = avail.x() + (avail.width()  - w) // 2
            y = avail.y() + (avail.height() - h) // 2
            main_window.setGeometry(QRect(x, y, w, h))
            return True
        except Exception:
            return False

    def _update_status(self, msg: str = ""):
        if not msg:
            scheme_n = len(self._project.adjusts)
            cur = self._current_scheme_idx + 1 if scheme_n else 0
            if self._tcp_path:
                src = "TCP=" + os.path.basename(self._tcp_path)
            elif self._tga_root:
                src = "TGA=" + os.path.basename(self._tga_root)
            else:
                src = "(未加载素材)"
            msg = f"{src}  |  当前方案 {cur}/{scheme_n}"
        self._status_label.setText(msg)


# ═══════════════════════════════════════════════════════════════════
# Tab 2: TGA 变色输出 — 批量把 TGA 序列变色后保存为 PNG (1xxx 起始编号)
# ═══════════════════════════════════════════════════════════════════
from PyQt5.QtWidgets import (
    QTabWidget, QProgressBar, QPlainTextEdit,
)
from PyQt5.QtCore import QThread, pyqtSignal


class _TgaExportWorker(QThread):
    """
    后台批量导出线程: 把源目录下所有 TGA 依 PP 调色后保存为 PNG.
    
    两种模式:
      keep_structure=False: 输出到 输出目录 根下, 文件名 = 起始编号 + 递增
      keep_structure=True:  保持原目录结构, 每个子文件夹单独编号

    signals:
        progress(done, total, cur_name) — 每张完成触发
        message (text, level)            — text/log (level: "info"/"warn"/"error")
        finished_ok(done, total, out_dir) — 正常完成
        failed  (text)                   — 异常中止
    """
    progress    = pyqtSignal(int, int, str)
    message     = pyqtSignal(str, str)
    finished_ok = pyqtSignal(int, int, str)
    failed      = pyqtSignal(str)

    def __init__(self, src_dir: str, out_dir: str, adj: pp_core.PPAdjust,
                 start_num: int = 1000, overwrite: bool = True,
                 keep_structure: bool = False, actions_index: dict = None,
                 parent=None):
        super().__init__(parent)
        self._src_dir   = src_dir
        self._out_dir   = out_dir
        self._adj       = adj.clone()   # 快照, 避免线程间共享
        self._start_num = int(start_num)
        self._overwrite = bool(overwrite)
        self._keep_structure = bool(keep_structure)
        self._actions_index = actions_index or {}
        self._cancel    = False

    def cancel(self):
        self._cancel = True

    # ───── 扫描: 返回全部 TGA/PNG 路径 (按 方向, 帧 排好序) ─────
    @staticmethod
    def scan_tga_list(src_dir: str) -> List[str]:
        """
        扫描 src_dir 下 [方向][帧号].(tga|png) 文件, 返回按 (方向, 帧) 排序的路径列表.
        若没有匹配命名, 则退化为纯文件名字典序 (兼容其它命名的 TGA/PNG).

        支持的扩展名: .tga, .png  (大小写不敏感)
        """
        if not src_dir or not os.path.isdir(src_dir):
            return []
        # 同时匹配 .tga / .png  (例如 0000.tga / 1003.png / 7011.tga)
        tga_re = re.compile(r'^(\d)(\d{3})\.(tga|png)$', re.IGNORECASE)
        valid_exts = ('.tga', '.png')
        matched: List[tuple] = []
        others:  List[str]   = []
        try:
            for fn in os.listdir(src_dir):
                full = os.path.join(src_dir, fn)
                if not os.path.isfile(full):
                    continue
                if not fn.lower().endswith(valid_exts):
                    continue
                m = tga_re.match(fn)
                if m:
                    matched.append((int(m.group(1)), int(m.group(2)), full))
                else:
                    others.append(full)
        except OSError:
            return []
        if matched:
            matched.sort(key=lambda x: (x[0], x[1]))
            return [p for _, _, p in matched]
        others.sort()
        return others

    def run(self):
        try:
            # 保持目录结构模式
            if self._keep_structure and self._actions_index:
                self._run_structured()
                return

            # 普通平铺模式
            tga_list = self.scan_tga_list(self._src_dir)
            total = len(tga_list)
            if total == 0:
                self.failed.emit("源目录未找到任何 .tga / .png 文件")
                return

            try:
                os.makedirs(self._out_dir, exist_ok=True)
            except OSError as e:
                self.failed.emit(f"创建输出目录失败: {e}")
                return

            self.message.emit(f"开始处理: {total} 张图片 → PNG", "info")
            done = 0
            for i, tga_path in enumerate(tga_list):
                if self._cancel:
                    self.message.emit("已取消.", "warn")
                    break
                num     = self._start_num + i
                out_name = f"{num}.png"
                out_fp   = os.path.join(self._out_dir, out_name)

                if not self._overwrite and os.path.exists(out_fp):
                    self.message.emit(f"跳过已存在: {out_name}", "warn")
                    self.progress.emit(i + 1, total, out_name)
                    continue

                try:
                    q = load_tga(tga_path)
                    if q is None or q.isNull():
                        self.message.emit(
                            f"无法读取: {os.path.basename(tga_path)}", "error")
                        self.progress.emit(i + 1, total, out_name)
                        continue
                    # RGBA8888 统一, 方便 numpy 处理
                    if q.format() != QImage.Format_RGBA8888:
                        q = q.convertToFormat(QImage.Format_RGBA8888)
                    arr = qimage_to_rgba(q)
                    if arr is None:
                        self.message.emit(
                            f"RGBA 转换失败: {os.path.basename(tga_path)}", "error")
                        self.progress.emit(i + 1, total, out_name)
                        continue
                    out_arr = pp_core.apply_pp_adjust(arr, self._adj)
                    out_qi  = rgba_to_qimage(out_arr)
                    if out_qi.isNull():
                        self.message.emit(
                            f"导出 QImage 失败: {os.path.basename(tga_path)}", "error")
                        self.progress.emit(i + 1, total, out_name)
                        continue
                    if not out_qi.save(out_fp, "PNG"):
                        self.message.emit(
                            f"PNG 保存失败: {out_name}", "error")
                        self.progress.emit(i + 1, total, out_name)
                        continue
                    done += 1
                except Exception as e:
                    self.message.emit(
                        f"[{os.path.basename(tga_path)}] 处理异常: {e}", "error")
                self.progress.emit(i + 1, total, out_name)

            self.finished_ok.emit(done, total, self._out_dir)
        except Exception as e:
            self.failed.emit(f"未预期错误: {e}\n{traceback.format_exc()}")

    def _run_structured(self):
        """保持目录结构导出: 每个子文件夹单独编号."""
        # 统计总量
        total = 0
        for action_name, dirs_dict in self._actions_index.items():
            for dir_id, frames in dirs_dict.items():
                total += len(frames)
        if total == 0:
            self.failed.emit("未找到任何图片")
            return

        self.message.emit(f"开始处理: {len(self._actions_index)} 个子目录, 共 {total} 张图片", "info")

        done = 0
        processed = 0
        for action_name, dirs_dict in sorted(self._actions_index.items()):
            if self._cancel:
                self.message.emit("已取消.", "warn")
                break

            # 为每个子文件夹创建输出目录
            sub_out_dir = os.path.join(self._out_dir, action_name)
            try:
                os.makedirs(sub_out_dir, exist_ok=True)
            except OSError as e:
                self.message.emit(f"创建 {action_name} 目录失败: {e}", "error")
                continue

            # 收集该子目录所有帧 (按方向/帧排序)
            all_frames = []
            for dir_id in sorted(dirs_dict.keys()):
                for fp in dirs_dict[dir_id]:
                    all_frames.append(fp)

            self.message.emit(f"处理子目录 [{action_name}]: {len(all_frames)} 张", "info")

            for i, tga_path in enumerate(all_frames):
                if self._cancel:
                    break
                num = self._start_num + i
                out_name = f"{num}.png"
                out_fp = os.path.join(sub_out_dir, out_name)

                processed += 1

                if not self._overwrite and os.path.exists(out_fp):
                    self.message.emit(f"跳过: {action_name}/{out_name}", "warn")
                    self.progress.emit(processed, total, f"{action_name}/{out_name}")
                    continue

                try:
                    q = load_tga(tga_path)
                    if q is None or q.isNull():
                        self.message.emit(f"无法读取: {action_name}/{os.path.basename(tga_path)}", "error")
                        self.progress.emit(processed, total, f"{action_name}/{out_name}")
                        continue
                    if q.format() != QImage.Format_RGBA8888:
                        q = q.convertToFormat(QImage.Format_RGBA8888)
                    arr = qimage_to_rgba(q)
                    if arr is None:
                        self.message.emit(f"RGBA 转换失败: {action_name}/{os.path.basename(tga_path)}", "error")
                        self.progress.emit(processed, total, f"{action_name}/{out_name}")
                        continue
                    out_arr = pp_core.apply_pp_adjust(arr, self._adj)
                    out_qi = rgba_to_qimage(out_arr)
                    if out_qi.isNull():
                        self.message.emit(f"QImage 失败: {action_name}/{out_name}", "error")
                        self.progress.emit(processed, total, f"{action_name}/{out_name}")
                        continue
                    if not out_qi.save(out_fp, "PNG"):
                        self.message.emit(f"PNG 保存失败: {action_name}/{out_name}", "error")
                        self.progress.emit(processed, total, f"{action_name}/{out_name}")
                        continue
                    done += 1
                except Exception as e:
                    self.message.emit(f"[{action_name}/{os.path.basename(tga_path)}] 异常: {e}", "error")
                self.progress.emit(processed, total, f"{action_name}/{out_name}")

        self.finished_ok.emit(done, total, self._out_dir)


class PPExportPage(PPPipelinePage):
    """Tab 2: TGA/PNG 批量变色输出 PNG (双画布: 本体 + 变色后)."""
    _LAYOUT_KEY = "pp_pipeline_tab2"

    def __init__(self, parent=None):
        self._export_worker: Optional[_TgaExportWorker] = None
        # 第二画布 (变色后预览), 在 _build_right_panel 中创建
        self._canvas2: Optional[PreviewCanvas] = None
        super().__init__(parent)
        # 父类 __init__ 完成后, 对继承来的 UI 做差异化改造
        self._post_init_customize()

    # 父类 __init__ 完成后调用: 标签改名 + 新增按钮
    def _post_init_customize(self):
        # (1) "素材:" -> "源目录:"
        for lbl in self.findChildren(QLabel):
            if lbl.text() == "素材:":
                lbl.setText("源目录:")
                break
        # (2) 路径框 placeholder
        try:
            self._tcp_edit.setPlaceholderText("选择含 .tga / .png 序列的目录")
        except Exception:
            pass
        # (3) 加载按钮文案
        try:
            self._load_tga_btn.setText("加载 TGA/PNG 序列")
        except Exception:
            pass
        # (4) 在 [导入.pp] 前 插入 [PNG保存PP文件]
        self._pp_save_from_dir_btn = QPushButton("PNG保存PP文件")
        self._pp_save_from_dir_btn.setMinimumWidth(120)
        self._pp_save_from_dir_btn.setToolTip(
            "根据 [源目录] 末段文件夹名生成同名 .pp 保存到该目录下")
        self._pp_save_from_dir_btn.clicked.connect(self._on_save_pp_from_png_dir)
        # 找到 _import_pp_btn 所在的 layout (遍历所有 QHBoxLayout)
        inserted = False
        for child in self.findChildren(QHBoxLayout):
            idx = child.indexOf(self._import_pp_btn)
            if idx >= 0:
                child.insertWidget(idx, self._pp_save_from_dir_btn)
                inserted = True
                break
        if not inserted:
            print("[PPExportPage] 未找到 _import_pp_btn 所在 layout, 按钮未插入")

    # 重写: 兼容 .tga + .png 两种扩展名 + 多动作子目录 (stand/walk/...)
    def _do_load_tga(self):
        path = self._tcp_edit.text().strip()
        if not path or not os.path.isdir(path):
            path = QFileDialog.getExistingDirectory(
                self, "选择 TGA/PNG 序列目录",
                os.path.dirname(self._tcp_path or self._pp_path or os.path.expanduser("~")))
            if not path:
                return
            self._tcp_edit.setText(path)

        pattern = re.compile(r'^(\d)(\d{3})\.(tga|png)$', re.IGNORECASE)

        # 先检测多动作结构 (子目录 stand/walk 等)
        actions_index = {}  # {action_name: {dir_id: [paths...]}}
        try:
            for entry in os.listdir(path):
                sub = os.path.join(path, entry)
                if os.path.isdir(sub):
                    sub_index = self._scan_dir_for_frames(sub, pattern)
                    if sub_index:
                        actions_index[entry] = sub_index
        except OSError:
            pass

        # 如果没有多动作子目录, 检查根目录是否有直接的帧
        root_index = self._scan_dir_for_frames(path, pattern)
        if root_index and not actions_index:
            actions_index["序列"] = root_index

        if not actions_index:
            QMessageBox.warning(self, "未找到序列",
                "在 " + path + " 及其子目录下未找到 [方向][帧号].tga / .png 的文件")
            return

        # 保存到实例
        self._actions_index = actions_index
        self._tcp_path   = None
        self._tcp_header = None
        self._tga_root   = path

        # 填充动作下拉
        action_names = sorted(actions_index.keys())
        self._action_combo.blockSignals(True)
        self._action_combo.clear()
        for a in action_names:
            self._action_combo.addItem(a)
        self._action_combo.blockSignals(False)

        # 默认选第一个动作
        first_action = action_names[0]
        self._tga_index = actions_index[first_action]

        # 刷新方向/帧信息
        self._refresh_dir_frame_from_action(first_action)

        # 关联 .pp
        self._associate_pp_from_path(path)

        self._current_scheme_idx = 0
        self._sync_scheme_list()
        self._sync_panel_from_current()
        self._reload_preview(force=False)
        self._update_status("已加载: " + os.path.basename(path) + " (" + str(len(action_names)) + " 个动作)")

    def _scan_dir_for_frames(self, dir_path, pattern):
        """
        扫描单个目录, 返回 {dir_id: [frame_paths...]} 或 None.
        支持两种命名:
          1) [方向][帧号].tga  如 0000.tga, 1003.tga  (pattern 参数)
          2) 纯帧号.tga       如 0000.tga, 0001.tga  (4 位数字, 无方向前缀)
        """
        index = {}
        pure_frame_pattern = re.compile(r'^(\d{4})\.(tga|png)$', re.IGNORECASE)
        try:
            for fn in os.listdir(dir_path):
                # 先用 [方向][帧号] pattern
                m = pattern.match(fn)
                if m:
                    d = int(m.group(1))
                    f = int(m.group(2))
                    index.setdefault(d, []).append((f, os.path.join(dir_path, fn)))
                    continue
                # 再用纯帧号 pattern (方向固定为 0)
                m2 = pure_frame_pattern.match(fn)
                if m2:
                    f = int(m2.group(1))
                    index.setdefault(0, []).append((f, os.path.join(dir_path, fn)))
        except OSError:
            return None
        if not index:
            return None
        for d in index:
            index[d].sort(key=lambda x: x[0])
            index[d] = [p for _, p in index[d]]
        return index

    def _refresh_dir_frame_from_action(self, action_name):
        """根据动作名刷新方向下拉/帧 spin/信息卡"""
        index = self._actions_index.get(action_name, {})
        self._tga_index = index
        dirs = sorted(index.keys())
        max_frames = max((len(v) for v in index.values()), default=0)

        self._tcp_info.setText(
            "模式 : TGA/PNG 序列\n"
            "动作 : " + action_name + "\n"
            "方向 : " + str(len(dirs)) + " 个 (" + ",".join(str(d) for d in dirs) + ")\n"
            "帧数 : " + str(max_frames) + " (最长方向)\n"
            "路径 : " + (self._tga_root or "")
        )
        self._dir_combo.blockSignals(True)
        self._dir_combo.clear()
        for d in dirs:
            self._dir_combo.addItem(str(d), d)
        self._dir_combo.blockSignals(False)
        self._frame_spin.blockSignals(True)
        self._frame_spin.setRange(0, max(0, max_frames - 1))
        self._frame_spin.setValue(min(1, max(0, max_frames - 1)))
        self._frame_spin.blockSignals(False)

    def _associate_pp_from_path(self, path):
        """关联 .pp (同目录优先, 然后上级)"""
        existing = None
        for cand_dir in (path, os.path.dirname(path)):
            for fn in ('00.pp', os.path.basename(path) + '.pp'):
                p = os.path.join(cand_dir, fn)
                if os.path.isfile(p):
                    existing = p
                    break
            if existing:
                break
            try:
                for fn in sorted(os.listdir(cand_dir)):
                    if fn.lower().endswith('.pp'):
                        existing = os.path.join(cand_dir, fn)
                        break
            except OSError:
                pass
            if existing:
                break
        if existing:
            try:
                pp = pp_format.read_pp(existing)
                self._project = PPProject.from_pp_file(pp)
                self._pp_path = existing
                self._set_pp_status(existing + "  (" + str(len(self._project.adjusts)) + " 个方案)")
            except Exception as e:
                QMessageBox.warning(self, ".pp 读取失败",
                    "找到 " + existing + " 但读取失败:\n" + str(e) + "\n\n将创建默认方案.")
                self._project = PPProject.new_default()
                self._pp_path = os.path.join(path, os.path.basename(path) + ".pp")
                self._set_pp_status(self._pp_path + "  (新建, 尚未保存)")
        else:
            self._project = PPProject.new_default()
            self._pp_path = os.path.join(path, os.path.basename(path) + ".pp")
            self._set_pp_status(self._pp_path + "  (新建, 尚未保存)")

    # 动作下拉切换时刷新
    def _on_action_changed(self, action_name):
        if hasattr(self, '_actions_index') and action_name in self._actions_index:
            self._refresh_dir_frame_from_action(action_name)
            self._reload_preview(force=False)

    # 重写: 在原右栏末尾追加导出控件 + 第二画布(变色后)
    def _build_right_panel(self) -> QWidget:
        w = super()._build_right_panel()
        lay = w.layout()
        if lay is None:
            return w

        # 双画布化: 把 super 的 self._canvas 替换为 [本体|变色] 横向 splitter
        self._wrap_canvas_with_secondary(lay)

        # ── 分隔线 ──
        sep = QFrame()
        sep.setFrameShape(QFrame.HLine)
        sep.setFrameShadow(QFrame.Sunken)
        sep.setStyleSheet("QFrame { color: #3a3a3a; }")
        lay.addWidget(sep)

        # ── 批量导出 PNG 分组 ──
        gb = QGroupBox("批量导出 PNG 序列")
        gb.setStyleSheet(
            "QGroupBox { font: bold 10pt 'Microsoft YaHei UI'; "
            "border: 1px solid #4a4a4a; border-radius: 4px; "
            "margin-top: 10px; padding-top: 10px; }"
            "QGroupBox::title { subcontrol-origin: margin; "
            "left: 10px; padding: 0 6px; color: #e0c070; }"
        )
        gl = QVBoxLayout(gb)
        gl.setContentsMargins(8, 10, 8, 8)
        gl.setSpacing(5)

        # 输出目录
        row_out = QHBoxLayout()
        row_out.setSpacing(4)
        row_out.addWidget(QLabel("输出目录:"))
        self._export_out_edit = PathLineEdit()
        self._export_out_edit.setPlaceholderText("PNG 将平铺在此目录根下 (必填)")
        row_out.addWidget(self._export_out_edit, 1)
        btn_out = QPushButton("浏览…")
        btn_out.setFixedWidth(60)
        btn_out.clicked.connect(self._on_export_pick_out)
        row_out.addWidget(btn_out)
        btn_open = QPushButton("打开")
        btn_open.setFixedWidth(50)
        btn_open.setToolTip("在资源管理器中打开输出目录")
        btn_open.clicked.connect(self._on_export_open_out)
        row_out.addWidget(btn_open)
        gl.addLayout(row_out)

        # 起始编号 + 覆盖
        row_opt = QHBoxLayout()
        row_opt.addWidget(QLabel("起始编号:"))
        self._export_start_spin = QSpinBox()
        self._export_start_spin.setRange(0, 99999)
        self._export_start_spin.setValue(1000)
        self._export_start_spin.setFixedWidth(80)
        self._export_start_spin.setToolTip(
            "第 1 张 PNG 的文件名 (纯数字). 每张 +1 递增.\n"
            "1000 → 1000.png, 1001.png, 1002.png …")
        row_opt.addWidget(self._export_start_spin)
        row_opt.addSpacing(12)
        self._export_overwrite_chk = QCheckBox("覆盖同名")
        self._export_overwrite_chk.setChecked(True)
        self._export_overwrite_chk.setToolTip(
            "勾选: 同名文件直接覆盖\n不勾: 遇同名则跳过")
        row_opt.addWidget(self._export_overwrite_chk)
        row_opt.addStretch(1)
        btn_scan = QPushButton("扫描")
        btn_scan.setFixedWidth(60)
        btn_scan.setToolTip("预估将输出多少张 PNG (基于顶部 TGA 目录)")
        btn_scan.clicked.connect(self._on_export_scan)
        row_opt.addWidget(btn_scan)
        gl.addLayout(row_opt)

        # 开始 / 取消
        row_run = QHBoxLayout()
        self._export_run_btn = QPushButton("▶ 开始导出 PNG 序列")
        self._export_run_btn.setMinimumHeight(30)
        self._export_run_btn.setStyleSheet(
            "QPushButton { background:#2a7a2a; color:white; "
            "border-radius:3px; font: bold 10pt 'Microsoft YaHei UI'; }"
            "QPushButton:hover { background:#338033; }"
            "QPushButton:disabled { background:#3a3a3a; color:#888; }")
        self._export_run_btn.clicked.connect(self._on_export_run)
        row_run.addWidget(self._export_run_btn, 1)
        self._export_cancel_btn = QPushButton("取消")
        self._export_cancel_btn.setFixedWidth(60)
        self._export_cancel_btn.setMinimumHeight(30)
        self._export_cancel_btn.setEnabled(False)
        self._export_cancel_btn.clicked.connect(self._on_export_cancel)
        row_run.addWidget(self._export_cancel_btn)
        gl.addLayout(row_run)

        # 进度条
        self._export_progress = QProgressBar()
        self._export_progress.setRange(0, 1)
        self._export_progress.setValue(0)
        self._export_progress.setFormat("就绪")
        self._export_progress.setMinimumHeight(18)
        gl.addWidget(self._export_progress)

        # 日志
        self._export_log = QPlainTextEdit()
        self._export_log.setReadOnly(True)
        self._export_log.setMaximumBlockCount(500)
        self._export_log.setMaximumHeight(120)
        self._export_log.setStyleSheet(
            "QPlainTextEdit { background:#1b1b1b; color:#d0d0d0; "
            "border:1px solid #333; border-radius:3px; padding:4px; "
            "font: 9pt 'Consolas'; }")
        gl.addWidget(self._export_log)

        lay.addWidget(gb)
        return w

    # ───── 工具函数 ──────────────────────────────────────────────────
    def _export_append_log(self, text: str, level: str = "info"):
        color = {
            "info":  "#c0c0c0",
            "warn":  "#e5c07b",
            "error": "#e06c75",
            "ok":    "#98c379",
        }.get(level, "#c0c0c0")
        self._export_log.appendHtml(f'<span style="color:{color};">{text}</span>')

    def _export_resolve_src_dir(self) -> Optional[str]:
        """
        取导出用的源 TGA 目录:
            优先: self._tga_root (通过 [加载 TGA 序列] 按钮加载过的目录)
            其次: 顶部输入框 self._tcp_edit.text() 若指向目录
            否则: None
        """
        if self._tga_root and os.path.isdir(self._tga_root):
            return self._tga_root
        txt = self._tcp_edit.text().strip() if hasattr(self, "_tcp_edit") else ""
        if txt and os.path.isdir(txt):
            return txt
        return None

    # ───── 按钮回调 ──────────────────────────────────────────────────
    def _on_export_pick_out(self):
        start = self._export_out_edit.text().strip() or os.path.expanduser("~")
        path = QFileDialog.getExistingDirectory(self, "选择 PNG 输出目录", start)
        if path:
            self._export_out_edit.setText(path)

    def _on_export_open_out(self):
        path = self._export_out_edit.text().strip()
        if not path:
            QMessageBox.information(self, "提示", "请先填写输出目录.")
            return
        try:
            os.makedirs(path, exist_ok=True)
            os.startfile(path)   # Windows
        except Exception as e:
            QMessageBox.warning(self, "打开失败", str(e))

    def _on_export_scan(self):
        src = self._export_resolve_src_dir()
        if not src:
            QMessageBox.warning(
                self, "未加载 TGA 目录",
                "请先在顶部 [加载 TGA 序列] 一个含 .tga 的目录.")
            return
        tga_list = _TgaExportWorker.scan_tga_list(src)
        n = len(tga_list)
        if n == 0:
            self._export_append_log(
                f"扫描: [{src}] 未找到任何 .tga", "warn")
            QMessageBox.information(
                self, "扫描结果",
                f"目录下未找到 .tga 文件:\n{src}")
            return
        first = os.path.basename(tga_list[0])
        last  = os.path.basename(tga_list[-1])
        start_n = self._export_start_spin.value()
        end_n   = start_n + n - 1
        self._export_append_log(
            f"扫描: 发现 {n} 张 TGA  ({first} … {last})  → "
            f"{start_n}.png … {end_n}.png", "ok")
        QMessageBox.information(
            self, "扫描结果",
            f"源目录:\n  {src}\n\n"
            f"共 {n} 张 TGA\n首: {first}\n末: {last}\n\n"
            f"将输出:\n  {start_n}.png … {end_n}.png")

    def _on_export_run(self):
        if self._export_worker is not None and self._export_worker.isRunning():
            return
        src = self._export_resolve_src_dir()
        if not src:
            QMessageBox.warning(
                self, "未加载 TGA 目录",
                "请先在顶部 [加载 TGA 序列] 加载一个含 .tga 的目录.")
            return
        out = self._export_out_edit.text().strip()
        if not out:
            QMessageBox.warning(self, "输出目录未填", "请先填写输出目录.")
            return
        try:
            if os.path.abspath(src) == os.path.abspath(out):
                QMessageBox.warning(
                    self, "目录冲突",
                    "源目录 与 输出目录 不能相同 — 请另选输出目录.")
                return
        except Exception:
            pass
        # 输出目录非空时提醒
        if os.path.isdir(out):
            try:
                existing = [f for f in os.listdir(out)
                            if f.lower().endswith('.png')]
                if existing:
                    r = QMessageBox.question(
                        self, "输出目录非空",
                        f"输出目录下已有 {len(existing)} 个 PNG 文件.\n\n"
                        f"继续导出?  (覆盖选项: "
                        f"{'覆盖' if self._export_overwrite_chk.isChecked() else '跳过'})")
                    if r != QMessageBox.Yes:
                        return
            except OSError:
                pass
        # 当前面板的 14 字段 (所见即所得)
        try:
            adj = self._panel.get_adjust()
        except Exception as e:
            QMessageBox.critical(self, "读取调色参数失败", str(e))
            return

        self._export_log.clear()
        self._export_append_log("─── 开始 ───", "ok")
        self._export_append_log(f"源目录 : {src}")
        self._export_append_log(f"输出   : {out}")
        self._export_append_log(f"起编号 : {self._export_start_spin.value()}")
        brief = (
            f"[{self._project.names[self._current_scheme_idx]}]  "
            f"M=[{adj.rr},{adj.rg},{adj.rb}|{adj.gr},{adj.gg},{adj.gb}|"
            f"{adj.br},{adj.bg},{adj.bb}]  "
            f"H={adj.h} S={adj.s} L={adj.l}  Lb={adj.lb} C={adj.c}"
        )
        self._export_append_log(f"调色   : {brief}")

        self._export_progress.setRange(0, 0)
        self._export_progress.setFormat("扫描中…")

        # 判断是否需要保持目录结构
        # 条件: actions_index 有多个子目录 或 有非 "序列" 的子目录
        keep_structure = False
        actions_index = getattr(self, '_actions_index', {})
        if len(actions_index) > 1 or (len(actions_index) == 1 and "序列" not in actions_index):
            keep_structure = True

        self._export_worker = _TgaExportWorker(
            src_dir   = src,
            out_dir   = out,
            adj       = adj,
            start_num = self._export_start_spin.value(),
            overwrite = self._export_overwrite_chk.isChecked(),
            keep_structure = keep_structure,
            actions_index = actions_index if keep_structure else None,
        )
        self._export_worker.progress.connect(self._on_export_progress)
        self._export_worker.message.connect(self._export_append_log)
        self._export_worker.finished_ok.connect(self._on_export_finished_ok)
        self._export_worker.failed.connect(self._on_export_failed)
        self._export_worker.finished.connect(self._on_export_thread_done)
        self._export_set_running(True)
        self._export_worker.start()

    def _on_export_cancel(self):
        if self._export_worker is not None and self._export_worker.isRunning():
            self._export_worker.cancel()
            self._export_append_log("收到取消请求…", "warn")

    def _on_export_progress(self, done: int, total: int, cur_name: str):
        if self._export_progress.maximum() != total:
            self._export_progress.setRange(0, max(1, total))
        self._export_progress.setValue(done)
        self._export_progress.setFormat(f"{done} / {total}  ({cur_name})")

    def _on_export_finished_ok(self, done: int, total: int, out_dir: str):
        self._export_append_log(f"─── 完成: 成功 {done} / {total} ───", "ok")
        self._export_append_log(f"输出: {out_dir}")

    def _on_export_failed(self, text: str):
        self._export_append_log(f"─── 失败: {text} ───", "error")
        self._export_progress.setRange(0, 1)
        self._export_progress.setValue(0)
        self._export_progress.setFormat("失败")

    def _on_export_thread_done(self):
        self._export_set_running(False)

    def _export_set_running(self, running: bool):
        self._export_run_btn.setEnabled(not running)
        self._export_cancel_btn.setEnabled(running)

    # 按源目录最末段文件夹名生成 .pp 并保存到该目录下
    def _on_save_pp_from_png_dir(self):
        src_dir = (self._tga_root or "").strip()
        if not src_dir:
            src_dir = (self._tcp_edit.text() or "").strip()
        if not src_dir or not os.path.isdir(src_dir):
            QMessageBox.warning(self, "未设置源目录",
                "请先在顶部选择一个目录, 或点 [加载 TGA/PNG 序列].")
            return
        folder_name = os.path.basename(os.path.normpath(src_dir))
        if not folder_name:
            QMessageBox.warning(self, "目录名异常", "无法取末段文件夹名: " + src_dir)
            return
        target_pp = os.path.join(src_dir, folder_name + ".pp")
        if os.path.exists(target_pp):
            box = QMessageBox(self)
            box.setIcon(QMessageBox.Question)
            box.setWindowTitle("文件已存在")
            box.setText("目标文件已存在:\n" + target_pp + "\n\n请选择操作:")
            btn_ov = box.addButton("覆盖", QMessageBox.AcceptRole)
            btn_rn = box.addButton("另存为新名", QMessageBox.ActionRole)
            btn_cn = box.addButton("取消", QMessageBox.RejectRole)
            box.exec_()
            chosen = box.clickedButton()
            if chosen is btn_cn:
                return
            if chosen is btn_rn:
                base = os.path.splitext(target_pp)[0]
                i = 2
                while os.path.exists(base + "_" + str(i) + ".pp"):
                    i += 1
                target_pp = base + "_" + str(i) + ".pp"
        try:
            self._write_pp_to(target_pp)
            self._pp_path = target_pp
            self._set_pp_status(target_pp + "  (" + str(len(self._project.adjusts)) + " 个方案)")
            self._update_status("已按源目录名保存: " + os.path.basename(target_pp))
        except Exception as e:
            QMessageBox.critical(self, "保存失败", str(e))

    # 把父类创建的 self._canvas 替换为 [本体 | 变色后] 横向 splitter
    def _wrap_canvas_with_secondary(self, lay):
        canvas_idx = lay.indexOf(self._canvas)
        if canvas_idx < 0:
            return
        # 1) 创建第二画布 (变色后预览)
        self._canvas2 = PreviewCanvas()
        self._canvas2.setMinimumSize(420, 420)
        self._canvas2.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)
        try:
            self._canvas2.set_bg_color(self._bg_color)
        except Exception:
            pass
        # 2) 用 splitter 包起来
        sp = QSplitter(Qt.Horizontal)
        sp.setChildrenCollapsible(False)
        sp.setHandleWidth(4)
        sp.addWidget(self._canvas)
        sp.addWidget(self._canvas2)
        sp.setSizes([500, 500])
        # 3) 替换 layout 里的 canvas
        lay.removeWidget(self._canvas)
        lay.insertWidget(canvas_idx, sp, 1)
        # 注册到 LayoutMixin (供保存/恢复)
        if not hasattr(self, '_layout_splitters'):
            self._layout_splitters = []
        self._layout_splitters.append(sp)

    # 重写: 同时刷新本体画布(原图) + 变色画布(应用 PP 后)
    def _do_refresh_preview(self):
        # canvas1: 本体 = 原始 RGBA, 不应用 PP
        if self._preview_qimage is not None:
            try:
                self._canvas.set_frame(self._preview_qimage)
            except Exception:
                pass
        # canvas2: 变色后 = apply_pp_adjust
        self._refresh_canvas2()

    def _refresh_canvas2(self):
        if self._canvas2 is None:
            return
        if self._preview_rgba is None:
            try:
                self._canvas2.set_frame(self._preview_qimage or QImage())
            except Exception:
                pass
            return
        adj = self._panel.get_adjust()
        try:
            out = pp_core.apply_pp_adjust(self._preview_rgba, adj)
            self._canvas2.set_frame(rgba_to_qimage(out))
        except Exception as e:
            traceback.print_exc()
            print("[PP-Tab2] 应用 PP 失败: " + str(e))

    # 背景操作: 双画布同步
    def _pick_bg_color(self):
        super()._pick_bg_color()
        if self._canvas2 is not None:
            try:
                self._canvas2.set_bg_color(self._bg_color)
            except Exception:
                pass

    def _pick_bg_image(self):
        super()._pick_bg_image()
        if self._canvas2 is not None and self._bg_image_path:
            try:
                self._canvas2.set_bg_image(self._bg_image_path)
            except Exception:
                pass

    def _clear_bg_image(self):
        super()._clear_bg_image()
        if self._canvas2 is not None:
            try:
                self._canvas2.set_bg_image("")
                self._canvas2.set_bg_color(self._bg_color)
            except Exception:
                pass


# ═══════════════════════════════════════════════════════════════════
# 外层容器: Tab 1 (PP 调色) + Tab 2 (PP 调色 + 批量导出 PNG)
# 这是 Tool_Development.py 里 "PP官方管线" 的真正入口.
# 两个 Tab 完全独立: 各有一套 方案/滑块/.pp 读写/预览, 互不联动.
# ═══════════════════════════════════════════════════════════════════
class PPPipelineTabsPage(QWidget):
    """
    PP官方管线 (多 Tab 版本):
        Tab 1: PPPipelinePage   — 原 PP 调色主界面 (滑块 + 预览 + .pp 读写)
        Tab 2: PPExportPage     — 完整调色 + 预览 + 末尾批量导出 PNG 序列
    """
    def __init__(self, parent=None):
        super().__init__(parent)
        lay = QVBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(0)

        self._tabs = QTabWidget()
        self._tabs.setDocumentMode(True)
        self._tabs.setStyleSheet(
            "QTabBar::tab { min-width: 160px; padding: 6px 14px; "
            "font: 10pt 'Microsoft YaHei UI'; }"
            "QTabBar::tab:selected { background:#2f4f7f; color:white; }"
        )

        # Tab 1: 原有页面 (不变)
        self._tab1 = PPPipelinePage()
        self._tabs.addTab(self._tab1, "Tab 1  ·  PP 调色")

        # Tab 2: 完整调色 + 预览 + 末尾 批量导出 PNG 序列
        self._tab2 = PPExportPage()
        self._tabs.addTab(self._tab2, "Tab 2  ·  TGA 变色输出")

        lay.addWidget(self._tabs)

    # ───── 窗口几何恢复 (主窗口打开时调用) ──────────────────────────
    def restore_saved_window_geometry(self, main_window=None):
        if hasattr(self._tab1, "restore_saved_window_geometry"):
            try:
                self._tab1.restore_saved_window_geometry(main_window)
            except Exception:
                pass

