#pragma once

#include <QColor>
#include <QFont>
#include <QString>
#include <QPalette>

namespace Lingjing {
    namespace UI {

        // ============================================================================
        // 主题模式
        // ============================================================================

        enum class ThemeMode {
            Dark,
            Light,
            Auto,
        };

        // ============================================================================
        // 颜色集
        // ============================================================================

        struct ColorScheme {
            // 主色
            QColor primary;
            QColor primaryLight;
            QColor primaryDark;
            QColor primaryHover;
            QColor primaryPressed;

            // 强调色
            QColor accent;
            QColor accentHover;
            QColor accentPressed;

            // 背景
            QColor background;
            QColor backgroundAlt;
            QColor surface;
            QColor surfaceAlt;
            QColor overlay;

            // 边框
            QColor border;
            QColor borderLight;
            QColor borderFocus;

            // 文本
            QColor textPrimary;
            QColor textSecondary;
            QColor textDisabled;
            QColor textInverse;

            // 状态
            QColor success;
            QColor warning;
            QColor error;
            QColor info;

            // 图表
            QColor chartLine1;
            QColor chartLine2;
            QColor chartLine3;
            QColor chartGrid;
            QColor chartBackground;
        };

        // ============================================================================
        // 主题
        // ============================================================================

        class Theme {
        public:
            static Theme& instance();

            // 模式
            void setMode(ThemeMode mode);
            ThemeMode mode() const { return mode_; }

            // 颜色
            const ColorScheme& colors() const { return colors_; }

            QColor color(const QString& name) const;

            // 字体
            QFont fontRegular() const { return fontRegular_; }
            QFont fontBold() const { return fontBold_; }
            QFont fontMono() const { return fontMono_; }
            QFont fontTitle() const { return fontTitle_; }

            // 大小
            int spacingSmall() const { return 4; }
            int spacingMedium() const { return 8; }
            int spacingLarge() const { return 16; }
            int spacingXLarge() const { return 24; }

            int borderRadiusSmall() const { return 4; }
            int borderRadiusMedium() const { return 8; }
            int borderRadiusLarge() const { return 12; }

            // 应用样式表
            QString globalStyleSheet() const;

            // 面板控件透明度（0-100，100=不透明）
            int panelOpacity() const { return panelOpacity_; }
            void setPanelOpacity(int value) { panelOpacity_ = value; }

            // 生成 QPalette
            QPalette buildPalette() const;

            // 从配置文件加载
            bool loadFromFile(const QString& path);

            // 保存到文件
            bool saveToFile(const QString& path) const;

        private:
            Theme();
            ~Theme();

            Theme(const Theme&) = delete;
            Theme& operator=(const Theme&) = delete;

            void loadDarkScheme();
            void loadLightScheme();

            ThemeMode mode_ = ThemeMode::Dark;
            int panelOpacity_ = 85;
            ColorScheme colors_;

            QFont fontRegular_;
            QFont fontBold_;
            QFont fontMono_;
            QFont fontTitle_;
        };

        // ============================================================================
        // 便捷宏
        // ============================================================================

#define LJ_THEME (::Lingjing::UI::Theme::instance())
#define LJ_COLOR(name) (::Lingjing::UI::Theme::instance().colors().name)

    } // namespace UI
} // namespace Lingjing
