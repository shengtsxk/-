p = r"D:\lingjing\src\ui\MainWindow.cpp"
s = open(p, encoding="utf-8").read()

# onSettingsClicked：内嵌跳转设置面板
old = """        void MainWindow::onSettingsClicked() {
            SettingsDialog dlg(this);
            dlg.exec();

            // 设置保存后：重新加载壁纸、应用面板透明度
            reloadWallpaper();

            // 从 QSettings 重新读入透明度并应用到主题单例（避免改动后界面无反应）
            Theme& theme = Theme::instance();
            QSettings settings;
            theme.setPanelOpacity(settings.value("ui/panel_opacity", 85).toInt());
            applyTheme();
        }
"""
new = """        void MainWindow::onSettingsClicked() {
            // 内嵌跳转：点击设置直接切换到设置面板，不弹独立窗口
            if (!contentStack_ || !settingsPanel_) return;
            settingsPanel_->reloadSettings();
            contentStack_->setCurrentWidget(settingsPanel_);
        }

        void MainWindow::onSettingsBack() {
            // 从设置面板返回主界面，并应用已保存的设置
            if (contentStack_ && contentStack_->count() > 0) {
                contentStack_->setCurrentIndex(0);
            }
            reloadWallpaper();

            // 从 QSettings 重新读入透明度并应用到主题单例
            Theme& theme = Theme::instance();
            QSettings settings;
            theme.setPanelOpacity(settings.value("ui/panel_opacity", 85).toInt());
            applyTheme();
        }
"""
if old in s:
    s = s.replace(old, new, 1); print("onSettingsClicked fixed")
else:
    print("MISS onSettingsClicked")

# 在 setupConnections 里连接 settingsPanel_ 的 backRequested
# 找到 setupConnections 结尾，追加
marker = """        void MainWindow::setupConnections() {"""
if marker in s:
    # 在 setupConnections 函数体内开头追加设置面板连接
    ins = marker + """

            // 设置面板（内嵌）返回时切回主界面并应用设置
            if (settingsPanel_) {
                connect(settingsPanel_, &SettingsDialog::backRequested,
                    this, &MainWindow::onSettingsBack);
            }
"""
    s = s.replace(marker, ins, 1); print("setupConnections wired")
else:
    print("MISS setupConnections marker")
open(p, "w", encoding="utf-8", newline="").write(s)
