#include "D3DWidget.h"
#include "render/D3D11Context.h"

#include <QResizeEvent>
#include <QShowEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QDebug>
#include <array>
#include <cmath>

namespace HighPro {

D3DWidget::D3DWidget(QWidget* parent) : QWidget(parent)
{
    // Qt 不绘 — 我们独占
    setAttribute(Qt::WA_PaintOnScreen, true);
    setAttribute(Qt::WA_NativeWindow,  true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_OpaquePaintEvent, true);

    // 默认 ~60 fps
    m_timer.setInterval(16);
    connect(&m_timer, &QTimer::timeout, this, &D3DWidget::onTick);
    m_timer.start();
}

D3DWidget::~D3DWidget()
{
    m_timer.stop();
    releaseRenderTarget();
    m_swap.Reset();
}

void D3DWidget::setClearColor(const QColor& c)
{
    m_clearColor = c;
    requestRender();
}

void D3DWidget::requestRender()
{
    if (m_initOk) doRender();
}

void D3DWidget::setAutoRender(bool on)
{
    if (on) {
        if (!m_timer.isActive()) m_timer.start();
    } else {
        m_timer.stop();
    }
}

void D3DWidget::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    ensureInit();
}

void D3DWidget::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    if (m_initOk) {
        const QSize sz = e->size() * devicePixelRatioF();
        const UINT w = (UINT)qMax(1, sz.width());
        const UINT h = (UINT)qMax(1, sz.height());
        if (w != m_lastWidth || h != m_lastHeight) {
            releaseRenderTarget();
            HRESULT hr = m_swap->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0);
            if (FAILED(hr)) {
                qWarning() << "[D3DWidget] ResizeBuffers failed: 0x"
                           << QString::number((quint32)hr, 16);
            }
            m_lastWidth  = w;
            m_lastHeight = h;
            ensureRenderTarget();
        }
        doRender();
    }
}

void D3DWidget::paintEvent(QPaintEvent*)
{
    if (m_initOk) doRender();
}

bool D3DWidget::event(QEvent* e)
{
    return QWidget::event(e);
}

bool D3DWidget::ensureInit()
{
    if (m_initOk) return true;

    auto& ctx = D3D11Context::instance();
    if (!ctx.isReady()) {
        QString err;
        if (!ctx.initialize(&err)) {
            qCritical() << "[D3DWidget] D3D11Context init failed:" << err;
            return false;
        }
    }

    HWND hwnd = (HWND)winId();
    if (!hwnd) return false;

    const QSize sz = size() * devicePixelRatioF();
    m_lastWidth  = (UINT)qMax(1, sz.width());
    m_lastHeight = (UINT)qMax(1, sz.height());

    QString err;
    IDXGISwapChain1* sc = nullptr;
    if (!ctx.createSwapChainForHwnd(hwnd, m_lastWidth, m_lastHeight, &sc, &err)) {
        qCritical() << "[D3DWidget] swap chain failed:" << err;
        return false;
    }
    m_swap.Attach(sc);

    if (!ensureRenderTarget()) return false;

    m_initOk = true;
    return true;
}

bool D3DWidget::ensureRenderTarget()
{
    if (!m_swap) return false;
    if (m_rtv) return true;

    auto& ctx = D3D11Context::instance();

    ComPtr<ID3D11Texture2D> back;
    HRESULT hr = m_swap->GetBuffer(0, IID_PPV_ARGS(back.GetAddressOf()));
    if (FAILED(hr)) {
        qWarning() << "[D3DWidget] GetBuffer failed";
        return false;
    }

    // RTV format = _UNORM (非 _SRGB): GPU 不做 linear→sRGB encode, 写入即字节值.
    // 配合 PS 直接输出 sRGB 字节 (不做 srgbToLinear), 实现 Gamma 空间 alpha 合成 —
    // 与 AE 默认行为 (未勾 "Blend Colors Using 1.0 Gamma") 视觉一致, 避免线性空间
    // 合成造成的半透明区"光晕亮度抬升 / 溢出"问题.
    D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Texture2D.MipSlice = 0;

    hr = ctx.device()->CreateRenderTargetView(back.Get(), &rtvDesc, m_rtv.GetAddressOf());
    if (FAILED(hr)) {
        qWarning() << "[D3DWidget] CreateRTV failed";
        return false;
    }
    return true;
}

void D3DWidget::releaseRenderTarget()
{
    m_rtv.Reset();
}

