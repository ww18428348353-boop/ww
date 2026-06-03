"""
tcp_loader.py  ─  .tcp 二进制文件元数据读取器

大话西游 II 的 TCP 文件 (Sprite Pack) 文件头格式 (经实测 5 个真实样本全对得上):

    Offset  Size  字段
    ─────────────────────────────────────────
    0x00    2     magic    = b'SP'
    0x02    2     version  = 0x000C   (=12, 目前所有样本都是 12)
    0x04    2     num_dirs (方向数, 通常 8)
    0x06    2     num_frames (每方向帧数, 8/12/16)
    0x08    2     width (帧宽, 像素)
    0x0A    2     height (帧高, 像素)
    0x0C    2     center_x (绘制锚点 X, 角色脚下点的相对坐标)
    0x0E    2     center_y (绘制锚点 Y)
    0x10    ...   后续是帧偏移表 + RLE 压缩的索引图数据

本模块仅解析头部, 不解码像素 (帧像素是 RLE 索引图 + 调色板, 解码可后续接入).
对外提供 TcpHeader 数据结构 + 友好打印函数, 供 PP 编辑器:
    - 显示 TCP 概要信息 (尺寸/方向数/总帧数)
    - 推断同目录下应使用哪个 .pp 文件
    - 没有 .pp 时按照"中性默认"自动生成一个
"""
from __future__ import annotations

import os
import struct
from dataclasses import dataclass
from typing import Optional


TCP_MAGIC = b'SP'
TCP_VERSION_KNOWN = 0x000C   # 目前样本一律是 12, 其他版本暂不支持


@dataclass
class TcpHeader:
    """TCP 文件头部 (仅元数据, 不含像素)."""
    path: str
    file_size: int
    magic: bytes          # 应为 b'SP'
    version: int          # 应为 0x000C
    num_dirs: int         # 方向数
    num_frames: int       # 每方向帧数
    width: int            # 帧宽
    height: int           # 帧高
    center_x: int
    center_y: int

    @property
    def total_frames(self) -> int:
        return self.num_dirs * self.num_frames

    def summary(self) -> str:
        return (
            f"{os.path.basename(self.path)}  "
            f"v{self.version}  "
            f"{self.width}x{self.height}  "
            f"{self.num_dirs}方向 x {self.num_frames}帧 = {self.total_frames}帧  "
            f"锚点({self.center_x},{self.center_y})  "
            f"{self.file_size:,} bytes"
        )


class TcpFormatError(Exception):
    """TCP 文件格式错误 (magic 不对 / 文件太短等)."""


def read_tcp_header(path: str) -> TcpHeader:
    """读取 .tcp 文件头, 失败时抛 TcpFormatError."""
    if not os.path.isfile(path):
        raise TcpFormatError(f"文件不存在: {path}")

    file_size = os.path.getsize(path)
    if file_size < 16:
        raise TcpFormatError(f"文件太小 ({file_size} bytes), 不像 TCP")

    with open(path, 'rb') as f:
        head = f.read(16)

    magic = head[0:2]
    if magic != TCP_MAGIC:
        raise TcpFormatError(
            f"magic 不匹配: 期望 b'SP', 实际 {magic!r}"
        )

    (version, num_dirs, num_frames, width, height, cx, cy) = struct.unpack_from(
        '<HHHHHHH', head, 2
    )

    if version != TCP_VERSION_KNOWN:
        # 不当作错误, 只是先打个标记 — 头字段大概率仍然有效
        pass

    if num_dirs == 0 or num_frames == 0 or width == 0 or height == 0:
        raise TcpFormatError(
            f"头字段含 0 (dirs={num_dirs}, frames={num_frames}, "
            f"w={width}, h={height}), 不像有效 TCP"
        )

    if num_dirs > 32 or num_frames > 1024 or width > 8192 or height > 8192:
        raise TcpFormatError(
            f"头字段超界 (dirs={num_dirs}, frames={num_frames}, "
            f"w={width}, h={height}), 可能不是 TCP 或为新版本"
        )

    return TcpHeader(
        path=path,
        file_size=file_size,
        magic=magic,
        version=version,
        num_dirs=num_dirs,
        num_frames=num_frames,
        width=width,
        height=height,
        center_x=cx,
        center_y=cy,
    )


# ────────────────────────────────────────────────────────────
# 同目录 .pp 关联
# ────────────────────────────────────────────────────────────
def find_companion_pp(tcp_path: str) -> Optional[str]:
    """
    在 TCP 所在目录里寻找一个 .pp 文件.
    优先级:
        1. <tcp_basename>.pp        (例如 stand.tcp -> stand.pp)
        2. 00.pp                   (大话标准默认)
        3. 第一个找到的 *.pp
    返回完整路径, 找不到返回 None.
    """
    if not tcp_path:
        return None
    folder = os.path.dirname(tcp_path)
    if not os.path.isdir(folder):
        return None

    base = os.path.splitext(os.path.basename(tcp_path))[0]
    candidates = [
        os.path.join(folder, base + '.pp'),
        os.path.join(folder, '00.pp'),
    ]
    for c in candidates:
        if os.path.isfile(c):
            return c

    # 兜底: 第一个 *.pp
    try:
        for fn in sorted(os.listdir(folder)):
            if fn.lower().endswith('.pp'):
                return os.path.join(folder, fn)
    except OSError:
        pass

    return None


