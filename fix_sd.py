p = r"D:\lingjing\src\ui\SettingsDialog.cpp"
s = open(p, encoding="utf-8").read()

# 1. includes
old_inc = """#include "ui/SettingsDialog.h"
#include "ui/Theme.h"
#include "pipeline/FrameGenPipeline.h"
#include "learning/LearningManager.h"
#include "core/Logger.h"
"""
new_inc = """#include "ui/SettingsDialog.h"
#include "ui/Theme.h"
#include "pipeline/FrameGenPipeline.h"
#include "learning/LearningManager.h"
#include "core/Logger.h"
#include "core/DeviceCaps.h"
"""
if old_inc in s:
    s = s.replace(old_inc, new_inc); print("inc ok")
else:
    print("inc MISS")

# 2. loadSettings 补全
old_ls = """            settings.beginGroup("gpu");
            tensorCoresCheck_->setChecked(
                settings.value("tensor_cores", true).toBool());
            xmxCheck_->setChecked(settings.value("xmx", true).toBool());
            hwFlowCheck_->setChecked(settings.value("hw_flow", true).toBool());
            settings.endGroup();

            settings.beginGroup("capture");
            captureBackendCombo_->setCurrentIndex(
                settings.value("backend", 0).toInt());
            captureCursorCheck_->setChecked(
                settings.value("cursor", false).toBool());
            settings.endGroup();
"""
new_ls = """            settings.beginGroup("gpu");
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
            settings.endGroup();

            settings.beginGroup("capture");
            captureBackendCombo_->setCurrentIndex(
                settings.value("backend", 0).toInt());
            captureCursorCheck_->setChecked(
                settings.value("cursor", false).toBool());
            settings.endGroup();

            settings.beginGroup("ai");
            aiRepairCheck_->setChecked(
                settings.value("enabled", true).toBool());
            modelPathEdit_->setText(
                settings.value("model_path").toString());
            precisionCombo_->setCurrentIndex(
                settings.value("precision", 1).toInt());
            settings.endGroup();

            settings.beginGroup("performance");
            latencyTargetCombo_->setCurrentIndex(
                settings.value("latency_target", 3).toInt());
            maxFrameTimeSpin_->setValue(
                settings.value("max_frame_time", 8).toInt());
            adaptiveQualityCheck_->setChecked(
                settings.value("adaptive_quality", true).toBool());
            temporalWeightSlider_->setValue(
                settings.value("temporal_weight", 15).toInt());
            settings.endGroup();
"""
if old_ls in s:
    s = s.replace(old_ls, new_ls); print("load ok")
else:
    print("load MISS")

# 3. saveSettings 补全
old_ss = """            settings.beginGroup("gpu");
            settings.setValue("tensor_cores", tensorCoresCheck_->isChecked());
            settings.setValue("xmx", xmxCheck_->isChecked());
            settings.setValue("hw_flow", hwFlowCheck_->isChecked());
            settings.endGroup();

            settings.beginGroup("capture");
            settings.setValue("backend", captureBackendCombo_->currentIndex());
            settings.setValue("cursor", captureCursorCheck_->isChecked());
            settings.endGroup();
"""
new_ss = """            settings.beginGroup("gpu");
            settings.setValue("index", gpuCombo_->currentIndex());
            settings.setValue("tensor_cores", tensorCoresCheck_->isChecked());
            settings.setValue("xmx", xmxCheck_->isChecked());
            settings.setValue("hw_flow", hwFlowCheck_->isChecked());
            settings.endGroup();

            settings.beginGroup("capture");
            settings.setValue("backend", captureBackendCombo_->currentIndex());
            settings.setValue("cursor", captureCursorCheck_->isChecked());
            settings.endGroup();

            settings.beginGroup("ai");
            settings.setValue("enabled", aiRepairCheck_->isChecked());
            settings.setValue("model_path", modelPathEdit_->text());
            settings.setValue("precision", precisionCombo_->currentIndex());
            settings.endGroup();

            settings.beginGroup("performance");
            settings.setValue("latency_target", latencyTargetCombo_->currentIndex());
            settings.setValue("max_frame_time", maxFrameTimeSpin_->value());
            settings.setValue("adaptive_quality", adaptiveQualityCheck_->isChecked());
            settings.setValue("temporal_weight", temporalWeightSlider_->value());
            settings.endGroup();

            // 开机自启（写入注册表 Run 键）
            {
                QSettings runKey(
                    "HKEY_CURRENT_USER\\\\Software\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\Run",
                    QSettings::NativeFormat);
                if (startWithWindowsCheck_->isChecked()) {
                    runKey.setValue("Lingjing",
                        QCoreApplication::applicationFilePath());
                } else {
                    runKey.remove("Lingjing");
                }
            }
"""
if old_ss in s:
    s = s.replace(old_ss, new_ss); print("save ok")
else:
    print("save MISS")

open(p, "w", encoding="utf-8", newline="").write(s)
