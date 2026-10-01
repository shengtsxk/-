p = r"D:\lingjing\src\ui\MainWindow.cpp"
s = open(p, encoding="utf-8").read()

# 1. 构造函数：连接背景动画信号
old1 = """            setupUi();
            reloadWallpaper();
            applyTheme();

            // 更新定时器（每秒 30 次）"""
new1 = """            setupUi();
            reloadWallpaper();
            applyTheme();

            // 背景动画统一由主窗口背景层重绘（壁纸/水波控件自身不绘制，避免双重绘制错位）
            if (wallpaperEngine_) {
                connect(wallpaperEngine_, &WallpaperEngine::frameUpdated,
                    this, [this]() { update(); });
            }
            if (rippleWidget_) {
                connect(rippleWidget_, &RippleWidget::frameUpdated,
                    this, [this]() { update(); });
            }

            // 更新定时器（每秒 30 次）"""
if old1 in s:
    s = s.replace(old1, new1); print("patch1 ok")
else:
    print("patch1 MISS")

# 2. onRippleTriggered：统一由主窗口重绘
old2 = """        void MainWindow::onRippleTriggered(const QPoint& position) {
            // 水波由 RippleWidget 自绘，仅需触发重绘
            if (rippleWidget_) {
                rippleWidget_->update();
            }
            Q_UNUSED(position);
        }"""
new2 = """        void MainWindow::onRippleTriggered(const QPoint& position) {
            // 水波由主窗口背景层统一绘制
            Q_UNUSED(position);
            update();
        }"""
if old2 in s:
    s = s.replace(old2, new2); print("patch2 ok")
else:
    print("patch2 MISS")

# 3. onStartClicked：从 QSettings 读取用户配置（替代硬编码）
old3 = """            // 首次启动：初始化管线（设置目标窗口与基础配置）
            if (pipeline_->state() == PipelineState::Uninitialized) {
                PipelineConfig cfg;
                cfg.captureConfig.targetHwnd = selectedGameHwnd_;
                cfg.captureConfig.captureColor = true;
                cfg.captureConfig.useSharedTexture = true;
                cfg.frameGenSettings.multiplier = 2;
                cfg.frameGenSettings.aiRepairEnabled = true;

                if (!pipeline_->initialize(cfg)) {
                    LOG_ERROR("Pipeline initialize failed");
                    return;
                }
            }"""
new3 = """            // 首次启动：初始化管线（读取用户设置，替代硬编码）
            if (pipeline_->state() == PipelineState::Uninitialized) {
                QSettings settings;

                PipelineConfig cfg;
                cfg.captureConfig.targetHwnd = selectedGameHwnd_;
                cfg.captureConfig.captureColor = true;
                cfg.captureConfig.useSharedTexture = true;

                cfg.frameGenSettings.multiplier = static_cast<uint32_t>(
                    settings.value("frame_gen/multiplier", 2).toUInt());
                cfg.frameGenSettings.aiRepairEnabled =
                    settings.value("ai/enabled", true).toBool();

                // 捕获后端（0 自动 / 1 WGC / 2 DXGI）
                int backend = settings.value("capture/backend", 0).toInt();
                cfg.captureConfig.backend = (backend == 2)
                    ? CaptureBackend::DXGI
                    : CaptureBackend::WGC;

                // 延迟目标（0 极限低延迟 ... 3 画质优先）
                cfg.latencyTarget = static_cast<LatencyTarget>(
                    settings.value("performance/latency_target", 3).toUInt());

                // 低延迟：输出缓冲队列固定为 1，避免帧积压
                cfg.presentConfig.enableFrameQueue = true;
                cfg.presentConfig.maxQueueSize = 1;

                if (!pipeline_->initialize(cfg)) {
                    LOG_ERROR("Pipeline initialize failed");
                    return;
                }
            }"""
if old3 in s:
    s = s.replace(old3, new3); print("patch3 ok")
else:
    print("patch3 MISS")

open(p, "w", encoding="utf-8", newline="").write(s)
