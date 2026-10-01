# AI 修复：不可用时输出明确日志（当前静默跳过）
p = r"D:\lingjing\src\pipeline\FrameGenPipeline.cpp"
s = open(p, encoding="utf-8").read()
old = """        // ---- 5. AI 修复 ----
        if (config_.frameGenSettings.aiRepairEnabled) {
            config_.aiConfig = AIRepairFactory::autoConfigure(selectedGpu_);

            if (!config_.aiConfig.modelPath.empty()) {
                aiRepair_ = AIRepairFactory::create(config_.aiConfig.backend);

                if (aiRepair_ && !aiRepair_->initialize(gpuContext_.get(),
                    config_.aiConfig)) {
                    LOG_WARN("AI repair initialization failed, continuing without");
                    aiRepair_.reset();
                }
                else if (aiRepair_) {
                    LOG_INFO("AI repair: %s", aiRepair_->engineName());
                }
            }
        }
"""
new = """        // ---- 5. AI 修复 ----
        if (config_.frameGenSettings.aiRepairEnabled) {
            config_.aiConfig = AIRepairFactory::autoConfigure(selectedGpu_);

            if (config_.aiConfig.backend == AIRepairBackend::None) {
                LOG_WARN("AI repair unavailable: no usable backend for this GPU "
                    "(TensorRT needs CUDA, OpenVINO needs oneAPI; DirectML not built). "
                    "Continuing without AI repair.");
            }
            else if (config_.aiConfig.modelPath.empty()) {
                LOG_WARN("AI repair: backend=%d but no model configured, continuing without",
                    static_cast<int>(config_.aiConfig.backend));
            }
            else {
                aiRepair_ = AIRepairFactory::create(config_.aiConfig.backend);

                if (aiRepair_ && !aiRepair_->initialize(gpuContext_.get(),
                    config_.aiConfig)) {
                    LOG_WARN("AI repair initialization failed, continuing without");
                    aiRepair_.reset();
                }
                else if (aiRepair_) {
                    LOG_INFO("AI repair: %s", aiRepair_->engineName());
                }
            }
        }
"""
if old in s:
    s = s.replace(old, new, 1); print("ai repair log fixed")
else:
    print("MISS ai repair")
open(p, "w", encoding="utf-8", newline="").write(s)
