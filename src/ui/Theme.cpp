#include "ui/Theme.h"
#include "core/Logger.h"

#include <QApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>
#include <QSettings>

namespace Lingjing {
    namespace UI {

        // ============================================================================
        // 单例
        // ============================================================================

        Theme& Theme::instance() {
            static Theme inst;
            return inst;
        }

        Theme::Theme() {
            // 字体
            fontRegular_ = QFont("Microsoft YaHei UI", 9);
            fontBold_ = QFont("Microsoft YaHei UI", 9, QFont::Bold);
            fontMono_ = QFont("Consolas", 9);
            fontTitle_ = QFont("Microsoft YaHei UI", 14, QFont::Bold);

            loadDarkScheme();

            // 面板控件透明度（0-100）
            QSettings settings;
            panelOpacity_ = settings.value("ui/panel_opacity", 95).toInt();
        }

        Theme::~Theme() = default;

        // ============================================================================
        // 模式
        // ============================================================================

        void Theme::setMode(ThemeMode mode) {
            if (mode == ThemeMode::Auto) {
                // 根据系统判断
                QPalette palette = QApplication::palette();
                bool isDark = palette.color(QPalette::Window).lightness() < 128;
                mode = isDark ? ThemeMode::Dark : ThemeMode::Light;
            }

            if (mode == mode_) return;

            mode_ = mode;

            switch (mode) {
            case ThemeMode::Dark:
                loadDarkScheme();
                break;
            case ThemeMode::Light:
                loadLightScheme();
                break;
            default:
                break;
            }

            // 应用样式表
            if (qApp) {
                qApp->setStyleSheet(globalStyleSheet());
                qApp->setPalette(buildPalette());
            }
        }

        // ============================================================================
        // 暗色主题
        // ============================================================================

        void Theme::loadDarkScheme() {
            auto& c = colors_;

            // 主色：青蓝色
            c.primary = QColor(0x4A, 0x9E, 0xFF);
            c.primaryLight = QColor(0x7A, 0xBE, 0xFF);
            c.primaryDark = QColor(0x1E, 0x7E, 0xDF);
            c.primaryHover = QColor(0x5A, 0xAE, 0xFF);
            c.primaryPressed = QColor(0x2E, 0x8E, 0xEF);

            // 强调色：紫青
            c.accent = QColor(0x8A, 0x5A, 0xFF);
            c.accentHover = QColor(0x9A, 0x6A, 0xFF);
            c.accentPressed = QColor(0x7A, 0x4A, 0xEF);

            // 背景
            c.background = QColor(0x0F, 0x12, 0x18);
            c.backgroundAlt = QColor(0x14, 0x18, 0x20);
            c.surface = QColor(0x1A, 0x1E, 0x28);
            c.surfaceAlt = QColor(0x22, 0x28, 0x34);
            c.overlay = QColor(0x00, 0x00, 0x00, 0xB0);

            // 边框
            c.border = QColor(0x2A, 0x32, 0x40);
            c.borderLight = QColor(0x3A, 0x42, 0x50);
            c.borderFocus = c.primary;

            // 文本
            c.textPrimary = QColor(0xE8, 0xEC, 0xF2);
            c.textSecondary = QColor(0xAE, 0xB8, 0xC8);
            c.textDisabled = QColor(0x6E, 0x78, 0x90);
            c.textInverse = QColor(0x0F, 0x12, 0x18);

            // 状态
            c.success = QColor(0x4A, 0xD9, 0x8A);
            c.warning = QColor(0xFF, 0xB8, 0x4A);
            c.error = QColor(0xFF, 0x5A, 0x5A);
            c.info = QColor(0x4A, 0x9E, 0xFF);

            // 图表
            c.chartLine1 = QColor(0x4A, 0x9E, 0xFF);
            c.chartLine2 = QColor(0x4A, 0xD9, 0x8A);
            c.chartLine3 = QColor(0x8A, 0x5A, 0xFF);
            c.chartGrid = QColor(0x2A, 0x32, 0x40);
            c.chartBackground = QColor(0x0A, 0x0D, 0x12);
        }

