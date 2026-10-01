p = r"D:\lingjing\src\ui\SettingsDialog.cpp"
s = open(p, encoding="utf-8").read()
old = """        void SettingsDialog::onAccepted() {
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
new = """        void SettingsDialog::onAccepted() {
            saveSettings();
            emit backRequested();
        }

        void SettingsDialog::onRejected() {
            emit backRequested();
        }
"""
if old in s:
    s = s.replace(old, new, 1); print("onAccepted/Rejected fixed")
else:
    print("MISS")
open(p, "w", encoding="utf-8", newline="").write(s)
