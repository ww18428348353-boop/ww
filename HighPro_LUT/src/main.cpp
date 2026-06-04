#include "ui/MainWindow.h"
#include "app/AppSettings.h"
#include "app/AppTheme.h"
#include "app/ProjectController.h"

#include <QApplication>
#include <QStyleFactory>
#include <QFontDatabase>
#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QIcon>

int main(int argc, char* argv[])
{
    // 高 DPI 支持 (Qt 6 默认开启, 这里设缩放策略)
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);
    QApplication::setApplicationName("HighPro_LUT");
    QApplication::setOrganizationName("HighPro");
    QApplication::setApplicationVersion("1.0.0");

    // 应用图标 (任务栏 / Alt+Tab / 窗口左上)
    QApplication::setWindowIcon(QIcon(":/icons/app.ico"));

    HighPro::AppSettings::instance().load();

    // 主题: 默认暗色, 视图菜单可切. 必须在 MainWindow 创建前 apply,
    //       这样所有子 widget 第一次 polish 就走对色板, 避免局部白底闪烁.
    HighPro::AppTheme::apply(
        HighPro::AppSettings::instance().themeDark()
            ? HighPro::AppTheme::Mode::Dark
            : HighPro::AppTheme::Mode::Light);

    HighPro::MainWindow win;
    win.show();

    // M8: 启动后异步尝试自动加载上次工程 (如果存在)
    QTimer::singleShot(0, &app, []{
        auto& s = HighPro::AppSettings::instance();
        if (!s.autoLoadLastProject()) return;
        const QString p = s.lastProjectPath();
        if (p.isEmpty() || !QFileInfo::exists(p)) return;
        QString err;
        if (!HighPro::ProjectController::instance().loadProject(p, nullptr, &err)) {
            qWarning() << "自动加载工程失败:" << err;
        }
    });

    int rc = app.exec();
    HighPro::AppSettings::instance().save();
    return rc;
}
