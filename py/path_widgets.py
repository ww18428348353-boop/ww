"""
path_widgets.py — 共享的路径输入组件 + UI 布局持久化

提供:
  PathLineEdit  — 可手动编辑、双击浏览、路径规范化的 QLineEdit 子类
  LayoutMixin   — 为任意 QWidget 页面添加 "保存UI布局 / 重置UI布局" 能力
"""

import os
from PyQt5.QtWidgets import (
    QLineEdit, QFileDialog, QPushButton, QHBoxLayout, QWidget,
    QSplitter, QTableWidget, QHeaderView, QMessageBox
)
from PyQt5.QtGui import QFont
from PyQt5.QtCore import Qt, pyqtSignal, QSettings


class PathLineEdit(QLineEdit):
    """可编辑的路径输入框

    功能:
    - 支持手动粘贴/输入路径，\\ 和 / 都能生效
    - 双击弹出浏览对话框
    - 回车键确认路径
    - 路径变更时发出 pathChanged 信号
    """

    # 路径变更信号（规范化后的路径）
    pathChanged = pyqtSignal(str)

    # 浏览模式
    MODE_DIR = "dir"         # 选择目录
    MODE_FILE = "file"       # 选择文件
    MODE_OUTPUT = "output"   # 选择输出目录（不存在时自动创建）

    def __init__(self, parent=None, mode="dir", title="选择路径",
                 file_filter="", default_dir=""):
        super().__init__(parent)
        self._mode = mode
        self._browse_title = title
        self._file_filter = file_filter
        self._default_dir = default_dir
        self._last_valid_path = ""

        # 支持手动编辑
        self.setReadOnly(False)

        # 回车确认
        self.returnPressed.connect(self._on_return_pressed)
        # 失去焦点时也确认
        self.editingFinished.connect(self._on_editing_finished)

    def mouseDoubleClickEvent(self, event):
        """双击弹出浏览对话框"""
        self._browse()
        # 不调用 super，避免双击选中文字
        event.accept()

    def _browse(self):
        """弹出浏览对话框"""
        start_dir = self.text().strip() or self._default_dir or ""
        # 如果当前文本是有效目录，以它为起点
        if start_dir and os.path.isdir(start_dir):
            pass
        elif start_dir and os.path.isdir(os.path.dirname(start_dir)):
            start_dir = os.path.dirname(start_dir)
        else:
            start_dir = self._default_dir or ""

        if self._mode == MODE_FILE:
            path, _ = QFileDialog.getOpenFileName(
                self, self._browse_title, start_dir, self._file_filter
            )
        else:
            # dir / output 都用目录对话框
            path = QFileDialog.getExistingDirectory(
                self, self._browse_title, start_dir,
                QFileDialog.ShowDirsOnly | QFileDialog.DontResolveSymlinks
            )

        if path:
            normalized = path.replace('\\', '/')
            self.setText(normalized)
            self._apply_path(normalized)

    def _on_return_pressed(self):
        """回车确认"""
        self._apply_current_text()

    def _on_editing_finished(self):
        """编辑完成（失去焦点）"""
        self._apply_current_text()

    def _apply_current_text(self):
        """处理当前文本输入"""
        raw = self.text().strip()
        if not raw:
            return
        # 规范化路径分隔符
        normalized = raw.replace('\\', '/')
        if normalized != raw:
            self.setText(normalized)
        self._apply_path(normalized)

    def _apply_path(self, path):
        """应用路径并发出信号"""
        if path == self._last_valid_path:
            return

        # 输出模式：不存在则自动创建
        if self._mode == MODE_OUTPUT and path:
            if not os.path.exists(path):
                try:
                    os.makedirs(path, exist_ok=True)
                except OSError:
                    pass  # 创建失败不阻塞

        self._last_valid_path = path
        self.pathChanged.emit(path)

    def get_path(self):
        """获取当前规范化路径"""
        raw = self.text().strip().replace('\\', '/')
        return raw

    def set_path(self, path):
        """设置路径"""
        normalized = path.replace('\\', '/') if path else ""
        self.setText(normalized)
        self._last_valid_path = normalized

    def set_default_dir(self, d):
        """设置浏览对话框的默认起始目录"""
        self._default_dir = d


# 方便直接引用的常量
MODE_DIR = PathLineEdit.MODE_DIR
MODE_FILE = PathLineEdit.MODE_FILE
MODE_OUTPUT = PathLineEdit.MODE_OUTPUT


# ============================================================================
# LayoutMixin — UI 布局持久化
# ============================================================================

# 按钮 QSS 常量
_SAVE_BTN_QSS = """
QPushButton {
    background-color: #7c3aed;
    color: white;
    border: none;
    border-radius: 4px;
    padding: 5px 14px;
    font-size: 12px;
}
QPushButton:hover {
    background-color: #6d28d9;
}
QPushButton:pressed {
    background-color: #5b21b6;
}
"""

_RESET_BTN_QSS = """
QPushButton {
    background-color: #9ca3af;
    color: white;
    border: none;
    border-radius: 4px;
    padding: 5px 14px;
    font-size: 12px;
}
QPushButton:hover {
    background-color: #6b7280;
}
QPushButton:pressed {
    background-color: #4b5563;
}
"""


