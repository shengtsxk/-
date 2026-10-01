# 1) 按钮 enable：管线状态变化时同步 启动/暂停/停止
p = r"D:\lingjing\src\ui\MainWindow.cpp"
s = open(p, encoding="utf-8").read()

old = """                        QMetaObject::invokeMethod(this, [this, text]() {
                            if (stateLabel_) {
                                stateLabel_->setText(text);
                                if (text == "错误") {
                                    stateLabel_->setStyleSheet(
                                        "color: #FF6B6B; font-size: 12px; font-weight: bold;");
                                } else {
                                    stateLabel_->setStyleSheet(
                                        "color: #4AD98A; font-size: 12px; font-weight: bold;");
                                }
                            }
                        }, Qt::QueuedConnection);"""
new = """                        QMetaObject::invokeMethod(this, [this, text, newState]() {
                            if (stateLabel_) {
                                stateLabel_->setText(text);
                                if (text == "错误") {
                                    stateLabel_->setStyleSheet(
                                        "color: #FF6B6B; font-size: 12px; font-weight: bold;");
                                } else {
                                    stateLabel_->setStyleSheet(
                                        "color: #4AD98A; font-size: 12px; font-weight: bold;");
                                }
                            }
                            // 同步按钮可用状态（启动/暂停/停止）
                            if (startBtn_ && pauseBtn_ && stopBtn_) {
                                switch (newState) {
                                case PipelineState::Running:
                                    startBtn_->setEnabled(false);
                                    pauseBtn_->setEnabled(true);
                                    stopBtn_->setEnabled(true);
                                    break;
                                case PipelineState::Paused:
                                    startBtn_->setEnabled(true);
                                    pauseBtn_->setEnabled(false);
                                    stopBtn_->setEnabled(true);
                                    break;
                                default:
                                    startBtn_->setEnabled(true);
                                    pauseBtn_->setEnabled(false);
                                    stopBtn_->setEnabled(false);
                                    break;
                                }
                            }
                        }, Qt::QueuedConnection);"""
if old in s:
    s = s.replace(old, new, 1); print("btn enable fixed")
else:
    print("MISS btn")

# 2) 源帧率：0 时显示 --
old2 = """            if (sourceFpsLabel_) {
                sourceFpsLabel_->setText(QString("源: %1 FPS")
                    .arg(static_cast<int>(stats.currentSourceFps)));
            }
            if (outputFpsLabel_) {
                outputFpsLabel_->setText(QString("输出: %1 FPS")
                    .arg(static_cast<int>(stats.currentOutputFps)));
            }"""
new2 = """            if (sourceFpsLabel_) {
                if (stats.currentSourceFps <= 0.0f) {
                    sourceFpsLabel_->setText("源: -- FPS");
                } else {
                    sourceFpsLabel_->setText(QString("源: %1 FPS")
                        .arg(static_cast<int>(stats.currentSourceFps)));
                }
            }
            if (outputFpsLabel_) {
                if (stats.currentOutputFps <= 0.0f) {
                    outputFpsLabel_->setText("输出: -- FPS");
                } else {
                    outputFpsLabel_->setText(QString("输出: %1 FPS")
                        .arg(static_cast<int>(stats.currentOutputFps)));
                }
            }"""
if old2 in s:
    s = s.replace(old2, new2, 1); print("fps fixed")
else:
    print("MISS fps")
open(p, "w", encoding="utf-8", newline="").write(s)