void D3DWidget::doRender()
{
    if (!m_swap || !m_rtv) return;

    auto& ctx = D3D11Context::instance();
    auto* dc = ctx.context();

    if (m_render) {
        // 外部回调负责清屏 + 全部绘制
        m_render(m_rtv.Get(), (int)m_lastWidth, (int)m_lastHeight);
    } else {
        // 默认: 仅清屏
        const float bg[4] = {
            (float)m_clearColor.redF(),
            (float)m_clearColor.greenF(),
            (float)m_clearColor.blueF(),
            1.0f
        };
        ID3D11RenderTargetView* rtvs[] = { m_rtv.Get() };
        dc->OMSetRenderTargets(1, rtvs, nullptr);
        D3D11_VIEWPORT vp{};
        vp.TopLeftX = 0; vp.TopLeftY = 0;
        vp.Width = (FLOAT)m_lastWidth;
        vp.Height = (FLOAT)m_lastHeight;
        vp.MinDepth = 0; vp.MaxDepth = 1;
        dc->RSSetViewports(1, &vp);
        dc->ClearRenderTargetView(m_rtv.Get(), bg);
    }

    HRESULT hr = m_swap->Present(1, 0);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        qWarning() << "[D3DWidget] device lost, recreate.";
        releaseRenderTarget();
        m_swap.Reset();
        m_initOk = false;
        ensureInit();
    }
}

void D3DWidget::onTick()
{
    if (!isVisible()) return;
    if (!m_initOk) {
        ensureInit();
        if (!m_initOk) return;
    }
    doRender();
}

// === 视图变换 ===

namespace {
// AE/PS 风格档位 (1.5% .. 800%)
const std::array<double, 17> kZoomSteps = {
    0.015, 0.031, 0.0625, 0.125, 0.25, 0.333, 0.5,
    0.667, 1.0, 1.5, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0
};
}

int D3DWidget::zoomStepIndex(double z) const
{
    int best = 0;
    double bestD = std::abs(kZoomSteps[0] - z);
    for (int i = 1; i < (int)kZoomSteps.size(); ++i) {
        double d = std::abs(kZoomSteps[i] - z);
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

double D3DWidget::snapZoom(double z) const
{
    return kZoomSteps[zoomStepIndex(z)];
}

void D3DWidget::setContentZoom(double z, QPointF anchorWidgetPx)
{
    double newZoom = std::clamp(z, kZoomSteps.front(), kZoomSteps.back());
    if (std::abs(newZoom - m_zoom) < 1e-6) return;

    // 锚点保持: 锚点对应的"内容坐标"在缩放前后不变.
    // contentX = (anchor.x - cx - panX) / zoom    其中 cx = widget 中心
    if (anchorWidgetPx.x() < 0) {
        anchorWidgetPx = QPointF(width() * 0.5, height() * 0.5);
    }
    const double cx = width() * 0.5;
    const double cy = height() * 0.5;
    const double anchorContentX = (anchorWidgetPx.x() - cx - m_panX) / m_zoom;
    const double anchorContentY = (anchorWidgetPx.y() - cy - m_panY) / m_zoom;

    m_zoom = newZoom;
    // 重算 pan 让锚点对齐
    m_panX = anchorWidgetPx.x() - cx - anchorContentX * m_zoom;
    m_panY = anchorWidgetPx.y() - cy - anchorContentY * m_zoom;

    emit viewTransformChanged();
    requestRender();
}

void D3DWidget::resetView()
{
    m_zoom = 1.0;
    m_panX = 0.0;
    m_panY = -60.0;     // 画布默认上移 60px (toolbar 占用底部空间, 让角色更居中视野)
    emit viewTransformChanged();
    requestRender();
}

void D3DWidget::fitView(int contentW, int contentH)
{
    if (contentW <= 0 || contentH <= 0) { resetView(); return; }
    const double sx = double(width())  / contentW;
    const double sy = double(height()) / contentH;
    double z = std::min(sx, sy);
    z = std::clamp(z, kZoomSteps.front(), kZoomSteps.back());
    m_zoom = z;
    m_panX = 0.0;
    m_panY = 0.0;
    emit viewTransformChanged();
    requestRender();
}

void D3DWidget::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::MiddleButton) {
        m_panning = true;
        m_panStartPt = e->pos();
        m_panStartX = m_panX;
        m_panStartY = m_panY;
        setCursor(Qt::ClosedHandCursor);
        e->accept();
        return;
    }
    QWidget::mousePressEvent(e);
}

void D3DWidget::mouseMoveEvent(QMouseEvent* e)
{
    if (m_panning) {
        const QPoint d = e->pos() - m_panStartPt;
        m_panX = m_panStartX + d.x();
        m_panY = m_panStartY + d.y();
        emit viewTransformChanged();
        requestRender();
        e->accept();
        return;
    }
    QWidget::mouseMoveEvent(e);
}

void D3DWidget::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() == Qt::MiddleButton && m_panning) {
        m_panning = false;
        unsetCursor();
        e->accept();
        return;
    }
    QWidget::mouseReleaseEvent(e);
}

void D3DWidget::wheelEvent(QWheelEvent* e)
{
    const int delta = e->angleDelta().y();
    if (delta == 0) { e->ignore(); return; }

    int idx = zoomStepIndex(m_zoom);
    if (delta > 0) idx = std::min(idx + 1, (int)kZoomSteps.size() - 1);
    else           idx = std::max(idx - 1, 0);
    setContentZoom(kZoomSteps[idx], e->position());
    e->accept();
}

void D3DWidget::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) {
        resetView();
        e->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(e);
}

} // namespace HighPro