        // ============================================================================
        // 亮色主题
        // ============================================================================

        void Theme::loadLightScheme() {
            auto& c = colors_;

            c.primary = QColor(0x1E, 0x7E, 0xDF);
            c.primaryLight = QColor(0x4A, 0x9E, 0xFF);
            c.primaryDark = QColor(0x0E, 0x5E, 0xBF);
            c.primaryHover = QColor(0x2E, 0x8E, 0xEF);
            c.primaryPressed = QColor(0x0E, 0x6E, 0xCF);

            c.accent = QColor(0x6A, 0x3A, 0xDF);
            c.accentHover = QColor(0x7A, 0x4A, 0xEF);
            c.accentPressed = QColor(0x5A, 0x2A, 0xCF);

            c.background = QColor(0xF5, 0xF7, 0xFA);
            c.backgroundAlt = QColor(0xEC, 0xEF, 0xF4);
            c.surface = QColor(0xFF, 0xFF, 0xFF);
            c.surfaceAlt = QColor(0xF8, 0xFA, 0xFC);
            c.overlay = QColor(0x00, 0x00, 0x00, 0x60);

            c.border = QColor(0xD5, 0xDA, 0xE0);
            c.borderLight = QColor(0xE5, 0xEA, 0xF0);
            c.borderFocus = c.primary;

            c.textPrimary = QColor(0x1A, 0x1E, 0x28);
            c.textSecondary = QColor(0x5A, 0x62, 0x70);
            c.textDisabled = QColor(0x9A, 0xA4, 0xB4);
            c.textInverse = QColor(0xFF, 0xFF, 0xFF);

            c.success = QColor(0x2A, 0xB9, 0x6A);
            c.warning = QColor(0xDF, 0x98, 0x2A);
            c.error = QColor(0xDF, 0x3A, 0x3A);
            c.info = QColor(0x1E, 0x7E, 0xDF);

            c.chartLine1 = QColor(0x1E, 0x7E, 0xDF);
            c.chartLine2 = QColor(0x2A, 0xB9, 0x6A);
            c.chartLine3 = QColor(0x6A, 0x3A, 0xDF);
            c.chartGrid = QColor(0xE5, 0xEA, 0xF0);
            c.chartBackground = QColor(0xFA, 0xFC, 0xFF);
        }

        // ============================================================================
        // 颜色查询
        // ============================================================================

        QColor Theme::color(const QString& name) const {
            const auto& c = colors_;

            if (name == "primary") return c.primary;
            if (name == "accent") return c.accent;
            if (name == "background") return c.background;
            if (name == "backgroundAlt") return c.backgroundAlt;
            if (name == "surface") return c.surface;
            if (name == "surfaceAlt") return c.surfaceAlt;
            if (name == "border") return c.border;
            if (name == "borderLight") return c.borderLight;
            if (name == "textPrimary") return c.textPrimary;
            if (name == "textSecondary") return c.textSecondary;
            if (name == "success") return c.success;
            if (name == "warning") return c.warning;
            if (name == "error") return c.error;
            if (name == "info") return c.info;

            return c.textPrimary;
        }

        // ============================================================================
        // 全局样式表
        // ============================================================================

