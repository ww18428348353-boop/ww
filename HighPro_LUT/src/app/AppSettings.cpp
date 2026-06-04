#include "AppSettings.h"
#include <QSettings>

namespace HighPro {

AppSettings& AppSettings::instance()
{
    static AppSettings s;
    return s;
}

void AppSettings::load()
{
    QSettings s;
    s.beginGroup("Window");
    m_windowSize      = s.value("size",      m_windowSize).toSize();
    m_windowPos       = s.value("pos",       m_windowPos).toPoint();
    m_windowMaximized = s.value("maximized", m_windowMaximized).toBool();
    m_dockState       = s.value("dock").toByteArray();
    s.endGroup();

    s.beginGroup("Path");
    m_lastSourceDir   = s.value("lastSource").toString();
    m_lastOutputDir   = s.value("lastOutput").toString();
    m_lastProjectPath = s.value("lastProject").toString();
    m_autoLoadLastProject = s.value("autoLoadLastProject", true).toBool();
    m_recentProjects  = s.value("recentProjects").toStringList();
    // 防御: 启动时丢掉空串/不存在的项, 截断到上限
    m_recentProjects.removeAll(QString());
    while (m_recentProjects.size() > kMaxRecentProjects) m_recentProjects.removeLast();
    s.endGroup();

    s.beginGroup("Preview");
    m_bgColor = s.value("bgColor", m_bgColor).value<QColor>();
    m_bgImage = s.value("bgImage").toString();
    m_fps     = s.value("fps", m_fps).toInt();
    s.endGroup();

    s.beginGroup("Theme");
    m_themeDark = s.value("dark", m_themeDark).toBool();
    s.endGroup();
}

void AppSettings::save()
{
    QSettings s;
    s.beginGroup("Window");
    s.setValue("size",      m_windowSize);
    s.setValue("pos",       m_windowPos);
    s.setValue("maximized", m_windowMaximized);
    s.setValue("dock",      m_dockState);
    s.endGroup();

    s.beginGroup("Path");
    s.setValue("lastSource",  m_lastSourceDir);
    s.setValue("lastOutput",  m_lastOutputDir);
    s.setValue("lastProject", m_lastProjectPath);
    s.setValue("autoLoadLastProject", m_autoLoadLastProject);
    s.setValue("recentProjects", m_recentProjects);
    s.endGroup();

    s.beginGroup("Preview");
    s.setValue("bgColor", m_bgColor);
    s.setValue("bgImage", m_bgImage);
    s.setValue("fps",     m_fps);
    s.endGroup();

    s.beginGroup("Theme");
    s.setValue("dark", m_themeDark);
    s.endGroup();
}

void AppSettings::pushRecentProject(const QString& p)
{
    if (p.isEmpty()) return;
    // 已存在则移到头部 (大小写敏感: Windows 路径, 文件系统大小写不敏感, 但 string 区分)
    m_recentProjects.removeAll(p);
    m_recentProjects.prepend(p);
    while (m_recentProjects.size() > kMaxRecentProjects) m_recentProjects.removeLast();
}

} // namespace HighPro
