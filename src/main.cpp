// ============================================================================
// 灵境 Lingjing — 主入口
// ============================================================================

#include "ui/MainWindow.h"
#include "ui/Theme.h"
#include "core/Logger.h"
#include "core/DeviceCaps.h"
#include "core/UpdateChecker.h"
#include "pipeline/FrameGenPipeline.h"
#include "pipeline/D3D11FrameInterpolator.h"
#include "pipeline/D3D11SuperResolution.h"

#include <QApplication>
#include <QFont>
#include <QDir>
#include <QStandardPaths>
#include <QSettings>
#include <QTranslator>
#include <QStyleFactory>
#include <QScreen>
#include <QMessageBox>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QIcon>
#include <QDesktopServices>
#include <QUrl>
#include <QNetworkReply>
#include <QNetworkAccessManager>
#include <QFile>
#include <QProcess>

#include <filesystem>
#include <memory>
#include <cstdlib>
#include <string>
#include <thread>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#include <shellscalingapi.h>
#pragma comment(lib, "shcore.lib")
#endif

namespace fs = std::filesystem;

// ============================================================================
// 应用初始化
// ============================================================================

static QString getAppDataPath() {
    QString path = QStandardPaths::writableLocation(
        QStandardPaths::AppDataLocation);

    if (path.isEmpty()) {
        path = QDir::homePath() + "/.lingjing";
    }

    QDir dir(path);

    if (!dir.exists()) {
        dir.mkpath(".");
    }

    return path;
}

static bool initializeLogging() {
    QString dataPath = getAppDataPath();
    QString logPath = dataPath + "/logs";

    QDir().mkpath(logPath);

    auto& logger = Lingjing::Logger::instance();

    return logger.initialize(logPath.toStdString(),
        "lingjing.log");
}

static void setupStyleSheet(QApplication& app) {
    app.setStyle(QStyleFactory::create("Fusion"));

    // 应用主题
    auto& theme = Lingjing::UI::Theme::instance();

    QSettings settings;
    int themeIndex = settings.value("general.theme", 0).toInt();

    switch (themeIndex) {
    case 0: theme.setMode(Lingjing::UI::ThemeMode::Dark); break;
    case 1: theme.setMode(Lingjing::UI::ThemeMode::Light); break;
    case 2: theme.setMode(Lingjing::UI::ThemeMode::Auto); break;
    default: theme.setMode(Lingjing::UI::ThemeMode::Dark);
    }

    app.setStyleSheet(theme.globalStyleSheet());
    app.setPalette(theme.buildPalette());
}

// ============================================================================
// 主函数
// ============================================================================

#ifdef _WIN32
    // 崩溃信息捕获（Vectored Exception Handler）
    static FILE* crashLogFile = nullptr;
    LONG WINAPI CrashHandler(PEXCEPTION_POINTERS p) {
        if (!crashLogFile) {
            crashLogFile = _wfopen(L"C:\\\\Users\\\\Asgard\\\\AppData\\\\Roaming\\\\LingjingProject\\\\Lingjing\\\\logs\\\\crash_info.txt", L"a");
        }
        if (crashLogFile) {
            fprintf(crashLogFile, "ExceptionCode=0x%08X Addr=%p Thread=%lu\\n",
                (unsigned)p->ExceptionRecord->ExceptionCode,
                p->ExceptionRecord->ExceptionAddress,
                (unsigned long)GetCurrentThreadId());
            HMODULE hm = nullptr;
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                (LPCWSTR)p->ExceptionRecord->ExceptionAddress, &hm);
            if (hm) {
                wchar_t path[512] = {};
                GetModuleFileNameW(hm, path, 512);
                fwprintf(crashLogFile, L"  Module: %ls\\n", path);
            }
            fflush(crashLogFile);
        }
        return EXCEPTION_CONTINUE_SEARCH;
    }
#endif

