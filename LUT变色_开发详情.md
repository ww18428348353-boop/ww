# LUT变色 模块 — 开发详情文档

> 模块位置: `modules/lut_recolor_page.py`
> 主类: `LutRecolorPage`
> 版本: LUT_Recolor v1.0.0

---

## 一句话概括

**召唤兽/特效 TGA 序列帧的多层叠加实时预览 + 五维色彩调整（HSB/色阶/曲线/色彩平衡/LUT）+ 批量导出工具。**

支持 body / 00 / 01 三层结构, 每层独立开启/关闭调色, 调整后实时循环播放预览, 一键导出变色后的 TGA 序列。

---

## 功能架构总览

```
LutRecolorPage
├── 路径设置 (源目录 / 输出目录)
├── 多层加载 (body / 00 / 01 自动识别)
├── 预览区
│   ├── 循环播放 (自动递帧, 可暂停)
│   ├── 动作切换 (guard/stand/walk/run/attack/...)
│   ├── 方向切换 (2/4/8 方向, 1-8 按钮)
│   ├── 帧率控制 (1-60 fps)
│   └── 自定义背景 (纯色 / 图片)
├── 色彩调整面板 (每层一个 Tab)
│   ├── HSB (色相/饱和度/明度)
│   ├── 色阶 (输入黑/白 + Gamma + 输出黑/白)
│   ├── 曲线 (PS 风格可拖拽控制点, Fritsch-Carlson 单调三次 Hermite 插值)
│   ├── 色彩平衡 (高光/中间调/阴影 × R/G/B)
│   └── LUT 文件导入 (.cube 1D/3D)
└── 批量导出 (全层 × 全动作 × 全方向 × 全帧 → TGA)
```

---

## 详细功能清单

### 路径与加载

| 编号 | 功能 | 说明 |
|------|------|------|
| 1.01 | 源目录选择 | 浏览或输入, PathLineEdit 组件 |
| 1.02 | 输出目录选择 | 变色后 TGA 保存位置 |
| 1.03 | 加载按钮 | 扫描源目录下 body/00/01 三层 |
| 1.04 | 自动层级识别 | 按 `DEFAULT_LAYER_ORDER = ['body', '00', '01']` 自动匹配子目录 |
| 1.05 | 动作目录扫描 | 每层根目录下的子文件夹作为动作 (guard/stand/walk 等) |
| 1.06 | 帧文件识别 | 正则 `^(\d)(\d{3})\.tga$` → 方向 ID + 帧号, 自动排序 |

### 预览系统

| 编号 | 功能 | 说明 |
|------|------|------|
| 2.01 | 循环播放 | QTimer 驱动, 默认 12fps, 自动递帧 |
| 2.02 | 暂停/播放切换 | ⏸ / ▶ 按钮 |
| 2.03 | 帧率调节 | QSpinBox, 1-60 fps |
| 2.04 | 动作切换 | QComboBox, 所有层的动作并集 |
| 2.05 | 方向数模式 | 自动 / 2方向 / 4方向 / 8方向 |
| 2.06 | 方向按钮 1-8 | QButtonGroup, 根据实际可用方向启用/禁用 |
| 2.07 | 当前帧标签 | "当前帧/总帧" 实时显示 |
| 2.08 | 背景色选择 | QColorDialog, 预览画布底色 |
| 2.09 | 背景图选择 | 支持 PNG/JPG/BMP/TGA |
| 2.10 | 清除背景图 | 恢复纯色背景 |
| 2.11 | 多层合成预览 | body → 00 → 01 从下到上 SourceOver 混合 |
| 2.12 | PreviewCanvas | 居中按比例绘制, 不放大 |

### 色彩调整 (每层独立)

| 编号 | 功能 | 说明 |
|------|------|------|
| 3.01 | 层级 Tab | 每个层一个 Tab 页, 独立调参 |
| 3.02 | 总开关 | QCheckBox "启用色彩调整 (对 X 层)" |
| 3.03 | 色相 (Hue) | -180° ~ +180° 滑块 + SpinBox |
| 3.04 | 饱和度 (Saturation) | -100 ~ +100 |
| 3.05 | 明度 (Brightness) | -100 ~ +100 |
| 3.06 | 色阶输入黑 | 0 ~ 254 |
| 3.07 | 色阶输入白 | 1 ~ 255 |
| 3.08 | Gamma | 0.01 ~ 9.99, 步进 0.05 |
| 3.09 | 色阶输出黑 | 0 ~ 254 |
| 3.10 | 色阶输出白 | 1 ~ 255 |
| 3.11 | 曲线编辑器 | 256×256 交互画布, 左键添加/拖动, 右键删除, 端点锁 x |
| 3.12 | 曲线重置 | 恢复对角线 [(0,0),(255,255)] |
| 3.13 | 色彩平衡 - 高光 | R/G/B 各 ±100 |
| 3.14 | 色彩平衡 - 中间调 | R/G/B 各 ±100 |
| 3.15 | 色彩平衡 - 阴影 | R/G/B 各 ±100 |
| 3.16 | LUT 文件加载 | 支持 .cube 格式 (1D 和 3D) |
| 3.17 | LUT 清除 | 移除已加载的 LUT |
| 3.18 | 全部重置 | 当前层所有参数回到默认 |

### 导出

| 编号 | 功能 | 说明 |
|------|------|------|
| 4.01 | 导出按钮 | "导出TGA序列" |
| 4.02 | 目录结构保持 | 输出: `<out>/<layer>/<action>/<方向帧号>.tga` |
| 4.03 | 进度条 | QProgressBar 显示总体进度 |
| 4.04 | 自定义 TGA Writer | 保证 Alpha + 中文路径 + 32bpp 无压缩输出 |
| 4.05 | 处理统计 | 完成弹窗: 总计/失败/输出目录 |

