# HighPro_LUT — GIF 输出管线

> 版本: v1.0 (Level 2 全局调色板)  
> 生成日期: 2026-06-02  
> 项目根: `x:\Lut变色\HighPro_LUT`  
> 配套文档: `00_开发环境.md`, `04_资源与材质.md`

---

## 一、总览

```
用户点 [输出 GIF 图...] 按钮
  │
  ↓ PreviewPanel::onExportGif()                                 (UI 编排层)
  │   1. 校验源 / 帧数
  │   2. 弹文件对话框 选输出路径
  │   3. 推断 baseW/baseH (TGA 实际尺寸)
  │   4. 计算网格 cells: cols×rows + cell 间距 + padding → gifW × gifH
  │   5. 临时停播放, 锁 zoom=1.0, 切 showLabel=gifShowId
  │   6. 调 GifExporter::exportGif(opts, totalFrames, setFrame, renderOnce)
  │   7. 还原 zoom/playing/frame
  │
  ↓ GifExporter::exportGif()                                    (编码层)
  │   1. 创建离屏 D3D11 BGRA RTV + staging
  │   2. 第一遍: 逐帧 setFrame(f) → renderOnce(rtv) → readback → frames[f] (RGBA)
  │   3. 拼所有帧成长图 → GifMakePalette → 全局 GifPalette pal (256 色)
  │   4. 写 GIF 头 (LSD + GCT + NETSCAPE 循环块)
  │   5. 每帧 GifDitherImage 用 pal → writeFrameWithGlobalPalette
  │   6. 写 0x3B trailer
  │   7. 中文路径中转复制 (TEMP → 用户路径)
  │   8. 后处理 NETSCAPE 字节修改循环次数
  │
  ↓ 用户拿到 .gif
```

**alpha**: GIF 1-bit 透明 (此实现强制 RGB 不透明, alpha 已在 Gamma 合成阶段写入背景色).  
**循环**: 默认无限循环 (NETSCAPE 2.0 子块两字节都 0); 用户改成 N 次 → 写 N 到 LE16.  
**调色板**: 全局 256 色 (Level 2), 每帧用同一调色板做 Floyd-Steinberg dither.

---

## 二、源文件 / 类

```
extern/gif/gif.h                       3rd-party (Charlie Tangora, Public Domain, ~28 KB)
                                       提供: GifPalette / GifMakePalette / GifDitherImage / GifThresholdImage
                                       LZW 编码工具 (GifWriteCode / GifWriteBit / GifWriteChunk / GifLzwNode)

src/render/GifExporter.h               GifExporter::Options / Result / exportGif() 接口
src/render/GifExporter.cpp             实现:
                                         · writeGifHeader            写 LSD + 真实 GCT + NETSCAPE
                                         · writeFrameWithGlobalPalette  写帧时不带 LCT
                                         · exportGif                总编排

src/ui/PreviewPanel.{h,cpp}            UI 编排:
                                         · m_gifLoopSpin / m_gifIdChk / m_gifExportBtn
                                         · onExportGif()            参数收集 + 调 exportGif

extern/gif (CMake interface)           target_link_libraries
```

---

## 三、UI 入口 (`PreviewPanel`)

### 3.1 Toolbar 第三行控件

| 控件 | 名 | 默认 | 范围 | 含义 |
|------|---|------|------|------|
| `QLabel("循环次数:")` | — | — | — | 标签 |
| `QSpinBox m_gifLoopSpin` | — | 0 | 0~9999 | 0=无限循环, 1~N=播 N+1 次 (NETSCAPE 语义); SpecialValueText="无限" |
| `QCheckBox m_gifIdChk` | "GIF ID" | true | — | 是否在 GIF 帧上画方案 ID label |
| `QPushButton m_gifExportBtn` | "输出 GIF 图..." | — | — | 触发导出 |

### 3.2 `onExportGif()` 流程

`src/ui/PreviewPanel.cpp` 关键步骤:

```cpp
1) 校验
   - proj.layers 非空? 否则 "还没加载源目录"
   - totalFrames >= 1?

2) 选输出路径
   - 默认名: <源目录名>_<动作>_<N>frame.gif
   - 起始目录: AppSettings.lastOutputDir → proj.sourceRoot
   - QFileDialog::getSaveFileName

3) 推断 baseW/baseH
   - 读 proj.layers.first().action(currentAction).framesByDir[dir][0] 这一帧的 D3D11Texture
   - 得到 TGA 实际尺寸 (e.g. 500×500)

4) 计算网格 (与 PreviewPanel::render 同样规则)
   - N = min(schemes.size(), 28)
   - cols = min(N, 7), rows = ceil(N/cols)
   - cellW = baseW, cellH = baseH (zoom=1.0, 不缩放)
   - gapX = m_charGapXPx (用户调的负间距, 例如 -300)
   - gapY = m_charGapYPx (例如 -260)
   - stepX = cellW + gapX, stepY = cellH + gapY
   - gridW = stepX*(cols-1) + cellW
   - gridH = stepY*(rows-1) + cellH
   - padX = padY = 40
   - gifW = max(64, gridW + padX*2)
   - gifH = max(64, gridH + padY*2)

5) 临时状态切换
   - 暂停播放 (m_playing=false, 停 m_playTimer)
   - 锁 zoom = 1.0 (D3DWidget::setContentZoom)
   - 切 m_showLabel = m_gifShowId
   - 记录 savedFrame = proj.currentFrame

6) 构 GifExporter::Options + 回调
   - opts.outPath / width / height / fps / loopCount / bgColor
   - setFrame  = lambda { ctl.setCurrentFrame(f); processEvents x4 }   ← 等异步加载
   - renderOnce = lambda { this->render(rtv, w, h) }                   ← 复用 PreviewPanel 渲染

7) 调 GifExporter::exportGif()

8) 还原状态
   - showLabel 复位
   - zoom 复位
   - currentFrame 复位
   - 恢复播放定时器

9) 提示结果 / 写 lastOutputDir
```

### 3.3 网格策略举例

```
schemes=22  →  cols=7, rows=4  →  gridW = 6*(500+gap) + 500
                                  gridH = 3*(500+gap) + 500
schemes=15  →  cols=7, rows=3
schemes=7   →  cols=7, rows=1
```

---

## 四、编码层 (`GifExporter`)

### 4.1 接口

```cpp
struct Options {
    QString outPath;            // utf-8 路径 (中文友好, TEMP 中转)
    int     width, height;      // GIF 帧像素
    int     fps;                // 1..50
    int     loopCount;          // 0=无限, N>0=播 N 次
    QColor  bgColor;            // 不直接写, 由 renderOnce 内已合成
    bool    showLabel;          // 信息字段, 实际由调用方控制
};

struct Result {
    bool    ok;
    QString error;
    int     framesWritten;
    qint64  bytesWritten;
};

using SetFrameFn  = std::function<void(int frameIdx)>;
using RenderRtvFn = std::function<void(ID3D11RenderTargetView* rtv, int w, int h)>;

static Result exportGif(const Options&, int frameCount, SetFrameFn, RenderRtvFn);
```

设计要点:
- **回调注入**: GifExporter 不直接依赖 PreviewPanel/ProjectController, 通过 SetFrameFn / RenderRtvFn 解耦
- **同步逐帧**: setFrame 一帧一调, 等异步资源加载完, 然后 renderOnce 一次出 RTV; 没用线程池

### 4.2 `exportGif` 五步走

#### Step 1 — 创建离屏 RTV (BGRA, 与 D3DWidget 一致)

```cpp
DXGI_FORMAT_B8G8R8A8_UNORM      // 非 _SRGB, 与 Gamma 合成 pipeline 配套
                                // (详见 03_TGA加载与渲染管线.md)
+ BIND_RENDER_TARGET | BIND_SHADER_RESOURCE
+ Staging (CPU_READ) 用于 readback
```

#### Step 2 — 渲染所有帧 → RGBA 缓存

```cpp
for (f = 0..frameCount-1) {
    setFrame(f);                                    // 业务层切帧 + 等加载
    renderOnce(rtv, opts.width, opts.height);       // 业务层渲染 (复用 FrameRenderer cells)
    dc->CopyResource(staging, rtvTex);
    Map(staging) → 拷贝 + BGRA→RGBA 重排 → frames[f] (vector<uint8_t>)
}
```

