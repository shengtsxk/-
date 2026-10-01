# 1. 透明度：onSettingsClicked 读 QSettings 应用到 Theme
p = r"D:\lingjing\src\ui\MainWindow.cpp"
s = open(p, encoding="utf-8").read()
old = """            Theme& theme = Theme::instance();
            applyTheme();
        }

        void MainWindow::onMinimizeClicked() {"""
new = """            // 从 QSettings 重新读入透明度并应用到主题单例（避免改动后界面无反应）
            Theme& theme = Theme::instance();
            QSettings settings;
            theme.setPanelOpacity(settings.value("ui/panel_opacity", 85).toInt());
            applyTheme();
        }

        void MainWindow::onMinimizeClicked() {"""
if old in s:
    s = s.replace(old, new, 1); print("opacity fixed")
else:
    print("MISS opacity")
open(p, "w", encoding="utf-8", newline="").write(s)
