p = r"D:\lingjing\src\ui\SettingsDialog.cpp"
s = open(p, encoding="utf-8").read()
old = """                hwFlowCheck_ = new QCheckBox("使用硬件光流");
                form->addRow("", hwFlowCheck_);

                layout->addWidget(group);
            }"""
new = """                hwFlowCheck_ = new QCheckBox("使用硬件光流");
                form->addRow("", hwFlowCheck_);

                accelStatusLabel_ = new QLabel("将自动识别可用加速");
                accelStatusLabel_->setProperty("subdued", true);
                accelStatusLabel_->setWordWrap(true);
                form->addRow("", accelStatusLabel_);

                layout->addWidget(group);
            }"""
if old in s:
    s = s.replace(old, new, 1); print("accel label created")
else:
    print("MISS label")
open(p, "w", encoding="utf-8", newline="").write(s)
