p = r"D:\lingjing\src\ui\SettingsDialog.cpp"
s = open(p, encoding="utf-8").read()
old = """            settings.beginGroup("gpu");
            // 枚举本机 GPU 填入选择列表
            {
                auto gpus = detectAllGpus();
                gpuCombo_->clear();
                gpuCombo_->addItem("自动选择");
                for (const auto& g : gpus) {
                    gpuCombo_->addItem(QString::fromStdString(g.toDisplayString()));
                }
            }
            gpuCombo_->setCurrentIndex(
                settings.value("index", 0).toInt());
            tensorCoresCheck_->setChecked(
                settings.value("tensor_cores", true).toBool());
            xmxCheck_->setChecked(settings.value("xmx", true).toBool());
            hwFlowCheck_->setChecked(settings.value("hw_flow", true).toBool());
            settings.endGroup();"""
new = """            settings.beginGroup("gpu");
            // 枚举本机 GPU 填入选择列表
            auto gpus = detectAllGpus();
            {
                gpuCombo_->clear();
                gpuCombo_->addItem("自动选择（识别可用加速）");
                for (const auto& g : gpus) {
                    gpuCombo_->addItem(QString::fromStdString(g.toDisplayString()));
                }
            }
            gpuCombo_->setCurrentIndex(settings.value("index", 0).toInt());

            // ---- 自动识别可用加速能力（Tensor/XMX/硬件光流）----
            // 未保存时按能力默认；已保存时仅当显卡支持才可勾选，不支持则置灰
            {
                int idx = settings.value("index", 0).toInt() - 1;  // 0=自动
                const GpuInfo* sel = nullptr;
                if (idx >= 0 && idx < static_cast<int>(gpus.size())) {
                    sel = &gpus[static_cast<size_t>(idx)];
                }
                bool hasTensor = false, hasXmx = false, hasFlow = false;
                if (sel) {
                    hasTensor = sel->hasTensorCores();
                    hasXmx = sel->hasXmx();
                    hasFlow = sel->hasHardwareOpticalFlow();
                } else {
                    // 自动选择：任一 GPU 具备能力即视为可用
                    for (const auto& g : gpus) {
                        hasTensor |= g.hasTensorCores();
                        hasXmx |= g.hasXmx();
                        hasFlow |= g.hasHardwareOpticalFlow();
                    }
                }

                tensorCoresCheck_->setEnabled(hasTensor);
                xmxCheck_->setEnabled(hasXmx);
                hwFlowCheck_->setEnabled(hasFlow);

                tensorCoresCheck_->setChecked(
                    hasTensor && settings.value("tensor_cores", hasTensor).toBool());
                xmxCheck_->setChecked(
                    hasXmx && settings.value("xmx", hasXmx).toBool());
                hwFlowCheck_->setChecked(
                    hasFlow && settings.value("hw_flow", hasFlow).toBool());

                if (accelStatusLabel_) {
                    QString status;
                    if (hasTensor || hasXmx || hasFlow) {
                        QStringList parts;
                        if (hasTensor) parts << "Tensor Cores";
                        if (hasXmx) parts << "XMX";
                        if (hasFlow) parts << "硬件光流";
                        status = "已识别可用加速：" + parts.join("、");
                    } else {
                        status = "未检测到硬件专用加速（Tensor/XMX/光流），"
                                 "将使用通用 GPU 混合（D3D11）/CPU";
                    }
                    accelStatusLabel_->setText(status);
                }
            }
            settings.endGroup();"""
if old in s:
    s = s.replace(old, new, 1); print("accel auto-detect fixed")
else:
    print("MISS accel")
open(p, "w", encoding="utf-8", newline="").write(s)
