#pragma once

#include <QWidget>
#include <QTimer>
#include <QColor>

#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <functional>

namespace HighPro {

using Microsoft::WRL::ComPtr;

// D3D11 渲染面板. Qt 不参与绘制, 我们用 SwapChain Present.
//
// 使用:
//   widget->setRenderCallback([](ID3D11RenderTargetView* rtv, int w, int h){
//       // 自己用 D3D11Context::context() 画
//   });
//
// M5: 内置 AE 风格画布交互 (pan/zoom).
//   中键拖 → pan; 滚轮 → 跳档缩放, 鼠标位为锚.
//   外部用 contentZoom() / contentPan() 把内容矩形映射到 RTV 像素.
class D3DWidget : public QWidget
{
    Q_OBJECT

public:
    using RenderFn = std::function<void(ID3D11RenderTargetView* rtv, int w, int h)>;

    explicit D3DWidget(QWidget* parent = nullptr);
    ~D3DWidget() override;

    void setClearColor(const QColor& c);
    QColor clearColor() const { return m_clearColor; }

    // 设置渲染回调 (每帧由它绘制. 如果不设, 只清屏).
    void setRenderCallback(RenderFn fn) { m_render = std::move(fn); }

    // 强制立刻重绘
    void requestRender();

    // 关闭/启动定时驱动. 默认开 60fps.
    void setAutoRender(bool on);

    QPaintEngine* paintEngine() const override { return nullptr; }

    // 调试: 拿到当前 swapchain 的 RTV 与物理像素大小
    ID3D11RenderTargetView* currentRtv() const { return m_rtv.Get(); }
    int  rtvWidth()  const { return (int)m_lastWidth; }
    int  rtvHeight() const { return (int)m_lastHeight; }

    // === 视图变换 (M5) ===
    double contentZoom()  const { return m_zoom; }
    double contentPanX()  const { return m_panX; }
    double contentPanY()  const { return m_panY; }
    void   setContentZoom(double z, QPointF anchorWidgetPx = QPointF(-1,-1));
    void   resetView();      // zoom=1, pan=0 (居中)
    void   fitView(int contentW, int contentH);  // 适合窗口

signals:
    void viewTransformChanged();   // pan/zoom 变化, PreviewPanel 重算 cells 用

protected:
    void resizeEvent(QResizeEvent* e) override;
    void showEvent(QShowEvent* e) override;
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    bool event(QEvent* e) override;

private slots:
    void onTick();

private:
    bool ensureInit();
    bool ensureRenderTarget();
    void doRender();
    void releaseRenderTarget();
    int  zoomStepIndex(double z) const;
    double snapZoom(double z) const;

    ComPtr<IDXGISwapChain1>          m_swap;
    ComPtr<ID3D11RenderTargetView>   m_rtv;

    QTimer m_timer;
    QColor m_clearColor{ 60, 60, 60 };
    UINT   m_lastWidth  = 0;
    UINT   m_lastHeight = 0;
    bool   m_initOk     = false;

    RenderFn m_render;

    // 视图变换
    double m_zoom = 1.0;
    double m_panX = 0.0;
    double m_panY = -60.0;     // 默认上移 60px (toolbar 高 → 视觉补偿)

    // 平移交互
    bool   m_panning = false;
    QPoint m_panStartPt;
    double m_panStartX = 0.0;
    double m_panStartY = 0.0;
};

} // namespace HighPro
