#pragma once

#include <QString>
#include <QStringList>
#include <cstdint>

namespace HighPro {

struct Project;
class  LutBaker;

// 单方案 × 全层 LUT 导出.
// 输出布局: <outputRoot>/<层 displayName>/add_lut/01.png
// (一期固定 01.png; M5+ 多方案后改 0N.png)
class PngExporter
{
public:
    struct Result
    {
        int  successCount = 0;
        int  skippedCount = 0;       // 隐藏层 / 肤色层
        QString lastError;
        QStringList writtenPaths;
    };

    // 写一张 LUT 字节 (256×16 RGBA8) 到 png 文件 (中文路径友好).
    static bool writeLutPng(const uint8_t* rgba256x16, const QString& outPath, QString* errorOut = nullptr);

    // 遍历 project 的所有层, 用各层 EffectStack 烘焙后导出.
    // outputRoot 空 → 用 project.outputRoot.
    static Result exportCurrentScheme(const Project& project,
                                      LutBaker& baker,
                                      const QString& outputRoot = QString());

    // 导出所有非本体方案 (idx 1..N), 每个方案一组 <层>/add_lut/0N.png.
    // 已烘焙方案 (isBaked=true): 直接拷源 add_lut PNG (不烘焙).
    // 用户方案 (可编辑):           走 LutBaker 烘焙.
    static Result exportAllSchemes(const Project& project,
                                   LutBaker& baker,
                                   const QString& outputRoot = QString());

    // 仅导出 locked=true 的方案. 本体永远跳过. 没有任何锁定方案时
    // result.lastError = "没有锁定的方案".
    static Result exportLockedSchemes(const Project& project,
                                      LutBaker& baker,
                                      const QString& outputRoot = QString());
};

} // namespace HighPro
