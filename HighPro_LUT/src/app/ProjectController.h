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
    // 仅触发画布刷新, 不标 dirty / 不改 schemesChanged. 用于细化弹窗实时预览这类
    // "用户尚未提交" 的中间态. 调用方负责后续保存或回滚.
    void emitPreviewRefresh();
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

    // === P0 智能随机 (Palette + LayerSlot 驱动, 与上面 4 个并存) ===
    //   smartRandomizeCurrentLayer: 当前方案 - 当前层. 复用 (或自动生成) 方案 palette,
    //                               按 slotFor() 取参数策略. 不动其他层, 不动 palette.
    //   smartRandomizeAllLayers:    当前方案换一套完整 palette, 全层按 slot 重算.
    //   smartRandomizeAllSchemes:   每方案按 (idx-1) % 27 取 kSchemeStyles 风格生成 palette,
    //                               全层按 slot 重算. includeBaked=true 会把已烘焙降级.
    //   mixRandomizeAllSchemes:     每方案独立 50% 概率走 smart 或 legacy 随机,
    //                               混合分布. includeBaked 同上.
    void smartRandomizeCurrentLayer();
    void smartRandomizeAllLayers();
    void smartRandomizeAllSchemes(bool includeBaked);
    void mixRandomizeAllSchemes(bool includeBaked);

    // P0: 用户右键设置层语义.
    //   slot == Unknown → 清除手动指定 (slotFor() 走启发式).
    //   slot == Skin    → 同步加入 skinSafeLayerKeys.
    //   slot != Skin    → 同步移出 skinSafeLayerKeys.
    void setLayerSlot(const QString& layerKey, LayerSlot slot);

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

    // === 方案画廊新增: 细化方案 / 配色方案转移 ===
    //
    // 打开细化弹窗前确保目标方案可编辑.
    //   - 本体方案 (isBuiltin)        → 返回 false, errorOut 写入原因
    //   - 已烘焙方案 (isBaked)        → 若 allowConvertBaked=true 则降级为可编辑 (清空 layerLutPath),
    //                                  否则返回 false
    //   - 可编辑方案                  → 直接返回 true (不动)
    // 降级时会自动为缺失的层补 EffectStack{} 占位; 成功降级后 schemesChanged 信号发出.
    bool ensureSchemeEditable(int schemeIdx, bool allowConvertBaked, QString* errorOut = nullptr);

    // 保存细化弹窗的修改: 把 refinedEffects 写回方案对应层的 EffectStack.
    //   只允许写已存在于 project.layers 的 layerKey, 未知 key 会被忽略.
    //   写完发 effectsChanged / schemesChanged, 标 dirty.
    bool applyRefinedLayerEffects(int schemeIdx,
                                  const QHash<QString, EffectStack>& refinedEffects,
                                  QString* errorOut = nullptr);

    // 把源方案某分组的配色数据复制到目标方案对应层.
    //
    // groupKey 取值:
    //   "body"        → body 层
    //   "num:00".."num:NN" → 指定 numberedIdx 的数字层
    //   "addon"       → 所有 addon 子层
    //
    // 规则:
    //   - 源 / 目标都是可编辑 (isBaked=false): 复制 layerEffects
    //   - 源可编辑, 目标已烘焙: 目标降级为可编辑后复制
    //   - 源已烘焙, 目标已烘焙: 复制 layerLutPath
    //   - 源已烘焙, 目标可编辑: 不支持, 返回 false
    //   - 任一方是本体: 返回 false
    //   - 源 == 目标: 直接返回 true (无操作)
    bool transferSchemeColorGroup(int sourceSchemeIdx,
                                  int targetSchemeIdx,
                                  const QString& groupKey,
                                  QString* errorOut = nullptr);

    // === Undo / Redo 栈 (Ctrl+Z / Ctrl+Y) ===
    //
    // 简易快照式 undo: 适用于"批量级"操作 (添加/删除/复制/重命名/锁定/随机/重置/细化保存/转移).
    // EffectPanel 单滑块拖动不入栈, 但拖动结束 (鼠标释放 / spin 提交) 时各压一次, 见 EffectPanel.
    //
    // 栈深度默认 50; 超过 FIFO 抛弃最旧.
    // pushUndoSnapshot 自动清空 redo 栈 (任何新操作都使重做链失效).
    void pushUndoSnapshot(const QString& label);   // 操作前调; 失败也安全
    bool canUndo() const { return !m_undoStack.isEmpty(); }
    bool canRedo() const { return !m_redoStack.isEmpty(); }
    QString topUndoLabel() const { return m_undoStack.isEmpty() ? QString() : m_undoStack.last().label; }
    QString topRedoLabel() const { return m_redoStack.isEmpty() ? QString() : m_redoStack.last().label; }
    void undo();                                    // 弹 undo + 把当前态进 redo + 还原
    void redo();                                    // 弹 redo + 把当前态进 undo + 还原
    void clearUndo() { m_undoStack.clear(); m_redoStack.clear(); }

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

    // P0: 确保方案 palette 已生成 (按方案 idx 选风格), 返回引用.
    // 仅供 smart* 方法内部使用. 不会发信号 / 标 dirty (由调用方负责).
    SchemePalette& ensurePaletteForScheme(int schemeIdx);

    // Undo 栈条目: 快照足以还原所有方案级状态.
    struct UndoSnapshot {
        QString          label;
        QVector<Scheme>  schemes;
        int              currentSchemeIndex;
    };
    QVector<UndoSnapshot> m_undoStack;
    QVector<UndoSnapshot> m_redoStack;
    static constexpr int  kUndoLimit = 50;

    // 内部: 把当前 project 状态打包成快照 (供 undo/redo 互转用)
    UndoSnapshot captureSnapshot(const QString& label) const;
    // 内部: 把快照写回 project (不发信号), 由调用方发
    void restoreSnapshot(const UndoSnapshot& s);

    Project m_project;
    QString m_currentProjectPath;     // M8: 最近 saveProject/loadProject 的路径
    bool    m_dirty{ false };         // 有未保存改动
};

} // namespace HighPro
