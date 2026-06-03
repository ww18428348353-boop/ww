#pragma once

#include "core/Project.h"
#include <QObject>
#include <QString>
#include <QJsonObject>

namespace HighPro {

// 全局工程控制器 (单例): 加载 / 切换 / 信号发送.
class ProjectController : public QObject
{
    Q_OBJECT
public:
    static ProjectController& instance();

    const Project& project() const { return m_project; }
    Project&       projectMut()    { return m_project; }

    // 加载源目录 (扫描). 成功 emit projectLoaded.
    bool loadSource(const QString& sourceRoot, QString* errorOut = nullptr);

    // M8: 工程持久化 (.hplut.json)
    //   saveProject: 保存当前项目状态到指定路径; uiState 可包含 zoom/pan/间距/bg 等 UI 偏好.
    //   loadProject: 从文件读: 自动 loadSource 源目录 + 套用 schemes/肤色/方案选中等.
    bool saveProject(const QString& path, const QJsonObject& uiState = {}, QString* errorOut = nullptr);
    bool loadProject(const QString& path, QJsonObject* outUiState = nullptr, QString* errorOut = nullptr);

    QString currentProjectPath() const { return m_currentProjectPath; }

    // dirty 标志: 有未保存改动 (LUT / Effects / 方案增删改名 / 肤色 / 可见性 等会落盘的内容).
    // 切动作 / 帧 / 当前选中方案/层 不算 dirty (属于预览状态, 不弹未保存确认).
    bool isDirty() const { return m_dirty; }
    void setDirty(bool d = true) { m_dirty = d; }

    void setCurrentAction(const QString& a);
    void setCurrentDirection(int d);
    void setCurrentFrame(int f);
    void advanceFrame();

    void setLayerVisible(const QString& layerKey, bool visible);
    void setCurrentAddon(const QString& layerKey);

    // M7: 肤色层标记 (该层在所有方案下都不应用变色)
    void setLayerSkinSafe(const QString& layerKey, bool skinSafe);

    // M3: 切换某层使用的 LUT 文件 (空字符串 = 不变色 / passthrough)
    void setLayerLut(const QString& layerKey, const QString& lutPath);
    int  applyLutToAllLayers(const QString& filename);
    void clearAllLayerLut();

    // M4: 7 效果栈编辑
    void   setCurrentLayerKey(const QString& key);
    QString currentLayerKey() const { return m_project.currentLayerKey; }

    // 编辑器修改 effects 后调此一致刷新预览
    void notifyEffectsChanged();
    void resetCurrentLayerEffects();
    void resetAllLayerEffects();             // 当前方案下所有层重置
    void copyCurrentLayerEffectsToAll();

    // 跨方案重置:
    //   includeBaked=false → 仅可编辑方案 (isBaked=false), 重置该方案所有层
    //   includeBaked=true  → 所有非本体方案; 已烘焙方案降级为可编辑后重置
    void resetAllSchemesEffects(bool includeBaked);

    // M7: 智能随机
    //   randomizeCurrentLayer: 仅当前编辑层 (currentLayerKey) 应用一组随机参数.
    //   randomizeAllLayers:    当前方案下所有可见 + 非肤色保护层各自独立随机.
    //   sameSeedAllLayers=true: 全层用同一种子, 同一方案各层效果互相协调 (推荐).
    void randomizeCurrentLayer();
    void randomizeAllLayers(bool sameSeedAllLayers = true);

    // M7 跨方案随机: 对所有非本体方案的所有层做独立随机.
    //   includeBaked=false → 仅可编辑方案 (isBaked=false);
    //   includeBaked=true  → 已烘焙方案先转为可编辑 (丢失原 add_lut PNG 引用), 再随机.
    void randomizeAllSchemes(bool includeBaked);

    // M5: 方案管理
    int  schemeCount() const { return m_project.schemes.size(); }
    int  currentSchemeIndex() const { return m_project.currentSchemeIndex; }
    void setCurrentSchemeIndex(int i);

    // 新增空方案. 返回新方案 index (永远 >=1).
    int  addScheme(const QString& name = QString());
    // 删除指定方案. 不允许删本体 (idx==0). 返回是否成功.
    bool removeScheme(int idx);
    void renameScheme(int idx, const QString& name);

    // 方案锁: 锁住的方案不参与"随机当前层 / 所有层 / 全部". 本体 / 已烘焙不锁.
    bool isSchemeLocked(int idx) const;
    void setSchemeLocked(int idx, bool locked);

signals:
    void projectLoaded();
    void actionChanged();
    void directionChanged();
    void frameChanged();
    void visibilityChanged();
    void lutChanged();
    void effectsChanged();
    void currentLayerKeyChanged();
    void schemesChanged();           // 方案集合本身变 (增/删/改名)
    void currentSchemeChanged();     // 当前编辑方案切换

private:
    ProjectController() = default;
    Project m_project;
    QString m_currentProjectPath;     // M8: 最近 saveProject/loadProject 的路径
    bool    m_dirty{ false };         // 有未保存改动
};

} // namespace HighPro
