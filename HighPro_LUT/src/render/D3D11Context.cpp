#include "D3D11Context.h"

#include <dxgi1_3.h>
#include <QDebug>

namespace HighPro {

static QString hrToString(HRESULT hr)
{
    return QString("HRESULT 0x%1").arg((quint32)hr, 8, 16, QChar('0'));
}

D3D11Context& D3D11Context::instance()
{
    static D3D11Context ctx;
    return ctx;
}

bool D3D11Context::initialize(QString* errorOut)
{
    if (m_device) return true;

    UINT flags = 0;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    flags |= D3D11_CREATE_DEVICE_SINGLETHREADED;
    flags |= D3D11_CREATE_DEVICE_BGRA_SUPPORT; // 与 DXGI_FORMAT_B8G8R8A8 保持兼容

    const D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };

    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        flags,
        levels, ARRAYSIZE(levels),
        D3D11_SDK_VERSION,
        m_device.GetAddressOf(),
        &m_featureLevel,
        m_context.GetAddressOf());

    // 调试层不可用时退回
    if (FAILED(hr) && (flags & D3D11_CREATE_DEVICE_DEBUG)) {
        flags &= ~D3D11_CREATE_DEVICE_DEBUG;
        hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
            levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
            m_device.GetAddressOf(), &m_featureLevel,
            m_context.GetAddressOf());
    }

    if (FAILED(hr)) {
        if (errorOut) *errorOut = QString("D3D11CreateDevice 失败: %1").arg(hrToString(hr));
        return false;
    }

    // 拿到 DXGI Factory (从 Device 获取)
    ComPtr<IDXGIDevice> dxgiDev;
    hr = m_device.As(&dxgiDev);
    if (FAILED(hr)) {
        if (errorOut) *errorOut = QString("查询 IDXGIDevice 失败: %1").arg(hrToString(hr));
        return false;
    }

    ComPtr<IDXGIAdapter> adapter;
    hr = dxgiDev->GetAdapter(adapter.GetAddressOf());
    if (FAILED(hr)) {
        if (errorOut) *errorOut = QString("GetAdapter 失败: %1").arg(hrToString(hr));
        return false;
    }

    hr = adapter->GetParent(__uuidof(IDXGIFactory2), (void**)m_factory.GetAddressOf());
    if (FAILED(hr)) {
        if (errorOut) *errorOut = QString("GetParent IDXGIFactory2 失败: %1").arg(hrToString(hr));
        return false;
    }

    qInfo() << "[D3D11] Device created, FL =" << (int)m_featureLevel;
    return true;
}

void D3D11Context::shutdown()
{
    if (m_context) {
        m_context->ClearState();
        m_context->Flush();
    }
    m_factory.Reset();
    m_context.Reset();
    m_device.Reset();
}

bool D3D11Context::createSwapChainForHwnd(HWND hwnd, UINT width, UINT height,
                                          IDXGISwapChain1** outSwap, QString* errorOut)
{
    if (!m_factory || !m_device) {
        if (errorOut) *errorOut = "D3D11Context 未初始化";
        return false;
    }
    if (width == 0)  width = 1;
    if (height == 0) height = 1;

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width  = width;
    desc.Height = height;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;       // FLIP_* 不允许 _SRGB, 用 RTV 时再 sRGB
    desc.Stereo = FALSE;
    desc.SampleDesc.Count   = 1;
    desc.SampleDesc.Quality = 0;
    desc.BufferUsage  = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount  = 2;
    desc.Scaling      = DXGI_SCALING_STRETCH;
    desc.SwapEffect   = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode    = DXGI_ALPHA_MODE_IGNORE;
    desc.Flags        = 0;

    HRESULT hr = m_factory->CreateSwapChainForHwnd(
        m_device.Get(), hwnd, &desc, nullptr, nullptr, outSwap);
    if (FAILED(hr)) {
        if (errorOut) *errorOut = QString("CreateSwapChainForHwnd 失败: %1").arg(hrToString(hr));
        return false;
    }
    // 关闭 Alt+Enter 全屏切换 (Qt 主导)
    m_factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
    return true;
}

} // namespace HighPro