# ────────────────────────────────────────────────────────────
# tcp_2_tga.exe 调用层 (官方解码器复用)
#
# 因为 TCP 用的是 NeoX 私有 RLE+RGB565 编码, 自写解码器风险高.
# 你已经有官方 tcp_2_tga.exe, 我们直接复用它:
#   1) 在 %TEMP%\pp_tool_cache\<hash>\input_tcp\ 放一份 TCP 副本
#   2) 在该目录调起 tcp_2_tga.exe <size>
#   3) 它会写出 output_tga\<basename>\<dir><frame>.tga (例: 0000.tga)
#   4) 后续同 (size, mtime, file_size) 三元组直接命中缓存, 不重跳
# ────────────────────────────────────────────────────────────
import hashlib
import shutil
import subprocess
import tempfile

# tcp_2_tga.exe 的候选位置 (按优先级)
_TCP2TGA_CANDIDATES = [
    r'R:\QAutoEditor\packages\res_tools\bin\tcp_2_tga.exe',
    r'X:\DH2-Qauto\sed_hf_260421\_unpack_work\tcp_2_tga_portable\bin\tcp_2_tga.exe',
    r'X:\DH2-Qauto\sed_hf_260421\_unpack_work\tcp_2_tga_portable\tcp_2_tga.exe',
]

# 显式覆盖 (用户在 UI 里手动指定时存到这里)
_user_override_tcp2tga: Optional[str] = None


def set_tcp2tga_exe(path: Optional[str]) -> None:
    """允许 UI 显式指定 tcp_2_tga.exe 路径."""
    global _user_override_tcp2tga
    _user_override_tcp2tga = path if path and os.path.isfile(path) else None


def find_tcp2tga_exe() -> Optional[str]:
    """按优先级找 tcp_2_tga.exe, 找不到返回 None."""
    if _user_override_tcp2tga and os.path.isfile(_user_override_tcp2tga):
        return _user_override_tcp2tga
    for p in _TCP2TGA_CANDIDATES:
        if os.path.isfile(p):
            return p
    return None


def _cache_key(tcp_path: str, size: int) -> str:
    """同 (path, size, mtime, file_size) 不变就命中, 用 sha1 取前 12 位."""
    st = os.stat(tcp_path)
    raw = f'{os.path.abspath(tcp_path)}|{size}|{st.st_mtime_ns}|{st.st_size}'
    return hashlib.sha1(raw.encode('utf-8')).hexdigest()[:12]


def _cache_root() -> str:
    return os.path.join(tempfile.gettempdir(), 'pp_tool_cache')


