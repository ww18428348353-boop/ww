#pragma once

#include <QString>
#include <QByteArray>
#include <vector>
#include <cstdint>

namespace HighPro {

// 中文路径友好的文件读写工具.
// stb_image 在 Windows 下默认走 fopen, 中文路径在 MBCS 环境会失败.
// 统一通过 QFile 读字节流后再交给 stb_image_load_from_memory.
class PathUtil
{
public:
    // 读整个文件为字节数组. 失败返回空.
    static QByteArray readAll(const QString& utf8Path);

    // 写整个文件. 失败返回 false.
    static bool writeAll(const QString& utf8Path, const QByteArray& data);

    // utf-8 路径 → 宽字符 (Win32 API 需要)
    static std::wstring toWide(const QString& utf8Path);

    // 检查路径是否存在 (文件或目录)
    static bool exists(const QString& utf8Path);

    // 确保目录存在, 不存在则创建
    static bool ensureDir(const QString& utf8Path);

    // 拼接路径 (统一用 /)
    static QString join(const QString& base, const QString& sub);
};

} // namespace HighPro
