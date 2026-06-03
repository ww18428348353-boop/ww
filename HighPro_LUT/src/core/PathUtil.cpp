#include "PathUtil.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>

namespace HighPro {

QByteArray PathUtil::readAll(const QString& utf8Path)
{
    QFile f(utf8Path);
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    QByteArray data = f.readAll();
    f.close();
    return data;
}

bool PathUtil::writeAll(const QString& utf8Path, const QByteArray& data)
{
    // 确保父目录存在
    const QFileInfo info(utf8Path);
    QDir().mkpath(info.absolutePath());

    QFile f(utf8Path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    const qint64 n = f.write(data);
    f.close();
    return n == data.size();
}

std::wstring PathUtil::toWide(const QString& utf8Path)
{
    return utf8Path.toStdWString();
}

bool PathUtil::exists(const QString& utf8Path)
{
    return QFileInfo::exists(utf8Path);
}

bool PathUtil::ensureDir(const QString& utf8Path)
{
    QDir d;
    return d.mkpath(utf8Path);
}

QString PathUtil::join(const QString& base, const QString& sub)
{
    if (base.isEmpty()) return sub;
    if (sub.isEmpty()) return base;
    QString b = base;
    while (b.endsWith('/') || b.endsWith('\\')) b.chop(1);
    QString s = sub;
    while (s.startsWith('/') || s.startsWith('\\')) s.remove(0, 1);
    return b + QStringLiteral("/") + s;
}

} // namespace HighPro
