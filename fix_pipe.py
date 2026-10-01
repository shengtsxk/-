p = r"D:\lingjing\src\pipeline\FrameGenPipeline.cpp"
s = open(p, encoding="utf-8").read()

# 1. wrap sceneDetector analyzeFrame
old1 = """        if (sceneDetector_ && hasPrevFrame_) {
            sceneDetector_->analyzeFrame(currentFrame, prevFrame_, sceneAnalysis);
        }"""
new1 = """        if (sceneDetector_ && hasPrevFrame_) {
            try {
                sceneDetector_->analyzeFrame(currentFrame, prevFrame_, sceneAnalysis);
            }
            catch (...) {
                LOG_WARN("Scene analysis exception ignored");
            }
        }"""
if old1 in s:
    s = s.replace(old1, new1); print("patched1")
else:
    print("MISS1")

# 2. wrap processOneFrame call in thread proc
old2 = """            // 处理一帧
            if (!processOneFrame()) {
                // 错误后短延迟
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }"""
new2 = """            // 处理一帧
            try {
                if (!processOneFrame()) {
                    // 错误后短延迟
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
            catch (const std::exception& e) {
                LOG_ERROR("Pipeline frame exception: %s", e.what());
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            catch (...) {
                LOG_ERROR("Pipeline frame unknown exception");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }"""
if old2 in s:
    s = s.replace(old2, new2); print("patched2")
else:
    print("MISS2")

open(p, "w", encoding="utf-8", newline="").write(s)
