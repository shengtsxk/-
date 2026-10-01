p = r"D:\lingjing\src\pipeline\LatencyController.cpp"
s = open(p, encoding="utf-8").read()
old = """    void LatencyController::updateQuality() {
        if (history_.size() < 30) return;  // 至少需要 30 帧样本

        DynamicQuality current = currentQuality_.load();
        DynamicQuality suggested = suggestedQuality();
"""
new = """    void LatencyController::updateQuality() {
        // 调用方（recordFrame）已持有 mutex_，此处不再调用加锁的查询方法（避免递归死锁）
        if (history_.size() < 30) return;  // 至少需要 30 帧样本

        double avg = stats_.avgMs;
        double p95 = stats_.p95Ms;
        DynamicQuality current = currentQuality_.load();
        DynamicQuality suggested = current;

        if (p95 > targetMs_ * 1.5 || avg > targetMs_ * 1.2) {
            suggested = DynamicQuality::Low;
        }
        else if (p95 > targetMs_ * 1.1 || avg > targetMs_) {
            suggested = DynamicQuality::Medium;
        }
        else if (avg < targetMs_ * 0.6 && p95 < targetMs_ * 0.85) {
            suggested = DynamicQuality::Max;
        }
        else if (avg < targetMs_ * 0.8 && p95 < targetMs_ * 0.95) {
            suggested = DynamicQuality::High;
        }
"""
if old in s:
    s = s.replace(old, new); print("patched")
else:
    print("MISS")
open(p, "w", encoding="utf-8", newline="").write(s)
