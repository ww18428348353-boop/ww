#include "ui/MainWindow.h"
#include "app/AppSettings.h"
#include "app/ProjectController.h"

#include <QApplication>
#include <QStyleFactory>
#include <QFontDatabase>
#include <QDir>
#include <QFileInfo>
#include <QTimer>

int main(int argc, char* argv[])
{
    // 高 DPI 支持 (Qt 6 默认开启, 这里设缩放策略)
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);
    QApplication::setApplicationName("HighPro_LUT");
    QApplication::setOrganizationName("HighPro");
    QApplication::setApplicationVersion("1.0.0");

    // Fusion 风格 + 暗色调更适合调色工具
    QApplication::setStyle(QStyleFactory::create("Fusion"));

    HighPro::AppSettings::instance().load();

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
