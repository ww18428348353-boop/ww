#pragma once

// 全局主题切换 (Fusion 暗色 / 浅色 Palette + QSS).
//   - 暗色 = 默认, 与 EffectPanel/SchemePanel 等手写 QSS 协调.
//   - 浅色 = 系统默认 Fusion (备选, 视图菜单可切换).
// 调用方: main.cpp 启动 + MainWindow 视图菜单切换.

namespace HighPro {

class AppTheme
{
public:
    enum class Mode { Dark, Light };

    // 应用主题到 QApplication (style + palette + 全局 QSS).
    // 切换后会自动 polish 已有 widget; 内部启动期写 stylesheet, 已建窗口立刻生效.
    static void apply(Mode m);

    static Mode current();

private:
    static void applyDark();
    static void applyLight();
};

} // namespace HighPro
