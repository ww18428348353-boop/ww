#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <QString>

namespace HighPro {

using Microsoft::WRL::ComPtr;

// D3D11 全局上下文 (Device + ImmediateContext) + 多 SwapChain 工厂.
// 线程模型: Device 创建为 SINGLETHREADED (Qt UI 线程独占).
class D3D11Context
{
public:
    static D3D11Context& instance();

    // 初始化 Device. 第一次成功后保持复用.
    bool initialize(QString* errorOut = nullptr);
    void shutdown();
    bool isReady() const { return m_device != nullptr; }

    ID3D11Device*        device()        const { return m_device.Get(); }
    ID3D11DeviceContext* context()       const { return m_context.Get(); }
    IDXGIFactory2*       factory()       const { return m_factory.Get(); }
    D3D_FEATURE_LEVEL    featureLevel()  const { return m_featureLevel; }

    // 为指定 HWND 创建 SwapChain (FLIP_DISCARD, 双缓冲, sRGB)
    bool createSwapChainForHwnd(HWND hwnd,
                                UINT width,
                                UINT height,
                                IDXGISwapChain1** outSwap,
                                QString* errorOut = nullptr);

private:
    D3D11Context() = default;
    ~D3D11Context() { shutdown(); }
    D3D11Context(const D3D11Context&) = delete;
    D3D11Context& operator=(const D3D11Context&) = delete;

    ComPtr<ID3D11Device>        m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<IDXGIFactory2>       m_factory;
    D3D_FEATURE_LEVEL           m_featureLevel{ D3D_FEATURE_LEVEL_11_0 };
};

} // namespace HighPro