| 项 | 值 |
|----|---|
| 单帧字节数 | `width × height × 4` |
| 总 RAM | `frames × bytes` (e.g. 12帧×800×800×4 ≈ 30 MB) |
| RAM 上限 | 2 GB (超出直接报错, 防爆内存) |
| 通道顺序 | 内存中 R 在低 (gif.h 期望) |
| Alpha | 强制 255 (GIF 不支持半透明) |

#### Step 3 — 全局调色板 (Level 2 核心)

```cpp
// 把 N 帧物理拼接成长向量, 当作 numPixels=framePx*N, height=1 喂给 GifMakePalette
std::vector<uint8_t> bigImage(frameBytes * frameCount);
memcpy each frame contiguously;

GifPalette pal;
pal.bitDepth = 8;                                  // 256 色 (2^8)
GifMakePalette(/*lastFrame=*/nullptr,              // 不要差分
               bigImage.data(),
               framePx * frameCount, 1u,           // w*h 等价
               8, /*buildForDither=*/true, &pal);

bigImage.clear();                                  // 释放, 后面只用 pal
```

`GifMakePalette` 内部走 **median-cut** k-d tree 构建:
- 选 256 个颜色锚点
- 写 `pal.r/g/b[256]` + k-d 树 `treeSplitElt[256]` / `treeSplit[256]` 用于查找

#### Step 4 — 写 GIF 文件

##### (a) `writeGifHeader` 头部

```
6  bytes:  "GIF89a"
2  bytes:  width  (LE)
2  bytes:  height (LE)
1  byte:   packed = 0x80 | 0x70 | (bitDepth-1)
                    │     │     │
                    │     │     └─ size = 7 (256 entries)
                    │     └─────── resolution = 7 (24-bit)
                    └─────────────  GCT_flag = 1 (有全局调色板)
1  byte:   background color index (0)
1  byte:   pixel aspect ratio    (0)

256 × 3 bytes:  全局调色板 RGB

if (delay != 0):
   0x21 0xFF 0x0B "NETSCAPE2.0"
   0x03                       # 子块 3 字节
   0x01                       # sub-block ID
   0x00 0x00                  # loop count LE16 (0=无限)
   0x00                       # block terminator
```

##### (b) `GifDitherImage` 量化每帧

```cpp
for (f) {
    GifDitherImage(/*lastFrame=*/nullptr,         // 不做帧间差分 (每帧整体重写)
                   frames[f].data(),              // 输入 RGBA
                   outBuf.data(),                 // 输出: idx 写在 [i*4+3] 即 alpha 字节位
                   width, height, &pal);
    // outBuf 的 R/G/B 三字节是噪声(dither 残留), 调色板 idx 在第 4 字节
    writeFrameWithGlobalPalette(fp, outBuf, w, h, delay, pal);
}
```

Floyd-Steinberg dither 算法:
1. 每像素查 pal 找最近色 (k-d tree)
2. 误差 (orig - quant) 按 [7/16, 3/16, 5/16, 1/16] 散布到右/下三邻居
3. 让低色域过渡看起来"有更多色"

##### (c) `writeFrameWithGlobalPalette` 帧体

```
0x21 0xF9 0x04                             # GCE
0x05                                        # disposal=keep, transparency 标志
delay LE16
kGifTransIndex (=0)                         # 透明色 idx
0x00                                        # block terminator

0x2C                                        # Image Descriptor
0x00 0x00 0x00 0x00                         # left, top
width LE16, height LE16
0x00                                        # NO LCT (用全局), no interlace, no sort

minCodeSize (=8)                            # LZW
─── LZW data blocks ───────────────────────
[clear code] [pixel idx stream...] [EOI]
─────────────────────────────────────────
0x00                                        # image block terminator
```