        QString Theme::globalStyleSheet() const {
            const auto& c = colors_;

            QString primaryHex = c.primary.name();
            QString accentHex = c.accent.name();
            QString bgHex = c.background.name();
            QString surfaceHex = c.surface.name();
            QString surfaceAltHex = c.surfaceAlt.name();
            QString borderHex = c.border.name();
            QString textPrimaryHex = c.textPrimary.name();
            QString textSecondaryHex = c.textSecondary.name();
            QString textDisabledHex = c.textDisabled.name();
            QString successHex = c.success.name();
            QString warningHex = c.warning.name();
            QString errorHex = c.error.name();

            // 面板控件半透明色（alpha 由 panelOpacity_ 决定）
            const int alpha = qBound(0, panelOpacity_ * 255 / 100, 255);
            QColor panelBg = c.background;
            panelBg.setAlpha(alpha);
            QString panelBgRgba = panelBg.name(QColor::HexArgb);

            QColor panelSurface = c.surfaceAlt;
            panelSurface.setAlpha(alpha);
            QString panelSurfaceRgba = panelSurface.name(QColor::HexArgb);

            QString qss = QString(R"(
/* === 全局 === */
QWidget {
    background-color: %18;
    color: %2;
    font-family: "Microsoft YaHei UI";
    font-size: 9pt;
}

QMainWindow {

/* === 背景层容器透明（透出自定义壁纸） === */
QScrollArea { background: transparent; }
QScrollArea > QWidget > QWidget { background-color: %18; }
QAbstractScrollArea QWidget#qt_scrollarea_viewport { background: transparent; }
QScrollArea::viewport { background: transparent; }

/* === 按钮 === */
QPushButton {
            background-color: %19;
    color: %2;
    border: 1px solid %5;
    border-radius: 6px;
    padding: 6px 16px;
    min-height: 20px;
}

QPushButton:hover {
    background-color: %6;
    border-color: %7;
}

QPushButton:pressed {
    background-color: %8;
}

QPushButton:disabled {
    color: %9;
            background-color: %19;
}

QPushButton[primary="true"] {
    background-color: %7;
    color: white;
    border: none;
}

QPushButton[primary="true"]:hover {
    background-color: %10;
}

QPushButton[primary="true"]:pressed {
    background-color: %11;
}

QPushButton[accent="true"] {
    background-color: %12;
    color: white;
    border: none;
}

/* === 输入框 === */
QLineEdit, QTextEdit, QPlainTextEdit {
            background-color: %19;
    color: %2;
    border: 1px solid %5;
    border-radius: 6px;
    padding: 6px 10px;
    selection-background-color: %7;
}

QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus {
    border-color: %7;
}

QLineEdit:disabled {
    color: %9;
}

/* === 下拉框 === */
QComboBox {
            background-color: %19;
    color: %2;
    border: 1px solid %5;
    border-radius: 6px;
    padding: 6px 10px;
    min-width: 80px;
}

QComboBox:hover {
    border-color: %7;
}

QComboBox:focus {
    border-color: %7;
}

QComboBox::drop-down {
    border: none;
    width: 24px;
}

QComboBox::down-arrow {
    image: none;
    border-left: 4px solid transparent;
    border-right: 4px solid transparent;
    border-top: 5px solid %2;
    width: 0;
    height: 0;
    margin-right: 8px;
}

QComboBox QAbstractItemView {
    background-color: %3;
    border: 1px solid %5;
    border-radius: 6px;
    color: %2;
    selection-background-color: %7;
    outline: none;
    padding: 4px;
}

/* === 滑块 === */
QSlider::groove:horizontal {
    height: 4px;
    background: %5;
    border-radius: 2px;
}

QSlider::handle:horizontal {
    background: %7;
    border: none;
    width: 16px;
    height: 16px;
    margin: -6px 0;
    border-radius: 8px;
}

QSlider::handle:horizontal:hover {
    background: %10;
}

QSlider::sub-page:horizontal {
    background: %7;
    border-radius: 2px;
}

/* === 复选框 === */
QCheckBox {
    color: %2;
    spacing: 8px;
}

QCheckBox::indicator {
    width: 18px;
    height: 18px;
    border: 1px solid %5;
    border-radius: 4px;
            background-color: %19;
}

QCheckBox::indicator:hover {
    border-color: %7;
}

QCheckBox::indicator:checked {
    background-color: %7;
    border-color: %7;
}

/* === 单选按钮 === */
QRadioButton {
    color: %2;
    spacing: 8px;
}

QRadioButton::indicator {
    width: 18px;
    height: 18px;
    border: 1px solid %5;
    border-radius: 9px;
            background-color: %19;
}

QRadioButton::indicator:checked {
    background-color: %7;
    border-color: %7;
}

/* === 列表 === */
QListWidget, QListView {
            background-color: %19;
    color: %2;
    border: 1px solid %5;
    border-radius: 6px;
    outline: none;
    padding: 4px;
}

QListWidget::item {
    padding: 8px 12px;
    border-radius: 4px;
    margin: 1px 0;
}

QListWidget::item:hover {
    background-color: %6;
}

QListWidget::item:selected {
    background-color: %7;
    color: white;
}

/* === 标签 === */
QLabel {
    color: %2;
    background: transparent;
}

QLabel[header="true"] {
    font-size: 12pt;
    font-weight: bold;
    color: %2;
}

QLabel[title="true"] {
    font-size: 14pt;
    font-weight: bold;
    color: %2;
}

QLabel[subdued="true"] {
    color: %13;
    font-size: 9pt;
}

/* === 分组框 === */
QGroupBox {
            background-color: %18;
    border: 1px solid %5;
    border-radius: 8px;
    margin-top: 12px;
    padding-top: 16px;
    font-weight: bold;
}

QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    padding: 0 8px;
    color: %2;
}

/* === 选项卡 === */
QTabWidget::pane {
    background-color: %3;
    border: 1px solid %5;
    border-radius: 6px;
    top: -1px;
}

QTabBar::tab {
    background-color: transparent;
    color: %13;
    padding: 8px 20px;
    border: none;
    border-bottom: 2px solid transparent;
}

QTabBar::tab:hover {
    color: %2;
}

QTabBar::tab:selected {
    color: %7;
    border-bottom-color: %7;
}

/* === 滚动条 === */
QScrollBar:vertical {
    background: transparent;
    width: 10px;
    margin: 0;
}

QScrollBar::handle:vertical {
    background: %5;
    border-radius: 5px;
    min-height: 30px;
    margin: 2px;
}

QScrollBar::handle:vertical:hover {
    background: %7;
}

QScrollBar::add-line:vertical,
QScrollBar::sub-line:vertical {
    height: 0;
}

QScrollBar::add-page:vertical,
QScrollBar::sub-page:vertical {
    background: transparent;
}

QScrollBar:horizontal {
    background: transparent;
    height: 10px;
    margin: 0;
}

QScrollBar::handle:horizontal {
    background: %5;
    border-radius: 5px;
    min-width: 30px;
    margin: 2px;
}

QScrollBar::handle:horizontal:hover {
    background: %7;
}

QScrollBar::add-line:horizontal,
QScrollBar::sub-line:horizontal {
    width: 0;
}

QScrollBar::add-page:horizontal,
QScrollBar::sub-page:horizontal {
    background: transparent;
}

/* === 进度条 === */
QProgressBar {
    background-color: %3;
    border: none;
    border-radius: 4px;
    height: 8px;
    text-align: center;
    color: %2;
}

QProgressBar::chunk {
    background-color: %7;
    border-radius: 4px;
}

/* === 工具提示 === */
QToolTip {
    background-color: %14;
    color: %2;
    border: 1px solid %5;
    border-radius: 4px;
    padding: 6px 10px;
}

/* === 菜单 === */
QMenu {
    background-color: %14;
    color: %2;
    border: 1px solid %5;
    border-radius: 6px;
    padding: 4px;
}

QMenu::item {
    padding: 6px 24px 6px 12px;
    border-radius: 4px;
}

QMenu::item:hover {
    background-color: %6;
}

QMenu::item:selected {
    background-color: %7;
    color: white;
}

QMenu::separator {
    height: 1px;
    background: %5;
    margin: 4px 8px;
}

/* === 状态栏 === */
QStatusBar {
    background-color: %4;
    color: %13;
    border-top: 1px solid %5;
}

QStatusBar::item {
    border: none;
}

/* === 帧生成状态指示器 === */
QLabel[indicator="success"] {
    color: %15;
}

QLabel[indicator="warning"] {
    color: %16;
}

QLabel[indicator="error"] {
    color: %17;
}

)")
.arg(bgHex)          // %1 背景
.arg(textPrimaryHex) // %2 主文本
.arg(surfaceHex)     // %3 表面
.arg(surfaceAltHex)  // %4 表面替代
.arg(borderHex)      // %5 边框
.arg(c.borderLight.name())   // %6 边框浅
.arg(primaryHex)     // %7 主色
.arg(c.primaryPressed.name()) // %8 主色按下
.arg(textDisabledHex) // %9 禁用文本
.arg(c.primaryHover.name())   // %10 主色悬停
.arg(c.primaryDark.name())    // %11 主色深
.arg(accentHex)      // %12 强调色
.arg(textSecondaryHex)        // %13 次文本
.arg(c.surfaceAlt.name())     // %14 工具提示背景
.arg(successHex)     // %15 成功
.arg(warningHex)     // %16 警告
            .arg(errorHex)       // %17 错误
            .arg(panelBgRgba)       // %18 面板底色（半透明）
            .arg(panelSurfaceRgba); // %19 面板控件底色（半透明）

            return qss;
        }

