#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <QString>
#include <QByteArray>

namespace HighPro {

using Microsoft::WRL::ComPtr;

// 简易 VS+PS pair, 编译自 HLSL 源 (D3DCompile).
// 一期一张全屏 quad VS, 使用 SV_VertexID 生成顶点, 不需要 IA buffer.
class D3D11Shader
{
public:
    bool compile(const QByteArray& hlslSource,
                 const char* vsEntry,
                 const char* psEntry,
                 const char* psName,
                 QString* errorOut = nullptr);

    bool compileVS(const QByteArray& hlslSource, const char* entry, const char* name, QString* errorOut = nullptr);
    bool compilePS(const QByteArray& hlslSource, const char* entry, const char* name, QString* errorOut = nullptr);

    void release();
    bool isValid() const { return m_vs && m_ps; }

    ID3D11VertexShader* vs() const { return m_vs.Get(); }
    ID3D11PixelShader*  ps() const { return m_ps.Get(); }

private:
    static bool compileBlob(const QByteArray& src, const char* entry,
                            const char* target, const char* name,
                            ComPtr<ID3DBlob>& outBlob, QString* errorOut);

    ComPtr<ID3D11VertexShader> m_vs;
    ComPtr<ID3D11PixelShader>  m_ps;
};

} // namespace HighPro