LZW 编码主循环 (复用 gif.h 的 GifLzwNode + GifWriteCode):
```
for each pixel idx:
  if curCode == -1: curCode = idx
  elif tree[curCode].next[idx] 非空: curCode = tree[curCode].next[idx]    # 字典命中
  else:
    emit curCode
    add 新条目 tree[curCode].next[idx] = ++maxCode
    if maxCode 溢出当前 codeSize: 升 codeSize
    if maxCode == 4095: emit clear, 清树, 重置 codeSize
    curCode = idx
emit final curCode, clear, EOI
flush bit buffer
```

#### Step 5 — Trailer + 中文路径中转

```cpp
fputc(0x3B, fp);     // GIF trailer
fclose(fp);

// 由于 gif.h 的 fopen 不支持 utf-8 中文路径,
// 临时写到 %TEMP%/hplut_gif_tmp.gif (英文), 然后 QFile::copy 到目标.
QFile::copy(tmpEnglish, opts.outPath);
QFile::remove(tmpEnglish);
```

#### Step 6 — 循环次数后处理

如果 `loopCount != 0` (用户要求播 N 次):

```cpp
QFile gf(opts.outPath); gf.open(ReadWrite);
QByteArray buf = gf.readAll();
int idx = buf.indexOf("NETSCAPE2.0");
if (idx >= 0) {
    // 字节布局: NETSCAPE2.0 (11) + 0x03 + 0x01 + LoopLo + LoopHi + 0x00
    // 偏移: idx+13 = LoopLo, idx+14 = LoopHi
    int n = qBound(0, opts.loopCount, 0xFFFF);
    buf[idx+13] = n & 0xFF;
    buf[idx+14] = (n >> 8) & 0xFF;
    gf.seek(0); gf.write(buf);
}
```

> NETSCAPE 2.0 字节语义在播放器实现间略有差异 (通常 0=无限, N=循环 N 次, 部分播放器 N=循环 N 次外加首播 = 总播 N+1 次).

---

## 五、与画布预览的一致性

### 5.1 渲染路径完全相同

GIF 编码用 `renderOnce` 回调 → PreviewPanel::render() → FrameRenderer::renderCells():

- 相同的 cells 布局 (cols×rows, gapX/gapY)
- 相同的三个 PS shader 分支 (effect_chain / recolor / fullscreen_quad)
- 相同的 BlendState (SRC_ALPHA / INV_SRC_ALPHA, Straight Alpha)
- 相同的 sRGB 字节空间合成 (Gamma, RTV = _UNORM 非 _SRGB)
- 相同的 label 绘制 (cell viewport 内 顶部偏移 m_labelGapY)

### 5.2 唯一差异

| 项 | 画布 | GIF |
|----|------|-----|
| zoom | 用户可调 (Ctrl 滚轮) | 锁 1.0 |
| pan | 用户可拖 | 0 |
| RTV size | D3DWidget 实际像素 (随窗口) | gridW + 80 / gridH + 80 |
| 像素精度 | 8-bit per channel × 24M 色 | 256 色全局调色板 + dither |

---

## 六、Level 1 vs Level 2 对比

| 维度 | Level 1 (gif.h 默认) | Level 2 (本实现) |
|------|---------------------|------------------|
| 调色板 | 每帧独立 256 色 | 一份全局 256 色 |
| 帧间颜色一致性 | 差 (帧间调色板不同 → 颜色跳变, 看着闪烁) | 好 (同色用同 idx) |
| 半透明边缘 | 颗粒色块明显 | 平滑得多 |
| LCT | 每帧带 (字节冗余) | 不带, 全用 GCT |
| 文件大小 | 各帧独立, 略大 | 略小 (无 LCT) 或略大 (dither 噪点不利 LZW) |
| 编码速度 | 快 (单帧本地量化) | 慢 (扫所有帧建调色板) |
| RAM | 单帧缓冲 (~ 几 MB) | 全部帧缓存 (N×W×H×4 MB) |
| API | `GifBegin/WriteFrame/End` | 自写 `writeGifHeader/writeFrameWithGlobalPalette` |

---

## 七、参数与默认值