int main(int argc, char* argv[]) {

#ifdef _WIN32
    AddVectoredExceptionHandler(1, CrashHandler);
#endif




    // 启用高 DPI 支持
#ifdef _WIN32
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
#endif

    QApplication app(argc, argv);

    // 字体自适应：按主屏 DPI 缩放全局默认字体（高分屏自动放大，避免字体过小）
    {
        QScreen* screen = QGuiApplication::primaryScreen();
        if (screen) {
            const qreal dpi = screen->logicalDotsPerInch();
            const qreal scale = qMax<qreal>(1.0, dpi / 96.0);
            QFont f = app.font();
            f.setPointSizeF(qMax<qreal>(8.0, f.pointSizeF() * scale));
            app.setFont(f);
            LOG_INFO("Font DPI adaptive: logicalDPI=%.1f scale=%.2f",
                static_cast<double>(dpi), static_cast<double>(scale));
        }
    }

    // 应用信息
    app.setApplicationName("Lingjing");
    app.setApplicationDisplayName("灵境");
    app.setOrganizationName("LingjingProject");
    app.setOrganizationDomain("lingjing.dev");
    app.setApplicationVersion("1.3.9");

    // 应用图标（青鸾）
    QIcon appIcon("assets/icons/qingluan.png");
    if (!appIcon.isNull()) {
        app.setWindowIcon(appIcon);
    }

    // 日志
    if (!initializeLogging()) {
        QMessageBox::warning(nullptr, "警告",
            "无法初始化日志系统，部分功能可能不可用。");
    }

    LOG_INFO("========================================");
    LOG_INFO("灵境 Lingjing v" LINGJING_VERSION_STRING);
    LOG_INFO("通用游戏帧生成软件");
    LOG_INFO("========================================");

    // GPU 检测
    {
        auto gpus = Lingjing::detectAllGpus();


        LOG_INFO("检测到 %zu 个 GPU:", gpus.size());

        for (const auto& gpu : gpus) {
            LOG_INFO("  - %s", gpu.toDisplayString().c_str());
        }

        if (gpus.empty()) {
            QMessageBox::critical(nullptr, "错误",
                "未检测到可用的 GPU。\n"
                "灵境需要 NVIDIA 或 Intel GPU。");

            return 1;
        }
    }

    // 样式
    setupStyleSheet(app);

    // ============================================================================
    // 自检模式：--selftest 自动选择窗口跑完整管线（验证插帧与输出）
    // ============================================================================
    {
        bool selftest = false;
        for (int i = 1; i < argc; ++i) {
            if (std::string(argv[i]) == "--selftest") { selftest = true; break; }
        }

        if (selftest) {
            LOG_INFO("================ 自检模式 ================");

            // 动画源窗口与消息泵同线程（窗口消息只能由创建线程处理）
            std::atomic<bool> pumpRunning{ true };
            std::atomic<HWND> srcHwndAtomic{ nullptr };

            std::thread pumpThread([&]() {
#ifdef _WIN32
                HINSTANCE hInst = GetModuleHandleW(nullptr);
                const wchar_t kSrcClass[] = L"LingjingSelftestSourceWnd";

                auto srcWndProc = [](HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT {
                    if (m == WM_PAINT) {
                        PAINTSTRUCT ps;
                        HDC dc = BeginPaint(h, &ps);
                        RECT rc = {};
                        GetClientRect(h, &rc);
                        static unsigned long phase = 0;
                        phase += 3;
                        HBRUSH br = CreateSolidBrush(RGB(
                            (phase * 5) % 256,
                            (phase * 11) % 256,
                            (phase * 17) % 256));
                        FillRect(dc, &rc, br);
                        DeleteObject(br);
                        int cx = rc.left + (int)((phase % 200) * (rc.right - rc.left) / 200.0f);
                        int cy = rc.top + (int)((phase % 150) * (rc.bottom - rc.top) / 150.0f);
                        HBRUSH dot = CreateSolidBrush(RGB(255, 255, 255));
                        SelectObject(dc, dot);
                        Ellipse(dc, cx - 30, cy - 30, cx + 30, cy + 30);
                        DeleteObject(dot);
                        EndPaint(h, &ps);
                        return 0;
                    }
                    if (m == WM_TIMER) {
                        InvalidateRect(h, nullptr, TRUE);
                        return 0;
                    }
                    if (m == WM_DESTROY) {
                        KillTimer(h, 1);
                        return 0;
                    }
                    return DefWindowProcW(h, m, w, l);
                };

                WNDCLASS wc = {};
                wc.lpfnWndProc = srcWndProc;
                wc.hInstance = hInst;
                wc.lpszClassName = kSrcClass;
                RegisterClassW(&wc);

                HWND h = CreateWindowExW(0, kSrcClass, L"Lingjing Selftest Source",
                    WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                    60, 60, 640, 480, nullptr, nullptr, hInst, nullptr);
                if (h) SetTimer(h, 1, 33, nullptr);
                srcHwndAtomic.store(h);

                MSG msg;
                while (pumpRunning.load()) {
                    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                        TranslateMessage(&msg);
                        DispatchMessageW(&msg);
                        if (msg.message == WM_QUIT) { pumpRunning = false; break; }
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(8));
                }

                if (h) DestroyWindow(h);
                UnregisterClassW(kSrcClass, hInst);
#endif
            });

            // 等待窗口创建完成
            while (srcHwndAtomic.load() == nullptr && pumpRunning.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            HWND targetHwnd = srcHwndAtomic.load();

            LOG_INFO("Selftest target: LingjingSelftestSourceWnd (hwnd=0x%p)", targetHwnd);

            Lingjing::FrameGenPipeline pipeline;
            Lingjing::PipelineConfig cfg;
            cfg.captureConfig.targetHwnd = targetHwnd;
            cfg.captureConfig.captureColor = true;
            cfg.captureConfig.useSharedTexture = true;
            cfg.frameGenSettings.multiplier = 2;
            cfg.frameGenSettings.aiRepairEnabled = true;

            if (!pipeline.initialize(cfg)) {
                LOG_ERROR("Selftest: pipeline initialize failed");
                pumpRunning = false;
                return 3;
            }

            if (!pipeline.start()) {
                LOG_ERROR("Selftest: pipeline start failed");
                pumpRunning = false;
                return 4;
            }

            LOG_INFO("Selftest: running 5 seconds...");
            std::this_thread::sleep_for(std::chrono::seconds(5));

            pipeline.stop();
            {
                auto st = pipeline.stats();
                LOG_INFO("Selftest stats: captured=%llu generated=%llu dropped=%llu "
                    "flowMs=%.2f blendMs=%.2f totalMs=%.2f",
                    static_cast<unsigned long long>(st.capturedFrames),
                    static_cast<unsigned long long>(st.generatedFrames),
                    static_cast<unsigned long long>(st.droppedFrames),
                    st.avgFlowMs, st.avgBlendMs, st.avgTotalMs);
            }
            pipeline.shutdown();

            pumpRunning = false;
#ifdef _WIN32
            if (targetHwnd) PostMessageW(targetHwnd, WM_CLOSE, 0, 0);
#endif
            if (pumpThread.joinable()) pumpThread.join();

            // 插帧性能基准（独立于 WGC，合成帧直接喂 D3D11 核心）
            {
                Lingjing::D3D11FrameInterpolator interp;
                if (interp.initialize()) {
                    const int bw = 1280, bh = 720, bn = 4;
                    std::vector<uint8_t> f0((size_t)bw * bh * 4);
                    std::vector<uint8_t> f1((size_t)bw * bh * 4);
                    for (int y = 0; y < bh; ++y) {
                        for (int x = 0; x < bw; ++x) {
                            size_t idx = ((size_t)y * bw + x) * 4;
                            uint8_t g = (uint8_t)((x * 255) / bw);
                            f0[idx + 0] = g; f0[idx + 1] = g / 2; f0[idx + 2] = (uint8_t)(255 - g); f0[idx + 3] = 255;
                            // f1 在 f0 基础上右移白色方块（模拟运动）
                            f1[idx + 0] = g; f1[idx + 1] = g / 2; f1[idx + 2] = (uint8_t)(255 - g); f1[idx + 3] = 255;
                        }
                    }
                    int sq = 120;
                    int step = 80;
                    for (int y = bh / 2 - sq; y < bh / 2 + sq; ++y) {
                        for (int x = bw / 4 - sq + step; x < bw / 4 + sq + step; ++x) {
                            if (x >= 0 && x < bw && y >= 0 && y < bh) {
                                size_t idx = ((size_t)y * bw + x) * 4;
                                f1[idx + 0] = 255; f1[idx + 1] = 255; f1[idx + 2] = 255; f1[idx + 3] = 255;
                            }
                        }
                    }
                    std::vector<std::vector<uint8_t>> out;
                    float fms = 0, gms = 0, q = 0;
                    // 预热
                    interp.generateFrames(f0.data(), f1.data(), bw, bh, bn, out, fms, gms, q);
                    double sumFlow = 0, sumGen = 0, sumTotal = 0;
                    int iters = 8;
                    for (int i = 0; i < iters; ++i) {
                        if (interp.generateFrames(f0.data(), f1.data(), bw, bh, bn, out, fms, gms, q)) {
                            sumFlow += fms; sumGen += gms;
                            sumTotal += interp.lastTotalMs();
                        }
                    }
                    LOG_INFO("[Bench] 1280x720 multiplier=%d iters=%d avgFlow=%.2fms avgGen=%.2fms avgTotal=%.2fms quality=%.2f",
                        bn, iters, sumFlow / iters, sumGen / iters, sumTotal / iters, q);

                    // 保存基准帧用于画面验证
                    {
                        QImage imgF0(f0.data(), bw, bh, bw * 4, QImage::Format_RGB32);
                        QImage imgF1(f1.data(), bw, bh, bw * 4, QImage::Format_RGB32);
                        imgF0.save(QStringLiteral("D:/lingjing/bench_f0.png"));
                        imgF1.save(QStringLiteral("D:/lingjing/bench_f1.png"));
                        if (out.size() >= 2) {
                            QImage imgM0(out[0].data(), bw, bh, bw * 4, QImage::Format_RGB32);
                            QImage imgM1(out[1].data(), bw, bh, bw * 4, QImage::Format_RGB32);
                            imgM0.save(QStringLiteral("D:/lingjing/bench_mid0.png"));
                            imgM1.save(QStringLiteral("D:/lingjing/bench_mid1.png"));
                        }
                    }
                } else {
                    LOG_ERROR("[Bench] D3D11 interpolator initialize failed");
                }
            }

            // 通用超分辨率基准（ED-ASR，独立于插帧核心）
            {
                Lingjing::D3D11SuperResolution sr;
                if (sr.initialize()) {
                    const int sw = 1280, sh = 720;
                    std::vector<uint8_t> src((size_t)sw * sh * 4);
                    // 渐变 + 白色方块边缘（超分最关注高频细节）
                    for (int y = 0; y < sh; ++y) {
                        for (int x = 0; x < sw; ++x) {
                            size_t idx = ((size_t)y * sw + x) * 4;
                            uint8_t g = (uint8_t)((x * 255) / sw);
                            src[idx + 0] = g;
                            src[idx + 1] = g / 2;
                            src[idx + 2] = (uint8_t)(255 - g);
                            src[idx + 3] = 255;
                        }
                    }
                    for (int y = sh / 2 - 60; y < sh / 2 + 60; ++y) {
                        for (int x = sw / 2 - 60; x < sw / 2 + 60; ++x) {
                            size_t idx = ((size_t)y * sw + x) * 4;
                            src[idx + 0] = 255; src[idx + 1] = 128;
                            src[idx + 2] = 0;   src[idx + 3] = 255;
                        }
                    }
                    std::vector<uint8_t> out;
                    float ms = 0;
                    for (int scale : { 2, 3, 4, 5 }) {
                        // 预热
                        sr.upscale(src.data(), sw, sh, scale, out, ms);
                        double sum = 0;
                        int iters = 8;
                        for (int i = 0; i < iters; ++i) {
                            if (sr.upscale(src.data(), sw, sh, scale, out, ms)) {
                                sum += ms;
                            }
                        }
                        LOG_INFO("[Bench] SuperRes ED-ASR %dx%d -> %dx%d scale=%d iters=%d avg=%.2fms",
                            sw, sh, sw * scale, sh * scale, scale, iters, sum / iters);
                    }
                    // 保存 2x 输出用于画面验证
                    {
                        sr.upscale(src.data(), sw, sh, 2, out, ms);
                        QImage imgOut(out.data(), sw * 2, sh * 2,
                            sw * 2 * 4, QImage::Format_RGB32);
                        imgOut.save(QStringLiteral("D:/lingjing/bench_sr2x.png"));
                        LOG_INFO("[Bench] SuperRes output saved: D:/lingjing/bench_sr2x.png (%.2fms)", ms);
                    }
                } else {
                    LOG_ERROR("[Bench] D3D11SuperResolution initialize failed");
                }
            }

            LOG_INFO("Selftest complete");
            return 0;
        }
    }

    // 主窗口
    Lingjing::UI::MainWindow window;

    // 帧生成管线（生命周期贯穿应用运行期）
    auto pipeline = std::make_unique<Lingjing::FrameGenPipeline>();
    window.attachPipeline(pipeline.get());

    // 居中显示
    {
        QScreen* screen = QGuiApplication::primaryScreen();
        if (screen) {
            QRect screenGeom = screen->availableGeometry();
            QRect windowGeom = window.frameGeometry();

            int x = screenGeom.center().x() - windowGeom.width() / 2;
            int y = screenGeom.center().y() - windowGeom.height() / 2;

            window.move(x, y);
        }
    }

    window.show();

    LOG_INFO("主窗口已显示");
    // 更新检查（异步，不阻塞启动；弹窗+下载逻辑统一在 MainWindow::checkForUpdates）
    {
        QSettings s;
        if (s.value("general/check_updates", true).toBool()) {
            window.checkForUpdates();
        }
    }

    int result = app.exec();

    LOG_INFO("灵境退出，返回码: %d", result);

    return result;
}
