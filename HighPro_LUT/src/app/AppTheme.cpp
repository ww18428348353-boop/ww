#include "AppTheme.h"

#include <QApplication>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>
#include <QColor>

namespace HighPro {

namespace {
AppTheme::Mode g_mode = AppTheme::Mode::Dark;

// 暗色 QSS: 覆盖 Fusion 留下的小空隙 (菜单/状态栏/dock 标题/工具提示等).
//   注意: EffectPanel / SchemePanel / LayerTreePanel 已有局部 setStyleSheet,
//         这里只补全局基础色, 不重复定义那些控件.
constexpr const char* kDarkQss = R"(
    QToolTip {
        color: #e6e6e6;
        background-color: #2b2b2b;
        border: 1px solid #555;
    }
    QMenuBar {
        background: #2b2b2b;
        color: #ddd;
    }
    QMenuBar::item:selected {
        background: #3b6ea8;
        color: #fff;
    }
    QMenu {
        background: #2b2b2b;
        color: #ddd;
        border: 1px solid #444;
    }
    QMenu::item:selected {
        background: #3b6ea8;
        color: #fff;
    }
    QMenu::separator {
        height: 1px;
        background: #444;
        margin: 4px 8px;
    }
    QStatusBar {
        background: #232323;
        color: #bbb;
    }
    QStatusBar::item {
        border: none;
    }
    QMainWindow::separator {
        background: #1f1f1f;
        width: 4px;
        height: 4px;
    }
    QDockWidget {
        color: #ddd;
        titlebar-close-icon: none;
    }
    QDockWidget::title {
        background: #2b2b2b;
        padding: 4px 8px;
        border-bottom: 1px solid #1f1f1f;
    }
    QHeaderView::section {
        background: #2b2b2b;
        color: #ddd;
        padding: 4px;
        border: 1px solid #1f1f1f;
    }
    QTreeWidget, QListWidget, QTableWidget {
        background: #262626;
        color: #ddd;
        alternate-background-color: #2c2c2c;
        selection-background-color: #3b6ea8;
        selection-color: #fff;
    }
    QScrollBar:vertical {
        background: #262626;
        width: 12px;
    }
    QScrollBar::handle:vertical {
        background: #4a4a4a;
        min-height: 24px;
        border-radius: 3px;
    }
    QScrollBar::handle:vertical:hover {
        background: #5a5a5a;
    }
    QScrollBar:horizontal {
        background: #262626;
        height: 12px;
    }
    QScrollBar::handle:horizontal {
        background: #4a4a4a;
        min-width: 24px;
        border-radius: 3px;
    }
    QScrollBar::handle:horizontal:hover {
        background: #5a5a5a;
    }
    QScrollBar::add-line, QScrollBar::sub-line { background: none; height: 0; width: 0; }
    QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QPlainTextEdit, QTextEdit {
        background: #1f1f1f;
        color: #e6e6e6;
        border: 1px solid #3a3a3a;
        border-radius: 2px;
        padding: 2px 4px;
        selection-background-color: #3b6ea8;
        selection-color: #fff;
    }
    QComboBox QAbstractItemView {
        background: #2b2b2b;
        color: #ddd;
        selection-background-color: #3b6ea8;
        selection-color: #fff;
    }
    QPushButton {
        background: #3a3a3a;
        color: #e6e6e6;
        border: 1px solid #555;
        border-radius: 2px;
        padding: 4px 10px;
    }
    QPushButton:hover {
        background: #4a4a4a;
    }
    QPushButton:pressed {
        background: #2f2f2f;
    }
    QPushButton:disabled {
        background: #2a2a2a;
        color: #777;
        border-color: #3a3a3a;
    }
    QGroupBox {
        color: #ddd;
        border: 1px solid #3a3a3a;
        border-radius: 3px;
        margin-top: 8px;
    }
    QGroupBox::title {
        subcontrol-origin: margin;
        left: 8px;
        padding: 0 4px;
    }
    QCheckBox, QRadioButton, QLabel {
        color: #ddd;
        background: transparent;
    }
    QCheckBox::indicator {
        width: 16px;
        height: 16px;
        border: 2px solid #4a7fba;
        border-radius: 2px;
        background: #1e2a3a;
    }
    QCheckBox::indicator:unchecked {
        image: none;
    }
    QCheckBox::indicator:checked {
        background: #2a4060;
        border: 2px solid #5a9fd4;
        image: url(:/icons/check_orange.svg);
    }
    QCheckBox::indicator:hover {
        border-color: #7ab8e8;
    }
    QSpinBox, QDoubleSpinBox {
        padding-right: 18px;
    }
    QSpinBox::up-button, QDoubleSpinBox::up-button {
        subcontrol-origin: border;
        subcontrol-position: top right;
        width: 17px;
        border-left: 1px solid #555;
        border-bottom: 1px solid #444;
        background: qlineargradient(y1:0, y2:1, stop:0 #6a6a6a, stop:1 #4a4a4a);
    }
    QSpinBox::up-button:hover, QDoubleSpinBox::up-button:hover {
        background: qlineargradient(y1:0, y2:1, stop:0 #7a7a7a, stop:1 #5a5a5a);
    }
    QSpinBox::up-arrow, QDoubleSpinBox::up-arrow {
        image: url(:/icons/arrow_up.svg);
        width: 10px;
        height: 8px;
    }
    QSpinBox::down-button, QDoubleSpinBox::down-button {
        subcontrol-origin: border;
        subcontrol-position: bottom right;
        width: 17px;
        border-left: 1px solid #555;
        border-top: 1px solid #444;
        background: qlineargradient(y1:0, y2:1, stop:0 #4a4a4a, stop:1 #333);
    }
    QSpinBox::down-button:hover, QDoubleSpinBox::down-button:hover {
        background: qlineargradient(y1:0, y2:1, stop:0 #5a5a5a, stop:1 #444);
    }
    QSpinBox::down-arrow, QDoubleSpinBox::down-arrow {
        image: url(:/icons/arrow_down.svg);
        width: 10px;
        height: 8px;
    }
    QTabWidget::pane {
        border: 1px solid #3a3a3a;
        background: #262626;
    }
    QTabBar::tab {
        background: #2b2b2b;
        color: #ccc;
        padding: 6px 12px;
        border: 1px solid #3a3a3a;
    }
    QTabBar::tab:selected {
        background: #3b6ea8;
        color: #fff;
    }
)";

QPalette buildDarkPalette()
{
    QPalette p;
    const QColor base(30, 30, 30);
    const QColor altBase(38, 38, 38);
    const QColor window(43, 43, 43);
    const QColor text(230, 230, 230);
    const QColor disabled(120, 120, 120);
    const QColor highlight(59, 110, 168);

    p.setColor(QPalette::Window,          window);
    p.setColor(QPalette::WindowText,      text);
    p.setColor(QPalette::Base,            base);
    p.setColor(QPalette::AlternateBase,   altBase);
    p.setColor(QPalette::ToolTipBase,     window);
    p.setColor(QPalette::ToolTipText,     text);
    p.setColor(QPalette::Text,            text);
    p.setColor(QPalette::Button,          QColor(58, 58, 58));
    p.setColor(QPalette::ButtonText,      text);
    p.setColor(QPalette::BrightText,      Qt::red);
    p.setColor(QPalette::Link,            QColor(120, 170, 230));
    p.setColor(QPalette::Highlight,       highlight);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::PlaceholderText, QColor(150, 150, 150));

    p.setColor(QPalette::Disabled, QPalette::Text,            disabled);
    p.setColor(QPalette::Disabled, QPalette::ButtonText,      disabled);
    p.setColor(QPalette::Disabled, QPalette::WindowText,      disabled);
    p.setColor(QPalette::Disabled, QPalette::HighlightedText, disabled);
    p.setColor(QPalette::Disabled, QPalette::Highlight,       QColor(50, 70, 95));

    return p;
}

} // namespace

void AppTheme::apply(Mode m)
{
    g_mode = m;
    if (m == Mode::Dark) applyDark();
    else                  applyLight();
}

AppTheme::Mode AppTheme::current() { return g_mode; }

void AppTheme::applyDark()
{
    qApp->setStyle(QStyleFactory::create("Fusion"));
    qApp->setPalette(buildDarkPalette());
    qApp->setStyleSheet(QString::fromUtf8(kDarkQss));
}

void AppTheme::applyLight()
{
    qApp->setStyle(QStyleFactory::create("Fusion"));
    qApp->setPalette(qApp->style()->standardPalette());
    qApp->setStyleSheet(QString());
}

} // namespace HighPro
