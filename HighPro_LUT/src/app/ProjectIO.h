#pragma once

#include <QString>
#include <QJsonObject>

namespace HighPro {

struct Project;
struct EffectStack;
struct Scheme;

// 工程持久化: .hplut.json 文件读写.
// 不保存 layers (源目录扫描重建); 保存所有方案 / 肤色层 / 显示状态 / UI 偏好.
class ProjectIO
{
public:
    // 保存到指定路径. 返回是否成功; 失败时 errorOut 写错误信息.
    static bool saveToFile(const Project& proj, const QString& path,
                           const QJsonObject& uiState = {},
                           QString* errorOut = nullptr);

    // 从文件加载: 解析 schemes/UI 等到 outProj 与 outUiState (outProj.layers 不动, 由调用方先 ResourceScanner 填好).
    // 失败返回 false, errorOut 写原因.
    static bool loadFromFile(const QString& path, Project& outProj,
                             QJsonObject* outUiState = nullptr,
                             QString* errorOut = nullptr);

    // === 单元 JSON 转换 (供调试 / 单元测试) ===
    static QJsonObject effectStackToJson(const EffectStack& s);
    static EffectStack effectStackFromJson(const QJsonObject& obj);

    static QJsonObject schemeToJson(const Scheme& s);
    static Scheme       schemeFromJson(const QJsonObject& obj);

    static QJsonObject projectToJson(const Project& p);
    static void        projectFromJson(const QJsonObject& obj, Project& out);
};

} // namespace HighPro
