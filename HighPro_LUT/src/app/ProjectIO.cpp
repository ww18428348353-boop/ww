#include "ProjectIO.h"
#include "core/Project.h"
#include "core/PathUtil.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QFileInfo>

namespace HighPro {

namespace {

QJsonArray boolArray(const std::array<bool, EffectStack::kCount>& a) {
    QJsonArray j;
    for (bool v : a) j.append(v);
    return j;
}

void readBoolArray(const QJsonValue& v, std::array<bool, EffectStack::kCount>& out) {
    if (!v.isArray()) return;
    QJsonArray a = v.toArray();
    for (size_t i = 0; i < EffectStack::kCount && i < (size_t)a.size(); ++i) {
        out[i] = a[i].toBool(false);
    }
}

QJsonObject ptsToJson(const CurveParams::Pts& pts) {
    QJsonObject o;
    QJsonArray xs, ys;
    for (const auto& p : pts) { xs.append(p.first); ys.append(p.second); }
    o["x"] = xs; o["y"] = ys;
    return o;
}
CurveParams::Pts ptsFromJson(const QJsonObject& o) {
    CurveParams::Pts pts;
    QJsonArray xs = o["x"].toArray();
    QJsonArray ys = o["y"].toArray();
    int n = std::min(xs.size(), ys.size());
    for (int i = 0; i < n; ++i) pts.append({ xs[i].toInt(), ys[i].toInt() });
    if (pts.size() < 2) pts = { {0,0}, {255,255} };
    return pts;
}

} // namespace

QJsonObject ProjectIO::effectStackToJson(const EffectStack& s)
{
    QJsonObject o;
    o["enabled"] = boolArray(s.enabled);
    o["shadowProtectThreshold"] = s.shadowProtectThreshold;

    {
        QJsonObject hsl;
        hsl["hue"] = s.hsl.hue;
        hsl["saturation"] = s.hsl.saturation;
        hsl["lightness"] = s.hsl.lightness;
        o["hsl"] = hsl;
    }
    {
        QJsonObject brt;
        brt["brightness"] = s.brtCtr.brightness;
        brt["contrast"] = s.brtCtr.contrast;
        o["brtCtr"] = brt;
    }
    {
        QJsonObject c;
        c["master"] = ptsToJson(s.curves.master);
        c["r"] = ptsToJson(s.curves.r);
        c["g"] = ptsToJson(s.curves.g);
        c["b"] = ptsToJson(s.curves.b);
        o["curves"] = c;
    }
    {
        QJsonObject m;
        m["rr"] = s.chMix.rr; m["rg"] = s.chMix.rg; m["rb"] = s.chMix.rb; m["rConst"] = s.chMix.r_const;
        m["gr"] = s.chMix.gr; m["gg"] = s.chMix.gg; m["gb"] = s.chMix.gb; m["gConst"] = s.chMix.g_const;
        m["br"] = s.chMix.br; m["bg"] = s.chMix.bg; m["bb"] = s.chMix.bb; m["bConst"] = s.chMix.b_const;
        m["mono"] = s.chMix.monochrome;
        o["chMix"] = m;
    }
    {
        QJsonObject b;
        b["sR"] = s.colorBal.sR; b["sG"] = s.colorBal.sG; b["sB"] = s.colorBal.sB;
        b["mR"] = s.colorBal.mR; b["mG"] = s.colorBal.mG; b["mB"] = s.colorBal.mB;
        b["hR"] = s.colorBal.hR; b["hG"] = s.colorBal.hG; b["hB"] = s.colorBal.hB;
        b["preserveLuma"] = s.colorBal.preserveLuma;
        o["colorBal"] = b;
    }
    {
        QJsonObject p;
        p["preset"] = s.photoFilter.preset;
        p["filterR"] = s.photoFilter.filterR;
        p["filterG"] = s.photoFilter.filterG;
        p["filterB"] = s.photoFilter.filterB;
        p["density"] = s.photoFilter.density;
        p["preserveLuma"] = s.photoFilter.preserveLuma;
        o["photoFilter"] = p;
    }
    {
        QJsonObject v;
        v["vibrance"] = s.vibrance.vibrance;
        v["saturation"] = s.vibrance.saturation;
        o["vibrance"] = v;
    }
    return o;
}

EffectStack ProjectIO::effectStackFromJson(const QJsonObject& o)
{
    EffectStack s;
    readBoolArray(o["enabled"], s.enabled);
    s.shadowProtectThreshold = o["shadowProtectThreshold"].toInt(8);

    {
        const QJsonObject h = o["hsl"].toObject();
        s.hsl.hue = h["hue"].toInt();
        s.hsl.saturation = h["saturation"].toInt();
        s.hsl.lightness = h["lightness"].toInt();
    }
    {
        const QJsonObject b = o["brtCtr"].toObject();
        s.brtCtr.brightness = b["brightness"].toInt();
        s.brtCtr.contrast = b["contrast"].toInt();
    }
    {
        const QJsonObject c = o["curves"].toObject();
        if (c.contains("master")) s.curves.master = ptsFromJson(c["master"].toObject());
        if (c.contains("r"))      s.curves.r      = ptsFromJson(c["r"].toObject());
        if (c.contains("g"))      s.curves.g      = ptsFromJson(c["g"].toObject());
        if (c.contains("b"))      s.curves.b      = ptsFromJson(c["b"].toObject());
    }
    {
        const QJsonObject m = o["chMix"].toObject();
        s.chMix.rr = m["rr"].toInt(100); s.chMix.rg = m["rg"].toInt();    s.chMix.rb = m["rb"].toInt();    s.chMix.r_const = m["rConst"].toInt();
        s.chMix.gr = m["gr"].toInt();    s.chMix.gg = m["gg"].toInt(100); s.chMix.gb = m["gb"].toInt();    s.chMix.g_const = m["gConst"].toInt();
        s.chMix.br = m["br"].toInt();    s.chMix.bg = m["bg"].toInt();    s.chMix.bb = m["bb"].toInt(100); s.chMix.b_const = m["bConst"].toInt();
        s.chMix.monochrome = m["mono"].toBool();
    }
    {
        const QJsonObject b = o["colorBal"].toObject();
        s.colorBal.sR = b["sR"].toInt(); s.colorBal.sG = b["sG"].toInt(); s.colorBal.sB = b["sB"].toInt();
        s.colorBal.mR = b["mR"].toInt(); s.colorBal.mG = b["mG"].toInt(); s.colorBal.mB = b["mB"].toInt();
        s.colorBal.hR = b["hR"].toInt(); s.colorBal.hG = b["hG"].toInt(); s.colorBal.hB = b["hB"].toInt();
        s.colorBal.preserveLuma = b["preserveLuma"].toBool();
    }
    {
        const QJsonObject p = o["photoFilter"].toObject();
        s.photoFilter.preset = p["preset"].toInt(-1);
        s.photoFilter.filterR = p["filterR"].toInt();
        s.photoFilter.filterG = p["filterG"].toInt();
        s.photoFilter.filterB = p["filterB"].toInt();
        s.photoFilter.density = p["density"].toInt();
        s.photoFilter.preserveLuma = p["preserveLuma"].toBool();
    }
    {
        const QJsonObject v = o["vibrance"].toObject();
        s.vibrance.vibrance = v["vibrance"].toInt();
        s.vibrance.saturation = v["saturation"].toInt();
    }
    return s;
}

QJsonObject ProjectIO::schemeToJson(const Scheme& s)
{
    QJsonObject o;
    o["name"] = s.name;
    o["isBuiltin"] = s.isBuiltin;
    o["isBaked"]   = s.isBaked;
    o["locked"]    = s.locked;

    if (s.isBaked) {
        QJsonObject m;
        for (auto it = s.layerLutPath.constBegin(); it != s.layerLutPath.constEnd(); ++it) {
            m[it.key()] = it.value();
        }
        o["layerLutPath"] = m;
    } else {
        QJsonObject m;
        for (auto it = s.layerEffects.constBegin(); it != s.layerEffects.constEnd(); ++it) {
            m[it.key()] = effectStackToJson(it.value());
        }
        o["layerEffects"] = m;
    }
    return o;
}

Scheme ProjectIO::schemeFromJson(const QJsonObject& o)
{
    Scheme s;
    s.name = o["name"].toString();
    s.isBuiltin = o["isBuiltin"].toBool();
    s.isBaked   = o["isBaked"].toBool();
    s.locked    = o["locked"].toBool();

    if (s.isBaked) {
        QJsonObject m = o["layerLutPath"].toObject();
        for (auto it = m.constBegin(); it != m.constEnd(); ++it) {
            s.layerLutPath.insert(it.key(), it.value().toString());
        }
    } else {
        QJsonObject m = o["layerEffects"].toObject();
        for (auto it = m.constBegin(); it != m.constEnd(); ++it) {
            s.layerEffects.insert(it.key(), effectStackFromJson(it.value().toObject()));
        }
    }
    return s;
}

QJsonObject ProjectIO::projectToJson(const Project& p)
{
    QJsonObject o;
    o["version"] = 1;
    o["sourceRoot"] = p.sourceRoot;
    o["outputRoot"] = p.outputRoot;
    o["currentAction"] = p.currentAction;
    o["currentDirection"] = p.currentDirection;
    o["currentFrame"] = p.currentFrame;
    o["currentSchemeIndex"] = p.currentSchemeIndex;
    o["currentLayerKey"] = p.currentLayerKey;
    o["currentAddonKey"] = p.currentAddonKey;

    {
        QJsonArray a;
        for (const QString& k : p.hiddenLayerKeys) a.append(k);
        o["hiddenLayerKeys"] = a;
    }
    {
        QJsonArray a;
        for (const QString& k : p.skinSafeLayerKeys) a.append(k);
        o["skinSafeLayerKeys"] = a;
    }
    {
        QJsonObject m;
        for (auto it = p.layerLutPath.constBegin(); it != p.layerLutPath.constEnd(); ++it) {
            m[it.key()] = it.value();
        }
        o["layerLutPath"] = m;
    }
    {
        QJsonArray arr;
        for (const auto& sc : p.schemes) arr.append(schemeToJson(sc));
        o["schemes"] = arr;
    }
    return o;
}

void ProjectIO::projectFromJson(const QJsonObject& obj, Project& out)
{
    // 不动 layers (调用方先用 ResourceScanner 填)
    out.sourceRoot = obj["sourceRoot"].toString();
    out.outputRoot = obj["outputRoot"].toString();
    out.currentAction = obj["currentAction"].toString();
    out.currentDirection = obj["currentDirection"].toInt();
    out.currentFrame = obj["currentFrame"].toInt();
    out.currentSchemeIndex = obj["currentSchemeIndex"].toInt();
    out.currentLayerKey = obj["currentLayerKey"].toString();
    out.currentAddonKey = obj["currentAddonKey"].toString();

    out.hiddenLayerKeys.clear();
    for (const auto& v : obj["hiddenLayerKeys"].toArray()) out.hiddenLayerKeys.insert(v.toString());

    out.skinSafeLayerKeys.clear();
    for (const auto& v : obj["skinSafeLayerKeys"].toArray()) out.skinSafeLayerKeys.insert(v.toString());

    out.layerLutPath.clear();
    QJsonObject lp = obj["layerLutPath"].toObject();
    for (auto it = lp.constBegin(); it != lp.constEnd(); ++it) {
        out.layerLutPath.insert(it.key(), it.value().toString());
    }

    out.schemes.clear();
    for (const auto& v : obj["schemes"].toArray()) {
        out.schemes.push_back(schemeFromJson(v.toObject()));
    }
    if (out.schemes.isEmpty()) {
        // 兜底: 没有任何方案 → 至少加本体
        out.schemes.push_back(Scheme::makeBuiltin());
    }
    if (out.currentSchemeIndex < 0 || out.currentSchemeIndex >= out.schemes.size()) {
        out.currentSchemeIndex = 0;
    }
}

bool ProjectIO::saveToFile(const Project& proj, const QString& path,
                           const QJsonObject& uiState, QString* errorOut)
{
    QJsonObject root;
    root["app"] = "HighPro_LUT";
    root["version"] = 1;
    root["project"] = projectToJson(proj);
    if (!uiState.isEmpty()) root["ui"] = uiState;

    QJsonDocument doc(root);
    QByteArray buf = doc.toJson(QJsonDocument::Indented);

    // 保证父目录存在
    const QFileInfo fi(path);
    PathUtil::ensureDir(fi.absolutePath());

    if (!PathUtil::writeAll(path, buf)) {
        if (errorOut) *errorOut = QString("写文件失败: %1").arg(path);
        return false;
    }
    return true;
}

bool ProjectIO::loadFromFile(const QString& path, Project& outProj,
                             QJsonObject* outUiState, QString* errorOut)
{
    QByteArray buf = PathUtil::readAll(path);
    if (buf.isEmpty()) {
        if (errorOut) *errorOut = QString("读文件失败: %1").arg(path);
        return false;
    }
    QJsonParseError pe{};
    QJsonDocument doc = QJsonDocument::fromJson(buf, &pe);
    if (doc.isNull() || !doc.isObject()) {
        if (errorOut) *errorOut = QString("JSON 解析失败: %1").arg(pe.errorString());
        return false;
    }
    QJsonObject root = doc.object();
    if (!root.contains("project")) {
        if (errorOut) *errorOut = "缺少 project 字段";
        return false;
    }
    projectFromJson(root["project"].toObject(), outProj);
    if (outUiState) *outUiState = root["ui"].toObject();
    return true;
}

} // namespace HighPro