        // ============================================================================
        // QPalette
        // ============================================================================

        QPalette Theme::buildPalette() const {
            QPalette p;
            const auto& c = colors_;

            p.setColor(QPalette::Window, c.background);
            p.setColor(QPalette::WindowText, c.textPrimary);
            p.setColor(QPalette::Base, c.surface);
            p.setColor(QPalette::AlternateBase, c.surfaceAlt);
            p.setColor(QPalette::ToolTipBase, c.surfaceAlt);
            p.setColor(QPalette::ToolTipText, c.textPrimary);
            p.setColor(QPalette::Text, c.textPrimary);
            p.setColor(QPalette::Button, c.surface);
            p.setColor(QPalette::ButtonText, c.textPrimary);
            p.setColor(QPalette::BrightText, c.error);
            p.setColor(QPalette::Link, c.primary);
            p.setColor(QPalette::Highlight, c.primary);
            p.setColor(QPalette::HighlightedText, Qt::white);
            p.setColor(QPalette::PlaceholderText, c.textSecondary);

            p.setColor(QPalette::Disabled, QPalette::Text, c.textDisabled);
            p.setColor(QPalette::Disabled, QPalette::ButtonText, c.textDisabled);

            return p;
        }

        // ============================================================================
        // 加载/保存
        // ============================================================================

        bool Theme::loadFromFile(const QString& path) {
            QFile f(path);

            if (!f.open(QIODevice::ReadOnly)) {
                LOG_WARN("Failed to open theme file: %s",
                    path.toStdString().c_str());
                return false;
            }

            QJsonDocument doc = QJsonDocument::fromJson(f.readAll());

            if (!doc.isObject()) return false;

            QJsonObject root = doc.object();

            QString modeStr = root["mode"].toString();

            if (modeStr == "dark") mode_ = ThemeMode::Dark;
            else if (modeStr == "light") mode_ = ThemeMode::Light;
            else if (modeStr == "auto") mode_ = ThemeMode::Auto;

            return true;
        }

        bool Theme::saveToFile(const QString& path) const {
            QJsonObject root;

            switch (mode_) {
            case ThemeMode::Dark: root["mode"] = "dark"; break;
            case ThemeMode::Light: root["mode"] = "light"; break;
            case ThemeMode::Auto: root["mode"] = "auto"; break;
            }

            QFile f(path);

            if (!f.open(QIODevice::WriteOnly)) return false;

            f.write(QJsonDocument(root).toJson());

            return true;
        }

    } // namespace UI
} // namespace Lingjing