| 参数 | 默认 | 来源 / 范围 | 备注 |
|------|------|-----------|------|
| `width × height` | 自动 | gridW + 80 × gridH + 80 | 不限大小 (用户要求) |
| `fps` | 当前预览 fps (默认 10) | 1~50 | `delay = 100/fps` 厘秒 |
| `loopCount` | 0 (无限) | 0~9999 | NETSCAPE LE16 |
| `bgColor` | 用户预览背景 | AppSettings | 已在 Gamma 合成层叠到帧 |
| `showLabel` | true | toolbar `GIF ID` | 影响 cell 渲染时 label 是否绘 |
| `zoom` | 强制 1.0 | — | 与画布解耦 |
| `pad` | 40px 上下左右 | 写死 | 防角色光晕被裁 |
| RAM 上限 | 2 GB | 写死 | 超过报错防爆 |

---

## 八、性能数据估算

资源 8326 (`body+00+01`), `stand` 12 帧, schemes=14 (2 行 7 列):

```
cellW = cellH = 500
gridW ≈ 6*200 + 500 = 1700
gridH ≈ 1*240 + 500 = 740
gifW = 1780, gifH = 820
单帧 RGBA = 1780*820*4 ≈ 5.84 MB
12 帧 RAM ≈ 70 MB

GifMakePalette 一次扫描 12*1780*820 ≈ 17.5 M pixels:
  median-cut k-d tree, ~150 ms (RTX 4090 / i9-13900K, 不上 GPU 的 CPU 串行)
GifDitherImage × 12 ≈ 12 × 35 ms = 420 ms
LZW 写入 ≈ 12 × 50 ms = 600 ms
合计 ≈ 1.2 s, 输出 .gif ~ 4~8 MB
```

如果 schemes=28 (4 行 7 列), 单帧 ~12 MB, 12 帧 ~140 MB, 总耗时 ~3 s.

---

## 九、已知限制 / 后续优化点

### 9.1 已知问题

- **256 色全局调色板对多方案大画布仍偏紧**: 14+ 方案颜色跨度大时, 半透明区还是会出现轻微色带
- **dither 噪点在静帧背景明显**: Floyd-Steinberg 抖动让原本平整的灰背景出现颗粒
- **不做帧间差分**: 每帧都是整体重画 (`disposal=keep` + 全帧重写), GIF 文件偏大. gif.h 自带的差分仅在 `dither=false` 时启用.
- **LZW 是单线程**: 大 GIF 主线程阻塞 ~1-3 秒, UI 不响应

### 9.2 Level 3 候选优化

| # | 升级项 | 收益 | 难度 |
|---|--------|------|------|
| 1 | 每 cell 独立调色板 (写多 sub-image, 每帧写多个 LCT) | 颜色精度大涨 | 高 (改 LZW 多次写, 调坐标) |
| 2 | NeuQuant 神经网络量化代替 median-cut | 调色板更接近视觉重要色 | 中 |
| 3 | 不要 dither (`GifThresholdImage`), 改用错误扩散到调色板预生成阶段 | 静帧无颗粒 | 低 |
| 4 | 帧间差分 (后帧只写改变区域, disposal=do-not-dispose + 透明色填空) | 文件大幅缩水 | 中 |
| 5 | 异步导出 (`QtConcurrent::run`) + 进度对话框 | UI 不卡 | 低 |
| 6 | 可选输出 APNG / WebP (无 256 色限制) | 画质 ≈ PNG | 中 (要 libpng/libwebp) |

### 9.3 dither 关闭对比

如果 GIF 静帧噪点无法接受, 改 `GifDitherImage` → `GifThresholdImage`:
- 颜色稳, 静帧无噪点
- 但低色域过渡区有可见色阶

---

## 十、调试入口

### 10.1 验证渲染输出 (在 GifExporter 之外)

```cpp
// PreviewPanel::onExportGif 里, GifExporter 调用前先 dump 一帧 PNG 看
DebugDumper::dumpRtv(rtv, opts.width, opts.height, "X:/dump/gif_frame_0.png");
```

### 10.2 验证调色板

`GifPalette::r/g/b[256]` 是 RGB 数组. dump 成 16×16 256 色 swatch png 一目了然.

### 10.3 查文件结构

用十六进制查看器看 GIF 头 + LCT/GCT 字节:

