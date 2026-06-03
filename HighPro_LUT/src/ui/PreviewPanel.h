#pragma once

#include <QWidget>
#include <QTimer>
#include <memory>

#include <Windows.h>
#include <d3d11.h>

class QComboBox;
class QSpinBox;
class QLabel;
class QPushButton;
class QButtonGroup;
class QCheckBox;

namespace HighPro {

class D3DWidget;
class FrameRenderer;

// 中央预览面板:
//   ┌────────────────────────────────────┐
//   │   [D3DWidget]                      │
//   │                                    │
//   ├────────────────────────────────────┤
//   │ 动作:[stand v] 方向:1 2 3 4 5 6 7 8│
//   │ 帧率:[10 ] ⏯ 当前:[ 5/12]          │
//   │ 背景:[#3C3C3C] [图...] [×]         │
//   └────────────────────────────────────┘
class PreviewPanel : public QWidget
{
    Q_OBJECT
public:
    explicit PreviewPanel(QWidget* parent = nullptr);
    ~PreviewPanel() override;

    D3DWidget* canvas() const { return m_canvas; }

private slots:
    void onProjectLoaded();
    void onActionChanged();
    void onDirectionChanged();
    void onFrameChanged();

    void onActionCombo(int idx);
    void onDirButton(int dirId);
    void onPlayPause();
    void onFpsChanged(int v);
    void onPickBgColor();
    void onPickBgImage();
    void onClearBgImage();
    void onTick();
    void onExportGif();

private:
    void buildUi();
    void connectSignals();
    void rebuildDirButtons();
    void updateFrameLabel();
    void updateZoomLabel();
    void prefetchUpcomingFrames(int aheadN = 8);   // M9: 异步预读后续 N 帧
    void render(ID3D11RenderTargetView* rtv, int w, int h);

    D3DWidget*    m_canvas{ nullptr };
    QComboBox*    m_actionCombo{ nullptr };
    QButtonGroup* m_dirGroup{ nullptr };
    QWidget*      m_dirRow{ nullptr };
    QSpinBox*     m_fpsSpin{ nullptr };
    QPushButton*  m_playBtn{ nullptr };
    QLabel*       m_frameLabel{ nullptr };
    QPushButton*  m_bgColorBtn{ nullptr };
    QPushButton*  m_bgImageBtn{ nullptr };
    QPushButton*  m_bgClearBtn{ nullptr };
    QLabel*       m_zoomLabel{ nullptr };  // M5: AE 风格百分比 HUD
    QSpinBox*     m_gapXSpin{ nullptr };   // M5: X 轴间距
    QSpinBox*     m_gapYSpin{ nullptr };   // M5: Y 轴间距
    QCheckBox*    m_showLabelChk{ nullptr }; // 画布方案 ID 显示开关
    QSpinBox*     m_labelGapYSpin{ nullptr }; // 方案 ID Y 间距 (label 距头顶, px)
    QPushButton*  m_fullCanvasBtn{ nullptr }; // 全屏画布开关 (≡ Ctrl+Space)
    bool          m_showLabel = true;
    int           m_labelGapY = 200;        // 默认: label 离 cell 顶部 200px

    // GIF 输出
    QSpinBox*     m_gifLoopSpin{ nullptr };  // 循环次数 (0=无限, 1=播 1 次)
    QCheckBox*    m_gifIdChk{ nullptr };     // GIF 是否包含方案 ID label
    QPushButton*  m_gifExportBtn{ nullptr }; // 输出 GIF 按钮
    int           m_gifLoop  = 0;          // 默认无限循环
    bool          m_gifShowId = true;

    QTimer        m_playTimer;
    bool          m_playing = true;

    int           m_charGapXPx = -300;     // 默认 (针对 500×500 TGA 的视觉合适值)
    int           m_charGapYPx = -260;     // 默认 (上下行重叠 ~半身)

    // M5: 选中标识闪烁
    QTimer        m_blinkTimer;
    int           m_blinkPhaseMs = 0;

    std::unique_ptr<FrameRenderer> m_renderer;
};

} // namespace HighPro
