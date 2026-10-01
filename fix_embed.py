p = r"D:\lingjing\src\ui\SettingsDialog.cpp"
s = open(p, encoding="utf-8").read()

# 构造：QDialog -> QWidget，去掉顶层窗口语义
old = """        SettingsDialog::SettingsDialog(QWidget* parent)
            : QDialog(parent)
        {
            setWindowTitle("灵境 - 设置");
            setMinimumSize(720, 640);
            resize(800, 700);

            setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

            setupUi();
            loadSettings();

            setStyleSheet(Theme::instance().globalStyleSheet());
        }

        SettingsDialog::~SettingsDialog() = default;
"""
new = """        SettingsDialog::SettingsDialog(QWidget* parent)
            : QWidget(parent)
        {
            setMinimumSize(720, 600);

            setupUi();
            loadSettings();

            setStyleSheet(Theme::instance().globalStyleSheet());
        }

        SettingsDialog::~SettingsDialog() = default;

        // ============================================================================
        // 内嵌模式 / 重新加载
        // ============================================================================

        void SettingsDialog::setEmbedded(bool embedded) {
            embedded_ = embedded;
            if (okBtn_) {
                okBtn_->setText(embedded ? "保存并返回" : "保存");
            }
            if (cancelBtn_) {
                cancelBtn_->setText(embedded ? "返回" : "取消");
            }
        }

        void SettingsDialog::reloadSettings() {
            loadSettings();
        }
"""
if old in s:
    s = s.replace(old, new, 1); print("ctor fixed")
else:
    print("MISS ctor")

# onAccepted：内嵌模式保存后发出 backRequested
old2 = """        void SettingsDialog::onAccepted() {
            saveSettings();
            accept();
        }

        void SettingsDialog::onRejected() {
            reject();
        }
"""
new2 = """        void SettingsDialog::onAccepted() {
            saveSettings();
            if (embedded_) {
                emit backRequested();
            } else {
                accept();
            }
        }

        void SettingsDialog::onRejected() {
            if (embedded_) {
                emit backRequested();
            } else {
                reject();
            }
        }
"""
if old2 in s:
    s = s.replace(old2, new2, 1); print("accepted fixed")
else:
    print("MISS accepted")
open(p, "w", encoding="utf-8", newline="").write(s)