### UI 布局

| 编号 | 功能 | 说明 |
|------|------|------|
| 5.01 | 保存UI布局 | 窗口大小/路径/Splitter/背景色/fps 持久化 (QSettings) |
| 5.02 | 重置UI布局 | 恢复默认分栏比例 |

---

## 色彩调整管线 — 算法细节

### ColorAdjust 类 (数据模型)

```python
class ColorAdjust:
    enabled: bool
    # HSB
    hue: int          # -180 ~ 180
    saturation: int   # -100 ~ 100
    brightness: int   # -100 ~ 100
    # 色阶
    levels_in_black, levels_in_white: int
    levels_gamma: float
    levels_out_black, levels_out_white: int
    # 曲线
    curve_points: List[Tuple[int, int]]   # [(x, y), ...] x,y ∈ [0, 255]
    # 色彩平衡
    balance_shadows: [R, G, B]   # 各 -100~100
    balance_mids:    [R, G, B]
    balance_highs:   [R, G, B]
    # LUT
    lut_1d: List[Tuple[R,G,B]] | None   # 256 级
    lut_3d: nested list | None           # NxNxN → (R,G,B)
```

### 处理管线顺序

```
输入像素 (BGRA)
    │
    ├── Alpha == 0 → 跳过 (保持原样)
    │
    ▼
[1] 色阶 → 输入映射 + Gamma + 输出映射 (生成 256 级 LUT)
    │
    ▼
[2] 曲线 → Fritsch-Carlson 单调三次 Hermite 插值, 生成 256 级 LUT
    │
    ▼
[3] 色彩平衡 → 阴影/中间调/高光 三区间权重叠加
    │        · 阴影权重: max(0, 1 - i/128) (i≤128)
    │        · 中间调权重: 1 - |i-128|/128
    │        · 高光权重: max(0, (i-128)/127) (i≥128)
    │
    ▼
[4] 1D LUT (若加载) → 直接查表替换 R/G/B
    │
    ▼
[5] HSB → RGB→HSV→偏移H/S/V→HSV→RGB
    │
    ▼
[6] 3D LUT (若加载) → 最近邻采样 (numpy 向量化)
    │
    ▼
输出像素 (BGRA, Alpha 不变)
```

### 性能优化

| 策略 | 效果 |
|------|------|
| numpy 向量化 | 有 numpy 时整图一次处理 (~1ms/500×500), 无 numpy 时逐像素 fallback |
| build_lut_table() | 色阶+曲线+色彩平衡+1D LUT 合并为 3 条 256 级查表, O(1) 查表 |
| 帧缓存 | `_frame_cache` 最多 200 张, 参数不变时直接命中 |
| 节流 debounce | 滑块连续拖动 60ms 合并为一次缓存失效 + 重渲染 |
| 透明跳过 | Alpha==0 像素不做任何计算 |

### TGA 读写

| 操作 | 实现 |
|------|------|
| 读取 | `load_tga()`: 优先 Qt plugin (支持中文路径 via bytes), 失败时 fallback 纯 Python 解析 type=2/10 32bpp |
| 写入 | `save_tga()`: 纯 Python 实现 uncompressed 32bpp TGA (保 Alpha), 避免 PyInstaller 环境下 Qt TGA writer 不稳定问题 |

### 曲线插值算法

使用 **Fritsch-Carlson 单调三次 Hermite 样条**:

1. 计算每段斜率 `d_k = (y_{k+1} - y_k) / (x_{k+1} - x_k)`
2. 初步端点斜率: 极值点切平 (防止超调)
3. 单调性修正: `a²+b² > 9` 时按比例缩小 → 保证曲线不回卷
4. Hermite 三次插值采样 256 级

效果: PS 风格圆滑曲线, 无论控制点如何摆放都不会出现"上翘后回卷"。

---

## 动作方向规范

```python
ACTION_DIRECTIONS = {
    'guard':   (2, 4),
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
    'addon':   (8, 4, 2),
}
```

---

## 多层合成逻辑

```
合成顺序 (从下到上):
    body ← 00 ← 01

渲染过程:
    1. 创建透明画布 (ARGB32)
    2. 遍历每层 (body → 00 → 01):
       - 若层不可见 → 跳过
       - 取当前帧 (帧号 mod 该层帧数 → 循环)
       - 应用该层的 ColorAdjust (if enabled)
       - CompositionMode_SourceOver 叠加到画布
    3. 画布送 PreviewCanvas 显示
```

---

## 依赖关系

```
lut_recolor_page.py
├── PyQt5 (全部 UI)
├── numpy (可选, 向量化加速)
├── modules/path_widgets.py (PathLineEdit)
└── 无外部 exe 依赖 (纯 Python 实现)
```

---

## 核心数据流

```
用户操作:
  [1] 选择源目录 (含 body/00/01 子目录)
       ↓
  [2] 点击"加载" → 扫描三层, 每层扫动作/方向/帧
       ↓
  [3] 自动循环播放预览 (body+00+01 合成)
       ↓
  [4] 在各层 Tab 中调整色彩参数
       ↓ (实时预览)
  [5] 选择输出目录 → 点击"导出TGA序列"
       ↓
  [6] 逐层逐帧: load_tga → apply_to_image → save_tga
       ↓
  [7] 输出完成, 统计结果
```

---

*文档生成时间: 2026-05-30*
*对应代码: modules/lut_recolor_page.py (~1936行)*