def extract_first_frame_to_tga(
    tcp_path: str,
    size: int = 500,
    *,
    direction: int = 0,
    frame: int = 0,
    force: bool = False,
) -> Optional[str]:
    """
    把 TCP 转成 TGA, 返回指定 (direction, frame) 的 TGA 完整路径.

    工作流 (经实证, tcp_2_tga.exe 是 wrapper 会再启动 bin/tcp2tga.exe,
    且需要同名 .key 文件, 因此必须在它的原始 portable 目录下跑):

      1) 计算缓存键 = sha1(tcp 绝对路径 + size + mtime + filesize)[:12]
      2) 若缓存目录里已有目标 TGA, 直接返回 (秒读)
      3) 否则: 在 tcp_2_tga.exe 所在 portable 根目录 (exe_dir/.. )
         的 input_tcp/ 里放一份 TCP 副本
      4) 在 portable 根目录调起 tcp_2_tga.exe <size>
      5) 把生成的 TGA 从 portable/output_tga/<basename>/ 搬到自己的缓存目录
         (避免影响别的进程)
      6) 返回缓存目录里的 TGA 路径

    参数:
        tcp_path  : 待转换的 TCP 文件
        size      : tcp_2_tga.exe 的边长参数 (320/500/1000/1500)
        direction : 方向 0~7
        frame     : 该方向的帧号 (0~total-1)
        force     : True = 跳过缓存重新转

    返回:
        TGA 完整路径; 任意一步失败返回 None.
    """
    if not os.path.isfile(tcp_path):
        return None

    exe = find_tcp2tga_exe()
    if not exe:
        return None

    base = os.path.splitext(os.path.basename(tcp_path))[0]
    key  = _cache_key(tcp_path, size)
    cache_dir = os.path.join(_cache_root(), key)
    cache_out_dir = os.path.join(cache_dir, base)
    tga_name  = f'{direction}{frame:03d}.tga'
    tga_path  = os.path.join(cache_out_dir, tga_name)

    if not force and os.path.isfile(tga_path):
        return tga_path

    # tcp_2_tga.exe 是 wrapper: 它在 cwd 下找 input_tcp/, 调 bin/tcp2tga.exe.
    # 必须在它的 portable 根目录 (exe 上一级) 下跑.
    exe_dir = os.path.dirname(exe)
    portable_root = os.path.dirname(exe_dir) if os.path.basename(exe_dir).lower() == 'bin' \
                    else exe_dir
    portable_input  = os.path.join(portable_root, 'input_tcp')
    portable_output = os.path.join(portable_root, 'output_tga', base)

    try:
        os.makedirs(portable_input, exist_ok=True)
        os.makedirs(cache_out_dir, exist_ok=True)
    except OSError as e:
        print(f'[tcp_loader] 准备目录失败: {e}')
        return None

    # 复制 TCP + 同名 .key 文件 (有的话)
    src_dir = os.path.dirname(tcp_path)
    dst_tcp = os.path.join(portable_input, os.path.basename(tcp_path))
    try:
        shutil.copy2(tcp_path, dst_tcp)
        # 同名 .key (大话工具链要求, 没有也可以跑, 但有更好)
        key_src = os.path.join(src_dir, base + '.key')
        if os.path.isfile(key_src):
            shutil.copy2(key_src, os.path.join(portable_input, base + '.key'))
    except OSError as e:
        print(f'[tcp_loader] 复制 TCP 到 input_tcp 失败: {e}')
        return None

    # 调用 tcp_2_tga.exe — cwd 必须是 portable_root
    try:
        creationflags = 0
        if os.name == 'nt':
            creationflags = 0x08000000  # CREATE_NO_WINDOW
        result = subprocess.run(
            [exe, str(int(size))],
            cwd=portable_root,
            capture_output=True,
            text=True,
            timeout=120,
            creationflags=creationflags,
        )
        if result.returncode != 0:
            print(f'[tcp_loader] tcp_2_tga.exe 退出码 {result.returncode}')
            print(f'   stdout: {result.stdout[-500:]}')
            print(f'   stderr: {result.stderr[-500:]}')
    except (subprocess.TimeoutExpired, OSError) as e:
        print(f'[tcp_loader] 调用 tcp_2_tga.exe 失败: {e}')
        return None

    # 把生成的 TGA 从 portable/output_tga/<basename>/ 复制到自己的缓存目录
    if os.path.isdir(portable_output):
        try:
            for fn in os.listdir(portable_output):
                if fn.lower().endswith('.tga'):
                    src = os.path.join(portable_output, fn)
                    dst = os.path.join(cache_out_dir, fn)
                    if not os.path.isfile(dst):
                        shutil.copy2(src, dst)
        except OSError as e:
            print(f'[tcp_loader] 复制 TGA 到缓存失败: {e}')

    if os.path.isfile(tga_path):
        return tga_path

    # 兜底: 缓存里没目标帧, 拿任意一张
    if os.path.isdir(cache_out_dir):
        for fn in sorted(os.listdir(cache_out_dir)):
            if fn.lower().endswith('.tga'):
                return os.path.join(cache_out_dir, fn)

    return None


def clear_tcp_cache() -> int:
    r"""清空 %TEMP%\pp_tool_cache\, 返回删除的目录数."""
    root = _cache_root()
    if not os.path.isdir(root):
        return 0
    n = 0
    for fn in os.listdir(root):
        sub = os.path.join(root, fn)
        if os.path.isdir(sub):
            try:
                shutil.rmtree(sub, ignore_errors=True)
                n += 1
            except OSError:
                pass
    return n


def default_pp_path_for_tcp(tcp_path: str) -> str:
    """
    根据 TCP 路径推算 "如果要为它生成默认 PP" 应该使用什么路径.
    规则: 同目录下, 文件名 = <tcp_basename>.pp
    """
    folder = os.path.dirname(tcp_path)
    base = os.path.splitext(os.path.basename(tcp_path))[0]
    return os.path.join(folder, base + '.pp')


# ────────────────────────────────────────────────────────────
# 自测: python -m modules.tcp_loader <some.tcp>
# ────────────────────────────────────────────────────────────
if __name__ == '__main__':
    import sys
    paths = sys.argv[1:]
    if not paths:
        # 默认扫一下解包目录
        DEFAULT = r'X:\DH2-Qauto\sed_hf_260421\_unpack_work\tcp文件'
        if os.path.isdir(DEFAULT):
            paths = [os.path.join(DEFAULT, fn) for fn in os.listdir(DEFAULT)
                     if fn.lower().endswith('.tcp')]
    for p in paths:
        try:
            h = read_tcp_header(p)
            print('  ', h.summary())
            comp = find_companion_pp(p)
            print('     companion .pp:', comp or '(none, will be auto-generated)')
        except TcpFormatError as e:
            print(f'  FAIL  {os.path.basename(p)}: {e}')
