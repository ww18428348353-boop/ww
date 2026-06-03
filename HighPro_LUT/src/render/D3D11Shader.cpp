#include "D3D11Shader.h"
#include "D3D11Context.h"

#include <d3dcompiler.h>

namespace HighPro {

bool D3D11Shader::compileBlob(const QByteArray& src, const char* entry,
                              const char* target, const char* name,
                              ComPtr<ID3DBlob>& outBlob, QString* errorOut)
{
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

    ComPtr<ID3DBlob> err;
    HRESULT hr = D3DCompile(
        src.constData(), (SIZE_T)src.size(),
        name,                       // 用于错误信息显示
        nullptr, nullptr,
        entry, target, flags, 0,
        outBlob.GetAddressOf(), err.GetAddressOf());
    if (FAILED(hr)) {
        QString msg;
        if (err) {
            msg = QString::fromUtf8((const char*)err->GetBufferPointer(),
                                    (int)err->GetBufferSize());
        } else {
            msg = QString("D3DCompile 失败 0x%1")
                  .arg((quint32)hr, 8, 16, QChar('0'));
        }
        if (errorOut) *errorOut = msg;
        return false;
    }
    return true;
}

bool D3D11Shader::compileVS(const QByteArray& src, const char* entry,
                            const char* name, QString* errorOut)
{
    auto* dev = D3D11Context::instance().device();
    if (!dev) { if (errorOut) *errorOut = "Device 未初始化"; return false; }

    ComPtr<ID3DBlob> blob;
    if (!compileBlob(src, entry, "vs_5_0", name, blob, errorOut)) return false;

    HRESULT hr = dev->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(),
                                          nullptr, m_vs.GetAddressOf());
    if (FAILED(hr)) {
        if (errorOut) *errorOut = "CreateVertexShader 失败";
        return false;
    }
    return true;
}

bool D3D11Shader::compilePS(const QByteArray& src, const char* entry,
                            const char* name, QString* errorOut)
{
    auto* dev = D3D11Context::instance().device();
    if (!dev) { if (errorOut) *errorOut = "Device 未初始化"; return false; }

    ComPtr<ID3DBlob> blob;
    if (!compileBlob(src, entry, "ps_5_0", name, blob, errorOut)) return false;

    HRESULT hr = dev->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(),
                                         nullptr, m_ps.GetAddressOf());
    if (FAILED(hr)) {
        if (errorOut) *errorOut = "CreatePixelShader 失败";
        return false;
    }
    return true;
}

bool D3D11Shader::compile(const QByteArray& src, const char* vsEntry, const char* psEntry,
                          const char* name, QString* errorOut)
{
    return compileVS(src, vsEntry, name, errorOut) &&
           compilePS(src, psEntry, name, errorOut);
}

void D3D11Shader::release()
{
    m_vs.Reset();
    m_ps.Reset();
}

} // namespace HighPro