class LayoutMixin:
    """为 QWidget 页面提供 "保存UI布局 / 重置UI布局" 功能的混入类。

    使用方法:
    1. 页面类继承时加入 LayoutMixin:  class MyPage(QWidget, LayoutMixin):
       或者直接调用 LayoutMixin 的方法（鸭子类型）。
    2. 在 _init_ui() 末尾调用 self._init_layout_buttons(parent_layout)
       将两个按钮放到页面右上角。
    3. 定义 self._layout_key = "unique_page_name"  作为 QSettings 存储键。
    4. 把需要保存/恢复的可调组件记录到:
         self._layout_splitters = [splitter1, splitter2, ...]
         self._layout_tables    = [table1, ...]
       以及默认值:
         self._layout_defaults = {
             'splitters': [[500, 500], [300, 700]],
             'tables': [
                 {col_idx: width, ...},  # 仅记录 Fixed/Interactive 列
             ]
         }
    5. 在 _init_ui() 的最后调用 self._restore_layout() 恢复上次保存的布局。
    """

    # ---------- 按钮创建 ----------
    def _init_layout_buttons(self, target_layout):
        """在 target_layout 右侧创建两个按钮并返回按钮行布局。
        target_layout 通常是页面的 main_layout（QVBoxLayout）。
        按钮会被插入到 target_layout 的 **最前面**。
        """
        btn_row = QHBoxLayout()
        btn_row.setContentsMargins(0, 0, 0, 0)
        btn_row.setSpacing(8)
        btn_row.addStretch()

        self._save_layout_btn = QPushButton("保存UI布局")
        self._save_layout_btn.setFont(QFont("Microsoft YaHei UI", 9))
        self._save_layout_btn.setStyleSheet(_SAVE_BTN_QSS)
        self._save_layout_btn.setFixedHeight(30)
        self._save_layout_btn.setCursor(Qt.PointingHandCursor)
        self._save_layout_btn.clicked.connect(self._on_save_layout)
        btn_row.addWidget(self._save_layout_btn)

        self._reset_layout_btn = QPushButton("重置UI布局")
        self._reset_layout_btn.setFont(QFont("Microsoft YaHei UI", 9))
        self._reset_layout_btn.setStyleSheet(_RESET_BTN_QSS)
        self._reset_layout_btn.setFixedHeight(30)
        self._reset_layout_btn.setCursor(Qt.PointingHandCursor)
        self._reset_layout_btn.clicked.connect(self._on_reset_layout)
        btn_row.addWidget(self._reset_layout_btn)

        # 插入到 layout 最前面
        target_layout.insertLayout(0, btn_row)

    # ---------- 保存 ----------
    def _on_save_layout(self):
        key = getattr(self, '_layout_key', 'default_page')
        settings = QSettings("DH2_FX_Tool", "ToolDevelopment")
        prefix = f"layout/{key}"

        # 保存 splitter sizes
        splitters = getattr(self, '_layout_splitters', [])
        for i, sp in enumerate(splitters):
            if isinstance(sp, QSplitter):
                settings.setValue(f"{prefix}/splitter_{i}", sp.sizes())

        # 保存 table column widths
        tables = getattr(self, '_layout_tables', [])
        for i, tbl in enumerate(tables):
            if isinstance(tbl, QTableWidget):
                widths = []
                for c in range(tbl.columnCount()):
                    widths.append(tbl.columnWidth(c))
                settings.setValue(f"{prefix}/table_{i}_widths", widths)

        settings.sync()
        # 简单反馈
        if hasattr(self, '_save_layout_btn'):
            old_text = self._save_layout_btn.text()
            self._save_layout_btn.setText("✔ 已保存")
            from PyQt5.QtCore import QTimer
            QTimer.singleShot(1200, lambda: self._save_layout_btn.setText(old_text))

    # ---------- 恢复 ----------
    def _restore_layout(self):
        """尝试从 QSettings 恢复布局，如果没有则使用默认值。"""
        key = getattr(self, '_layout_key', 'default_page')
        settings = QSettings("DH2_FX_Tool", "ToolDevelopment")
        prefix = f"layout/{key}"

        splitters = getattr(self, '_layout_splitters', [])
        for i, sp in enumerate(splitters):
            if isinstance(sp, QSplitter):
                saved = settings.value(f"{prefix}/splitter_{i}")
                if saved:
                    try:
                        sp.setSizes([int(v) for v in saved])
                    except (TypeError, ValueError):
                        pass

        tables = getattr(self, '_layout_tables', [])
        for i, tbl in enumerate(tables):
            if isinstance(tbl, QTableWidget):
                saved = settings.value(f"{prefix}/table_{i}_widths")
                if saved:
                    try:
                        for c, w in enumerate(saved):
                            tbl.setColumnWidth(c, int(w))
                    except (TypeError, ValueError):
                        pass

    # ---------- 重置 ----------
    def _on_reset_layout(self):
        key = getattr(self, '_layout_key', 'default_page')
        defaults = getattr(self, '_layout_defaults', {})

        # 重置 splitters
        splitters = getattr(self, '_layout_splitters', [])
        default_sp = defaults.get('splitters', [])
        for i, sp in enumerate(splitters):
            if isinstance(sp, QSplitter) and i < len(default_sp):
                sp.setSizes(default_sp[i])

        # 重置 tables
        tables = getattr(self, '_layout_tables', [])
        default_tbl = defaults.get('tables', [])
        for i, tbl in enumerate(tables):
            if isinstance(tbl, QTableWidget) and i < len(default_tbl):
                for col, width in default_tbl[i].items():
                    tbl.setColumnWidth(int(col), int(width))

        # 清除 QSettings 中的保存
        settings = QSettings("DH2_FX_Tool", "ToolDevelopment")
        prefix = f"layout/{key}"
        for k in settings.allKeys():
            if k.startswith(prefix):
                settings.remove(k)
        settings.sync()

        # 反馈
        if hasattr(self, '_reset_layout_btn'):
            old_text = self._reset_layout_btn.text()
            self._reset_layout_btn.setText("✔ 已重置")
            from PyQt5.QtCore import QTimer
            QTimer.singleShot(1200, lambda: self._reset_layout_btn.setText(old_text))