```
GIF89a [W LE2] [H LE2]
[Packed]                      0xF7 = GCT + 24-bit + 256 entries
[BG idx] [Aspect ratio]
... 256 × 3 RGB ...
21 FF 0B "NETSCAPE2.0" 03 01 [Loop LE2] 00      ← 循环块
21 F9 04 05 [Delay LE2] [TransIdx] 00           ← GCE for frame 1
2C [L LE2] [T LE2] [W LE2] [H LE2] [Packed=0]    ← Image Desc, Packed=0 = 无 LCT
[minCodeSize] ...LZW data...                     ← 帧数据
... 重复每帧 ...
3B                                                ← Trailer
```

---

## 十一、文件改动总览

```
extern/gif/gif.h                       新增 (3rd-party single-header)
CMakeLists.txt                         加 extern_gif interface lib + APP_SOURCES 加 GifExporter
src/render/GifExporter.h               接口
src/render/GifExporter.cpp             Level 2 实现 (writeGifHeader / writeFrameWithGlobalPalette / exportGif)
src/ui/PreviewPanel.h                  m_gifLoopSpin / m_gifIdChk / m_gifExportBtn / m_gifLoop / m_gifShowId
src/ui/PreviewPanel.cpp                toolbar 第 3 行 + onExportGif 编排
```

---

## 十二、API 调用链速览

```
[UI 用户点按钮]
    │
    ├── PreviewPanel::onExportGif()
    │     ├── QFileDialog::getSaveFileName              // 选路径
    │     ├── 推断 baseW/baseH (FrameLoader.get)
    │     ├── 算 gridW/gridH/gifW/gifH
    │     ├── 临时锁 zoom/showLabel/playing
    │     ├── 准备 lambda renderOnce / setFrame
    │     ├── GifExporter::exportGif(...)  ★
    │     │     ├── 创建 RTV + staging
    │     │     ├── for f: setFrame(f); renderOnce(); readback → frames[f]
    │     │     ├── 拼 bigImage; GifMakePalette → pal
    │     │     ├── fopen(tmpEnglish)
    │     │     ├── writeGifHeader(fp, w, h, delay, pal)
    │     │     ├── for f: GifDitherImage(NULL, frame, out, w, h, &pal);
    │     │     │       writeFrameWithGlobalPalette(fp, out, w, h, delay, pal);
    │     │     ├── fputc(0x3B); fclose;
    │     │     ├── QFile::copy(tmp → 用户路径)
    │     │     └── 后处理 NETSCAPE 字节 (loopCount != 0 时)
    │     ├── 还原 zoom/showLabel/playing/frame
    │     └── QMessageBox::information("写出 N 帧, X KB")
```

---

## 十三、术语速查

| 缩写 | 含义 |
|-----|-----|
| **LSD** | Logical Screen Descriptor (GIF 文件级元数据) |
| **GCT** | Global Color Table (整文件共用调色板, 写在 LSD 后) |
| **LCT** | Local Color Table (单帧自带调色板, 紧跟 Image Descriptor) |
| **GCE** | Graphics Control Extension (帧前的 disposal/delay/transparency 控制) |
| **NETSCAPE 2.0** | 早期 Netscape 浏览器扩展, 用于 loop count |
| **LZW** | Lempel-Ziv-Welch 字典压缩, GIF 唯一支持的压缩算法 |
| **median-cut** | 颜色量化算法: 不停切色彩立方体最长边, 256 个 cell 各取均色 → 256 调色板 |
| **Floyd-Steinberg** | 经典误差扩散 dither, 把量化误差按 [7,3,5,1]/16 散到右/下邻像素 |
| **k-d tree** | 多维空间二分搜索树, 在 256 色调色板里 O(log n) 找最近色 |
| **PMA** | Premultiplied Alpha (本项目不用) |

---

## 十四、开发环境 (GIF 模块相关)

> 完整环境参见 `00_开发环境.md`。此处仅列 GIF 导出功能依赖的关键项。

### 14.1 工具链

