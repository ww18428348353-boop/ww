#include "PngExporter.h"
#include "LutBaker.h"
#include "core/Project.h"
#include "core/LayerData.h"
#include "core/PathUtil.h"

// stb_image_write.h 的 stbi_write_png_to_mem 默认不在 header 头部前向声明,
// 这里手动声明 (实现已在 DebugDumper.cpp STB_IMAGE_WRITE_IMPLEMENTATION 中编译).
extern "C" unsigned char* stbi_write_png_to_mem(
    const unsigned char* pixels, int stride_bytes,
    int x, int y, int n, int* out_len);

// 与 stb 中的 STBIW_FREE 一致 (默认 free)
#include <cstdlib>
#define HP_STBIW_FREE(p) free(p)

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDebug>

namespace HighPro {

bool PngExporter::writeLutPng(const uint8_t* rgba, const QString& outPath, QString* errorOut)
{
    if (!rgba) { if (errorOut) *errorOut = "rgba null"; return false; }

    int outLen = 0;
    unsigned char* png = stbi_write_png_to_mem(
        rgba, LutBaker::kWidth * 4,
        LutBaker::kWidth, LutBaker::kHeight, 4, &outLen);
    if (!png || outLen <= 0) {
        if (errorOut) *errorOut = "encode png 失败";
        return false;
    }

    bool ok = PathUtil::writeAll(outPath, QByteArray((const char*)png, outLen));
    HP_STBIW_FREE(png);
    if (!ok && errorOut) *errorOut = QString("写文件失败: %1").arg(outPath);
    return ok;
}

namespace {

// 单方案导出. schemeIdx 决定文件名 0N.png. result 由调用方累加.
// 返回 false 即遇到致命错误, 调用方应停止 (并把 errMsg 写到 result.lastError 后返回).
bool exportOneScheme(const Project& project, const Scheme& sc, int schemeIdx,
                     const QString& outRoot, LutBaker& baker,
                     PngExporter::Result& r, QString* errMsg)
{
    if (sc.isBuiltin) {
        // idx=0 本体不导出 — 直接全部跳过
        for (const auto& l : project.layers) {
            if (!project.isLayerVisible(l)) { r.skippedCount++; }
        }
        return true;
    }

    const QString fileName = QString("%1.png").arg(schemeIdx, 2, 10, QChar('0'));
    EffectStack identity;

    for (const auto& layer : project.layers) {
        const bool hidden = !project.isLayerVisible(layer);
        // M7: 肤色保护层 / 隐藏层 → 导出默认 identity LUT (确保该层 add_lut 文件存在)
        const bool skinSafe = project.isSkinSafe(layer);
        const bool forceIdentity = skinSafe || hidden;

        QString dir = QDir(outRoot).filePath(layer.displayName + "/add_lut");
        if (!PathUtil::ensureDir(dir)) {
            if (errMsg) *errMsg = QString("创建目录失败: %1").arg(dir);
            return false;
        }
        QString outPath = QDir(dir).filePath(fileName);

        if (sc.isBaked && !forceIdentity) {
            // 已烘焙方案: 直接拷源 add_lut PNG (该层若没对应 LUT 则跳过)
            const QString srcLut = sc.layerLutPath.value(layer.key());
            if (srcLut.isEmpty()) {
                r.skippedCount++;
                continue;
            }
            QByteArray buf = PathUtil::readAll(srcLut);
            if (buf.isEmpty()) {
                if (errMsg) *errMsg = QString("[%1] 读源 LUT 失败: %2")
                                       .arg(layer.displayName, srcLut);
                return false;
            }
            if (!PathUtil::writeAll(outPath, buf)) {
                if (errMsg) *errMsg = QString("[%1] 写文件失败: %2")
                                       .arg(layer.displayName, outPath);
                return false;
            }
        } else {
            // 用户可编辑方案: 走 LutBaker
            // 肤色保护层/隐藏层强制用 identity (不变色), 确保 add_lut 文件存在
            const EffectStack* stk = forceIdentity ? nullptr : project.effectsForIn(sc, layer);
            const EffectStack& use = stk ? *stk : identity;
            QString err;
            if (!baker.bake(use, &err)) {
                if (errMsg) *errMsg = QString("[%1] 烘焙失败: %2").arg(layer.displayName, err);
                return false;
            }
            if (!PngExporter::writeLutPng(baker.bytes().data(), outPath, &err)) {
                if (errMsg) *errMsg = QString("[%1] 写文件失败: %2").arg(layer.displayName, err);
                return false;
            }
        }

        r.writtenPaths << outPath;
        r.successCount++;
    }
    return true;
}

} // namespace

PngExporter::Result PngExporter::exportCurrentScheme(const Project& project,
                                                    LutBaker& baker,
                                                    const QString& outputRootIn)
{
    Result r;
    if (!baker.isReady()) { r.lastError = "LutBaker 未就绪"; return r; }
    QString outRoot = outputRootIn.isEmpty() ? project.outputRoot : outputRootIn;
    if (outRoot.isEmpty()) { r.lastError = "未设置输出目录"; return r; }

    const int idx = project.currentSchemeIndex;
    if (idx < 0 || idx >= project.schemes.size()) {
        r.lastError = "未选中方案";
        return r;
    }
    const Scheme& sc = project.schemes[idx];
    if (sc.isBuiltin) {
        r.lastError = "本体方案不导出 (idx 0); 请切到其他方案";
        return r;
    }

    QString err;
    if (!exportOneScheme(project, sc, idx, outRoot, baker, r, &err)) {
        r.lastError = err;
    }
    return r;
}

PngExporter::Result PngExporter::exportAllSchemes(const Project& project,
                                                  LutBaker& baker,
                                                  const QString& outputRootIn)
{
    Result r;
    if (!baker.isReady()) { r.lastError = "LutBaker 未就绪"; return r; }
    QString outRoot = outputRootIn.isEmpty() ? project.outputRoot : outputRootIn;
    if (outRoot.isEmpty()) { r.lastError = "未设置输出目录"; return r; }

    if (project.schemes.size() <= 1) {
        r.lastError = "没有可导出的方案 (只有本体)";
        return r;
    }

    // 跳过 idx=0 本体, 导 1..N
    for (int i = 1; i < project.schemes.size(); ++i) {
        QString err;
        if (!exportOneScheme(project, project.schemes[i], i, outRoot, baker, r, &err)) {
            r.lastError = QString("[方案 #%1] %2").arg(i).arg(err);
            return r;
        }
    }
    return r;
}

PngExporter::Result PngExporter::exportLockedSchemes(const Project& project,
                                                    LutBaker& baker,
                                                    const QString& outputRootIn)
{
    Result r;
    if (!baker.isReady()) { r.lastError = "LutBaker 未就绪"; return r; }
    QString outRoot = outputRootIn.isEmpty() ? project.outputRoot : outputRootIn;
    if (outRoot.isEmpty()) { r.lastError = "未设置输出目录"; return r; }

    int locked = 0;
    for (int i = 1; i < project.schemes.size(); ++i) {
        if (project.schemes[i].locked) ++locked;
    }
    if (locked == 0) {
        r.lastError = "没有锁定的方案 (右键方案 → 🔒 锁定后再试)";
        return r;
    }

    for (int i = 1; i < project.schemes.size(); ++i) {
        if (!project.schemes[i].locked) continue;
        QString err;
        if (!exportOneScheme(project, project.schemes[i], i, outRoot, baker, r, &err)) {
            r.lastError = QString("[方案 #%1] %2").arg(i).arg(err);
            return r;
        }
    }
    return r;
}

} // namespace HighPro
