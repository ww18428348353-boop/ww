#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QColor>
#include <QSize>
#include <QPoint>

namespace HighPro {

// 全局应用配置. 用 QSettings 持久化于注册表 (HKCU\SOFTWARE\HighPro\HighPro_LUT).
class AppSettings : public QObject
{
    Q_OBJECT

public:
    static AppSettings& instance();

    void load();
    void save();

    // === 窗口几何 ===
    QSize  windowSize() const           { return m_windowSize; }
    void   setWindowSize(QSize s)       { m_windowSize = s; }

    QPoint windowPos() const            { return m_windowPos; }
    void   setWindowPos(QPoint p)       { m_windowPos = p; }

    bool   windowMaximized() const      { return m_windowMaximized; }
    void   setWindowMaximized(bool v)   { m_windowMaximized = v; }

    QByteArray dockState() const        { return m_dockState; }
    void   setDockState(const QByteArray& s) { m_dockState = s; }

    // === 路径 ===
    QString lastSourceDir() const       { return m_lastSourceDir; }
    void    setLastSourceDir(const QString& d) { m_lastSourceDir = d; }

    QString lastOutputDir() const       { return m_lastOutputDir; }
    void    setLastOutputDir(const QString& d) { m_lastOutputDir = d; }

    // M8: 最近打开/保存的工程文件 .hplut.json 路径
    QString lastProjectPath() const     { return m_lastProjectPath; }
    void    setLastProjectPath(const QString& p) { m_lastProjectPath = p; }

    bool    autoLoadLastProject() const     { return m_autoLoadLastProject; }
    void    setAutoLoadLastProject(bool v)  { m_autoLoadLastProject = v; }

    // 最近工程列表 (上限 10, 头部最新). 读: recentProjects(); 写: pushRecentProject(p).
    static constexpr int kMaxRecentProjects = 10;
    QStringList recentProjects() const  { return m_recentProjects; }
    void        pushRecentProject(const QString& p);
    void        clearRecentProjects()   { m_recentProjects.clear(); }

    // === 预览 ===
    QColor  bgColor() const             { return m_bgColor; }
    void    setBgColor(const QColor& c) { m_bgColor = c; }

    QString bgImage() const             { return m_bgImage; }
    void    setBgImage(const QString& p){ m_bgImage = p; }

    int     fps() const                 { return m_fps; }
    void    setFps(int v)               { m_fps = v; }

    // === 主题 ===
    // true = 暗色 Fusion (默认), false = 浅色 Fusion. 视图菜单可切换并持久化.
    bool    themeDark() const           { return m_themeDark; }
    void    setThemeDark(bool v)        { m_themeDark = v; }

private:
    AppSettings() = default;
    Q_DISABLE_COPY_MOVE(AppSettings)

    QSize    m_windowSize{ 1440, 900 };
    QPoint   m_windowPos{ 200, 100 };
    bool     m_windowMaximized{ false };
    QByteArray m_dockState;

    QString  m_lastSourceDir;
    QString  m_lastOutputDir;
    QString  m_lastProjectPath;             // M8
    bool     m_autoLoadLastProject{ true };  // M8: 启动时自动恢复
    QStringList m_recentProjects;            // 最近工程, 头新尾旧, 上限 10

    QColor   m_bgColor{ 60, 60, 60 };       // R60G60B60
    QString  m_bgImage;
    int      m_fps{ 10 };                   // 默认 10 fps

    bool     m_themeDark{ true };           // 默认暗色主题
};

} // namespace HighPro