| 工具 | 版本 (实测) | 用途 |
|------|-----------|------|
| Visual Studio 2026 Community | cl 19.51.36243 (x64) | 编译 gif.h + GifExporter.cpp |
| CMake | 3.30.5 | `extern_gif` interface lib 注册 |
| Ninja | 1.12.1 | 增量编译 GifExporter.cpp |
| Qt | 6.11.1 msvc2022_64 | `QFile` 中文路径中转 / `QFileDialog` / `QSpinBox` |
| Windows SDK | 10.0.26100.0 | D3D11 离屏 RTV + staging readback |

### 14.2 第三方库

| 库 | 版本 | 文件 | 许可 | 用途 |
|----|------|------|------|------|
| **gif.h** | master (2024-06-xx) | `extern/gif/gif.h` | **Public Domain** | GIF89a 编码: LZW / median-cut 调色板 / Floyd-Steinberg dither |
| stb_image | v2.30 | `extern/stb/stb_image.h` | PD | (GIF 模块不直接用, 但 FrameLoader 用) |
| stb_image_write | (随 stb) | `extern/stb/stb_image_write.h` | PD | (GIF 模块不直接用) |

**gif.h 来源**: <https://github.com/charlietangora/gif-h>  
**单 header, ~28 KB, 800 行**. 编进 EXE 后零外部依赖.

### 14.3 系统库 (D3D11)

GIF 导出复用项目已有的 D3D11 渲染管线:

| 库 | 用途 |
|----|------|
| `d3d11.dll` | 离屏 `ID3D11Texture2D` (RTV + Staging) + `ID3D11RenderTargetView` |
| `dxgi.dll` | SwapChain 间接 (GifExporter 不直接碰 DXGI, 但 D3D11Context 单例经 DXGI 创建设备) |

### 14.4 CMake 集成

`CMakeLists.txt` 新增:

```cmake
# 第三方 header
add_library(extern_gif INTERFACE)
target_include_directories(extern_gif INTERFACE ${CMAKE_SOURCE_DIR}/extern/gif)

# 源文件
set(APP_SOURCES
    ...
    src/render/GifExporter.h
    src/render/GifExporter.cpp
    ...
)

# 链接
target_link_libraries(HighPro_LUT PRIVATE
    ...
    extern_gif
    ...
)
```

### 14.5 编译注意

| 现象 | 原因 | 处置 |
|------|------|------|
| `warning C4334: "<<": 32 位移位结果隐式转 64 位` (gif.h L400-401) | gif.h 内部 `1 << bitDepth` 在 64-bit 平台 warn | 可忽略 (不影响正确性). 若要清: `pragma warning(disable:4334)` |
| `#define GIF_MALLOC / GIF_FREE` 重定义 | gif.h 定义自己的 malloc/free | 在 include 前定义 `GIF_TEMP_MALLOC` / `GIF_TEMP_FREE` / `GIF_MALLOC` / `GIF_FREE` 为 `malloc/free` (已做) |
| 中文路径 fopen 失败 | gif.h 用 `fopen/fopen_s`, Windows 下 `fopen` 不支持 UTF-8 | 写到 `%TEMP%` 英文路径, 写完后 `QFile::copy` 到用户中文路径 (已做) |
| `stbi_image.h` 多重定义 (ODR violation) | gif.h 不依赖 stbi, 但如果同一 TU 包含两者需注意 `STB_IMAGE_IMPLEMENTATION` 仅定义一次 | GifExporter.cpp 只 `#include "gif.h"`, 不包含 stbi (已确保) |

### 14.6 运行时依赖

GIF 导出功能**无新增运行时依赖**:

- gif.h = header-only, 编译进 EXE
- D3D11 = 系统自带
- Qt = 已部署

> EXE 单文件可运行, 无需额外 DLL.

### 14.7 测试环境

| 指标 | 值 |
|------|---|
| CPU | Intel Core i9-13900K |
| GPU | NVIDIA GeForce RTX 4090 |
| RAM | 64 GB DDR5 |
| OS | Windows 11 24H2 (Build 26100.6899) |
| 测试资源 | `X:\Lut变色\测试资源文件\8326` (body/00/01, stand 12 帧, 500×500 TGA) |
| 实测导出 | 14 方案 (2×7), stand 12 帧, GIF 1780×820 → ~4 MB, ~1.2 s |

---

*文档生成时间: 2026-06-02*
