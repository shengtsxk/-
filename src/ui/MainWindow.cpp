#include "ui/MainWindow.h"
#include "ui/ControlPanel.h"
#include "ui/GameListWidget.h"
#include "ui/PerformanceGraphWidget.h"
#include "ui/RippleWidget.h"
#include "ui/WallpaperEngine.h"
#include "ui/TrayIcon.h"
#include "ui/SettingsDialog.h"
#include "pipeline/FrameGenPipeline.h"
#include "core/Logger.h"
#include "core/UpdateChecker.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QTimer>
#include <QMouseEvent>
#include <QCloseEvent>
#include <QResizeEvent>
#include <QPainter>
#include <QPainterPath>
#include <QGraphicsDropShadowEffect>
#include <QApplication>
#include <QScreen>
#include <QSettings>
#include <QFileInfo>
#include <QStandardPaths>
#include <QDir>
#include <QDialog>
#include <QUrl>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QProcess>

namespace Lingjing {
    namespace UI {

        // ============================================================================
        // 构造/析构
        // ============================================================================

        MainWindow::MainWindow(QWidget* parent)
            : QMainWindow(parent)
        {
            setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
            setAttribute(Qt::WA_TranslucentBackground, false);
            setMinimumSize(1280, 800);
            resize(1440, 900);

            setupUi();
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

            // 更新定时器（每秒 30 次）
            updateTimer_ = new QTimer(this);
            connect(updateTimer_, &QTimer::timeout,
                this, &MainWindow::onUpdateTimer);
            updateTimer_->start(33);

            // 窗口位置自愈：被系统/第三方安全软件挪出屏幕时，自动移回主屏中央
            selfHealTimer_ = new QTimer(this);
            connect(selfHealTimer_, &QTimer::timeout, this, [this]() {
                if (!isVisible() || isMinimized()) return;
                QScreen* scr = QGuiApplication::primaryScreen();
                if (!scr) return;
                const QRect screenGeom = scr->availableGeometry();
                const QRect geom = frameGeometry();
                if (!screenGeom.intersects(geom)) {
                    move(screenGeom.center().x() - width() / 2,
                        screenGeom.center().y() - height() / 2);
                }
            });
            selfHealTimer_->start(1500);
        }

        MainWindow::~MainWindow() {
            if (updateTimer_) {
                updateTimer_->stop();
            }
        }

        // ============================================================================
        // 设置 UI
        // ============================================================================

        void MainWindow::setupUi() {
            QWidget* central = new QWidget(this);
            central->setStyleSheet("background: transparent;");

            QVBoxLayout* mainLayout = new QVBoxLayout(central);
            mainLayout->setContentsMargins(0, 0, 0, 0);
            mainLayout->setSpacing(0);

            // 标题栏
            setupTitleBar();
            mainLayout->addWidget(titleBar_);

            // 内容区
            setupContentArea();
            mainLayout->addWidget(contentArea_, 1);

            // 底部栏
            setupStatusBar();
            mainLayout->addWidget(bottomBar_);

            setCentralWidget(central);

            // 托盘
            trayIcon_ = new TrayIcon(this);

            setupConnections();
        }

        // ============================================================================
        // 标题栏
        // ============================================================================

        void MainWindow::setupTitleBar() {
            titleBar_ = new QWidget(this);
            titleBar_->setObjectName("titleBar");
            titleBar_->setFixedHeight(56);

            QHBoxLayout* layout = new QHBoxLayout(titleBar_);
            layout->setContentsMargins(16, 0, 16, 0);
            layout->setSpacing(12);

            // 图标
            appIcon_ = new QLabel(this);
            appIcon_->setFixedSize(32, 32);
            appIcon_->setStyleSheet(
                "background-color: #4A9EFF;"
                "border-radius: 8px;"
                "color: white;"
                "font-size: 18px;"
                "font-weight: bold;");
            appIcon_->setAlignment(Qt::AlignCenter);
            appIcon_->setText("灵");
            layout->addWidget(appIcon_);

            // 标题
            appTitle_ = new QLabel("灵境 Lingjing", this);
            appTitle_->setObjectName("appTitle");
            appTitle_->setStyleSheet(
                "font-size: 14px;"
                "font-weight: bold;"
                "color: #E8ECF2;");
            layout->addWidget(appTitle_);

            // 副标题
            QLabel* subtitle = new QLabel("通用游戏帧生成", this);
            subtitle->setStyleSheet(
                "font-size: 10px;"
                "color: #9AA4B4;"
                "margin-left: 4px;");
            layout->addWidget(subtitle);

            layout->addStretch();

            // 设置按钮
            settingsBtn_ = new QPushButton("⚙ 设置", this);
            settingsBtn_->setObjectName("settingsBtn");
            settingsBtn_->setFixedHeight(32);
            settingsBtn_->setCursor(Qt::PointingHandCursor);
            settingsBtn_->setStyleSheet(
                "QPushButton {"
                "  background-color: transparent;"
                "  color: #9AA4B4;"
                "  border: 1px solid #2A3240;"
                "  border-radius: 6px;"
                "  padding: 4px 16px;"
                "}"
                "QPushButton:hover {"
                "  color: #E8ECF2;"
                "  border-color: #4A9EFF;"
                "}");
            layout->addWidget(settingsBtn_);

            layout->addSpacing(8);

            // 窗口控制
            auto makeWindowButton = [this](const QString& text) -> QPushButton* {
                QPushButton* btn = new QPushButton(text, this);
                btn->setFixedSize(36, 32);
                btn->setCursor(Qt::PointingHandCursor);
                btn->setStyleSheet(
                    "QPushButton {"
                    "  background-color: transparent;"
                    "  color: #9AA4B4;"
                    "  border: none;"
                    "  border-radius: 6px;"
                    "  font-size: 14px;"
                    "}"
                    "QPushButton:hover {"
                    "  background-color: #2A3240;"
                    "  color: #E8ECF2;"
                    "}");
                return btn;
            };
            minimizeBtn_ = makeWindowButton("−");
            maximizeBtn_ = makeWindowButton("□");
            closeBtn_ = makeWindowButton("×");

            closeBtn_->setStyleSheet(
                "QPushButton {"
                "  background-color: transparent;"
                "  color: #9AA4B4;"
                "  border: none;"
                "  border-radius: 6px;"
                "  font-size: 14px;"
                "}"
                "QPushButton:hover {"
                "  background-color: #FF5A5A;"
                "  color: white;"
                "}");

            layout->addWidget(minimizeBtn_);
            layout->addWidget(maximizeBtn_);
            layout->addWidget(closeBtn_);

            titleBar_->setStyleSheet(
                "#titleBar {"
                "  background-color: #141820;"
                "  border-bottom: 1px solid #2A3240;"
                "}");
        }

        // ============================================================================
        // 内容区
        // ============================================================================

        void MainWindow::setupContentArea() {
            contentArea_ = new QWidget(this);
            contentArea_->setStyleSheet("background: transparent;");

            QVBoxLayout* layout = new QVBoxLayout(contentArea_);
            layout->setContentsMargins(0, 0, 0, 0);
            layout->setSpacing(0);

            // 主内容 / 设置面板 双页切换（点击设置不弹独立窗口，直接内嵌跳转）
            contentStack_ = new QStackedWidget(contentArea_);
            contentStack_->setStyleSheet("background: transparent;");
            contentStack_->setFrameShape(QFrame::NoFrame);

            QWidget* mainPage = new QWidget(contentStack_);
            mainPage->setStyleSheet("background: transparent;");
            QHBoxLayout* mainLayout = new QHBoxLayout(mainPage);
            mainLayout->setContentsMargins(0, 0, 0, 0);
            mainLayout->setSpacing(0);

            // 左侧面板：游戏列表
            gameList_ = new GameListWidget(mainPage);
            gameList_->setFixedWidth(320);
            mainLayout->addWidget(gameList_);

            // 中间+右侧容器
            QWidget* rightContainer = new QWidget(mainPage);
            rightContainer->setStyleSheet("background: transparent;");
            QVBoxLayout* rightLayout = new QVBoxLayout(rightContainer);
            rightLayout->setContentsMargins(0, 0, 0, 0);
            rightLayout->setSpacing(0);

            // 上方：水波背景 + 控制面板叠加
            QWidget* centerArea = new QWidget(this);
            centerArea->setStyleSheet("background: transparent;");
            centerArea->setMinimumHeight(400);
            centerArea_ = centerArea;
            centerArea->installEventFilter(this);

            // 动态壁纸引擎作为背景
            wallpaperEngine_ = new WallpaperEngine(centerArea);
            wallpaperEngine_->setGeometry(0, 0, centerArea->width(),
                centerArea->height());

            // 水波触动层
            rippleWidget_ = new RippleWidget(centerArea);
            rippleWidget_->setGeometry(0, 0, centerArea->width(),
                centerArea->height());

            // 控制面板（放入滚动区域）
            controlPanel_ = new ControlPanel(centerArea);

            QScrollArea* panelScroll = new QScrollArea(centerArea);
            panelScroll->setWidget(controlPanel_);
            panelScroll->setWidgetResizable(true);
            panelScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            panelScroll->setFrameShape(QFrame::NoFrame);
            panelScroll->setStyleSheet("QScrollArea { background: transparent; border: none; } QScrollBar:vertical { background: transparent; width: 6px; } QScrollBar::handle:vertical { background: #2A3240; border-radius: 3px; min-height: 30px; }");

            QHBoxLayout* centerLayout = new QHBoxLayout(centerArea);
            centerLayout->setContentsMargins(16, 16, 16, 16);
            centerLayout->addWidget(panelScroll, 1);

            rightLayout->addWidget(centerArea, 1);

            // 下方：性能图表
            perfGraph_ = new PerformanceGraphWidget(this);
            perfGraph_->setFixedHeight(200);
            rightLayout->addWidget(perfGraph_);

            mainLayout->addWidget(rightContainer, 1);

            contentStack_->addWidget(mainPage);

            // 设置面板（内嵌）
            settingsPanel_ = new SettingsDialog(contentStack_);
            settingsPanel_->setEmbedded(true);
            contentStack_->addWidget(settingsPanel_);

            layout->addWidget(contentStack_);
        }

        // ============================================================================
        // 底部栏
        // ============================================================================

        void MainWindow::setupStatusBar() {
            bottomBar_ = new QWidget(this);
            bottomBar_->setFixedHeight(72);
            bottomBar_->setStyleSheet(
                "background-color: #141820;"
                "border-top: 1px solid #2A3240;");

            QHBoxLayout* layout = new QHBoxLayout(bottomBar_);
            layout->setContentsMargins(16, 8, 16, 8);
            layout->setSpacing(16);

            // 状态指示
            auto makeIndicator = [this](const QString& label) -> QLabel* {
                QLabel* lbl = new QLabel(label, this);
                lbl->setStyleSheet(
                    "color: #9AA4B4;"
                    "font-size: 11px;");
                return lbl;
            };
            // 状态
            stateLabel_ = new QLabel("就绪", this);
            stateLabel_->setStyleSheet(
                "color: #4AD98A;"
                "font-size: 12px;"
                "font-weight: bold;");
            layout->addWidget(stateLabel_);

            layout->addSpacing(16);

            // 帧率
            sourceFpsLabel_ = makeIndicator("源: -- FPS");
            layout->addWidget(sourceFpsLabel_);

            QLabel* arrow = new QLabel("→", this);
            arrow->setStyleSheet("color: #4A9EFF; font-size: 14px;");
            layout->addWidget(arrow);

            outputFpsLabel_ = makeIndicator("输出: -- FPS");
            layout->addWidget(outputFpsLabel_);

            layout->addSpacing(16);

            // 延迟
            latencyLabel_ = makeIndicator("延迟: -- ms");
            layout->addWidget(latencyLabel_);

            layout->addSpacing(16);

            // 画质
            qualityLabel_ = makeIndicator("画质: --");
            layout->addWidget(qualityLabel_);

            layout->addStretch();

            // GPU 信息
            gpuLabel_ = makeIndicator("GPU: --");
            layout->addWidget(gpuLabel_);

            layout->addSpacing(16);

            // 控制按钮
            startBtn_ = new QPushButton("▶ 启动", this);
            startBtn_->setFixedSize(100, 40);
            startBtn_->setCursor(Qt::PointingHandCursor);
            startBtn_->setStyleSheet(
                "QPushButton {"
                "  background-color: #4A9EFF;"
                "  color: white;"
                "  border: none;"
                "  border-radius: 8px;"
                "  font-size: 13px;"
                "  font-weight: bold;"
                "}"
                "QPushButton:hover {"
                "  background-color: #5AAEFF;"
                "}"
                "QPushButton:pressed {"
                "  background-color: #3A8EEF;"
                "}"
                "QPushButton:disabled {"
                "  background-color: #2A3240;"
                "  color: #5A6270;"
                "}");
            layout->addWidget(startBtn_);

            pauseBtn_ = new QPushButton("⏸ 暂停", this);
            pauseBtn_->setFixedSize(80, 40);
            pauseBtn_->setCursor(Qt::PointingHandCursor);
            pauseBtn_->setStyleSheet(
                "QPushButton {"
                "  background-color: #2A3240;"
                "  color: #E8ECF2;"
                "  border: none;"
                "  border-radius: 8px;"
                "  font-size: 13px;"
                "}"
                "QPushButton:hover {"
                "  background-color: #3A4250;"
                "}"
                "QPushButton:disabled {"
                "  color: #5A6270;"
                "}");
            pauseBtn_->setEnabled(false);
            layout->addWidget(pauseBtn_);

            stopBtn_ = new QPushButton("■ 停止", this);
            stopBtn_->setFixedSize(80, 40);
            stopBtn_->setCursor(Qt::PointingHandCursor);
            stopBtn_->setStyleSheet(
                "QPushButton {"
                "  background-color: #2A3240;"
                "  color: #E8ECF2;"
                "  border: none;"
                "  border-radius: 8px;"
                "  font-size: 13px;"
                "}"
                "QPushButton:hover {"
                "  background-color: #FF5A5A;"
                "  color: white;"
                "}"
                "QPushButton:disabled {"
                "  color: #5A6270;"
                "}");
            stopBtn_->setEnabled(false);
            layout->addWidget(stopBtn_);
        }

        // ============================================================================
        // 连接
        // ============================================================================

        void MainWindow::setupConnections() {

            // 设置面板（内嵌）返回时切回主界面并应用设置
            if (settingsPanel_) {
                connect(settingsPanel_, &SettingsDialog::backRequested,
                    this, &MainWindow::onSettingsBack);
                connect(settingsPanel_, &SettingsDialog::checkUpdatesRequested,
                    this, &MainWindow::checkForUpdates);
            }

            connect(settingsBtn_, &QPushButton::clicked,
                this, &MainWindow::onSettingsClicked);
            connect(minimizeBtn_, &QPushButton::clicked,
                this, &MainWindow::onMinimizeClicked);
            connect(closeBtn_, &QPushButton::clicked,
                this, &MainWindow::onCloseClicked);
            connect(maximizeBtn_, &QPushButton::clicked, this, [this]() {
                if (isMaximized()) showNormal();
                else showMaximized();
            });

            connect(startBtn_, &QPushButton::clicked,
                this, &MainWindow::onStartClicked);
            connect(pauseBtn_, &QPushButton::clicked,
                this, &MainWindow::onPauseClicked);
            connect(stopBtn_, &QPushButton::clicked,
                this, &MainWindow::onStopClicked);

            if (gameList_) {
                connect(gameList_, &GameListWidget::gameSelected,
                    this, &MainWindow::onGameSelected);
            }

            if (rippleWidget_) {
                connect(rippleWidget_, &RippleWidget::rippleTriggered,
                    this, &MainWindow::onRippleTriggered);
            }

            // 托盘信号
            if (trayIcon_) {
                connect(trayIcon_, &TrayIcon::showMainWindowRequested,
                    this, &MainWindow::showAndActivate);
                connect(trayIcon_, &TrayIcon::toggleFrameGenRequested,
                    this, &MainWindow::onTrayToggleFrameGen);
                connect(trayIcon_, &TrayIcon::stopFrameGenRequested,
                    this, &MainWindow::onTrayStop);
                connect(trayIcon_, &TrayIcon::exitRequested,
                    this, &MainWindow::onTrayExit);
            }
        }

        // ============================================================================
        // 主题
        // ============================================================================

        void MainWindow::applyTheme() {
            const auto& theme = Theme::instance();

            // 标题栏半透明（跟随面板透明度）
            if (titleBar_) {
                QColor tbBg(0x14, 0x18, 0x20, theme.panelOpacity() * 255 / 100);
                titleBar_->setStyleSheet(QString(
                    "#titleBar {"
                    "  background-color: %1;"
                    "  border-bottom: 1px solid rgba(42, 50, 64, 180);"
                    "}").arg(tbBg.name(QColor::HexArgb)));
            }

            // 图表区随主题刷新
            if (perfGraph_) {
                perfGraph_->applyTheme();
            }

            setStyleSheet(theme.globalStyleSheet());

            if (gameList_) {
                gameList_->applyTheme();
            }
        }

        // ============================================================================
        // 绑定管线
        // ============================================================================

        void MainWindow::attachPipeline(FrameGenPipeline* pipeline) {
            pipeline_ = pipeline;

            if (pipeline_) {
                // 状态回调：同步到 UI 状态标签
                pipeline_->setStateChangeCallback(
                    [this](PipelineState /*oldState*/, PipelineState newState) {
                        QString text;
                        switch (newState) {
                        case PipelineState::Uninitialized: text = "未初始化"; break;
                        case PipelineState::Initializing:  text = "初始化中"; break;
                        case PipelineState::Ready:         text = "就绪"; break;
                        case PipelineState::Running:       text = "运行中"; break;
                        case PipelineState::Paused:        text = "已暂停"; break;
                        case PipelineState::Stopping:      text = "停止中"; break;
                        // 错误状态由错误回调显示具体信息，这里直接返回不覆盖
                        case PipelineState::Error:
                            return;

                        QMetaObject::invokeMethod(this, [this, text, newState]() {
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
                        }, Qt::QueuedConnection);
                    }
                });

                // 错误回调：在 UI 上显示具体错误信息
                pipeline_->setErrorCallback(
                    [this](const Error& error) {
                        QString msg = QString::fromStdString(error.message);
                        QMetaObject::invokeMethod(this, [this, msg]() {
                            if (stateLabel_) {
                                stateLabel_->setText("错误: " + msg);
                                stateLabel_->setStyleSheet(
                                    "color: #FF6B6B; font-size: 12px; font-weight: bold;");
                            }
                        }, Qt::QueuedConnection);
                    }
                );

                if (controlPanel_) {
                    controlPanel_->attachPipeline(pipeline_);

                    // 面板设置联动到管线
                    connect(controlPanel_, &ControlPanel::multiplierChanged,
                        this, [this](uint32_t m) {
                        if (pipeline_) pipeline_->setMultiplier(m);
                    });
                    connect(controlPanel_, &ControlPanel::qualityChanged,
                        this, [this](uint32_t level) {
                        if (pipeline_) pipeline_->setQualityLevel(static_cast<QualityLevel>(level));
                    });
                    connect(controlPanel_, &ControlPanel::settingsChanged,
                        this, [this]() {
                        if (pipeline_) pipeline_->setSceneDetectionEnabled(true);
                    });
                    connect(controlPanel_, &ControlPanel::frameGenToggled,
                        this, [this](bool enabled) {
                        if (pipeline_) pipeline_->setFrameGenEnabled(enabled);
                    });
                    connect(controlPanel_, &ControlPanel::superResChanged,
                        this, [this](bool enabled, uint32_t scale) {
                        if (pipeline_) pipeline_->setSuperResolution(enabled, scale);
                    });

                    // 从已保存设置恢复开关状态（与配置一致）
                    {
                        QSettings s;
                        controlPanel_->restoreFrameGenEnabled(
                            s.value("frame_gen/enabled", true).toBool());
                        controlPanel_->restoreSuperResolution(
                            s.value("superres/enabled", false).toBool(),
                            static_cast<uint32_t>(s.value("superres/scale", 0).toInt() + 2));
                    }
                }

                if (perfGraph_) {
                    perfGraph_->attachPipeline(pipeline_);
                }
            }
        }

        // ============================================================================
        // 槽函数
        // ============================================================================

        void MainWindow::onStartClicked() {
            if (!pipeline_) {
                LOG_WARN("No pipeline attached");
                return;
            }
            if (!selectedGameHwnd_) {
                LOG_WARN("No game selected");
                return;
            }

            // 首次启动：初始化管线（读取用户设置，替代硬编码）
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
                // 帧生成/超分开关以主界面状态为准（用户实时操作的首选入口，
                // 避免启动时被已存设置的旧值覆盖导致开关不生效）
                cfg.frameGenSettings.enabled =
                    controlPanel_ ? controlPanel_->isFrameGenEnabled()
                    : settings.value("frame_gen/enabled", true).toBool();

                // 超分辨率（ED-ASR）：独立开关 + 倍率
                if (controlPanel_) {
                    cfg.presentConfig.superResEnabled = controlPanel_->isSuperResEnabled();
                    cfg.presentConfig.superResScale = controlPanel_->superResScale();
                } else {
                    cfg.presentConfig.superResEnabled =
                        settings.value("superres/enabled", false).toBool();
                    cfg.presentConfig.superResScale = static_cast<uint32_t>(
                        settings.value("superres/scale", 0).toInt() + 2);
                }

                // 捕获后端（0 自动 / 1 WGC / 2 DXGI）
                int backend = settings.value("capture/backend", 0).toInt();
                cfg.captureConfig.preferredBackend = (backend == 2)
                    ? CaptureBackend::DXGI_DD
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
            }

            if (!pipeline_->start()) {
                LOG_ERROR("Pipeline start failed");
            }
        }
        void MainWindow::onPauseClicked() {
            if (!pipeline_) {
                LOG_WARN("No pipeline attached");
                return;
            }
            pipeline_->pause();
        }

        void MainWindow::onStopClicked() {
            if (!pipeline_) {
                LOG_WARN("No pipeline attached");
                return;
            }
            pipeline_->stop();
        }

        void MainWindow::onSettingsClicked() {
            // 内嵌跳转：点击设置直接切换到设置面板，不弹独立窗口
            if (!contentStack_ || !settingsPanel_) return;
            settingsPanel_->reloadSettings();
            contentStack_->setCurrentWidget(settingsPanel_);
        }

        void MainWindow::onSettingsBack() {
            // 从设置面板返回主界面，并应用已保存的设置
            if (contentStack_ && contentStack_->count() > 0) {
                contentStack_->setCurrentIndex(0);
            }
            reloadWallpaper();

            // 从 QSettings 重新读入透明度并应用到主题单例
            Theme& theme = Theme::instance();
            QSettings settings;
            theme.setPanelOpacity(settings.value("ui/panel_opacity", 85).toInt());
            applyTheme();
        }

        void MainWindow::onMinimizeClicked() {
            showMinimized();
        }

        void MainWindow::onCloseClicked() {
            close();
        }

        void MainWindow::onGameSelected(uint64_t hash, const QString& name, void* hwnd) {
            selectedGameHash_ = hash;
            selectedGameName_ = name;
            selectedGameHwnd_ = hwnd;

            if (controlPanel_) {
                controlPanel_->setTargetGame(name, hwnd);
            }
        }

        void MainWindow::onPipelineStateChanged(int /*oldState*/, int /*newState*/) {
            // 状态文本已由 attachPipeline 内的回调维护
        }

        void MainWindow::onPipelineError(const QString& /*message*/) {
            // 错误文本已由 attachPipeline 内的回调维护
        }

        void MainWindow::onUpdateTimer() {
            // 壁纸动画需要主窗口重绘（壁纸渲染在背景层）
            if (wallpaperEngine_ || rippleWidget_) {
                update();
            }
            updateFpsDisplay();
            updateLatencyDisplay();
            updateStatusIndicators();
        }

        void MainWindow::updateFpsDisplay() {
            if (!pipeline_) return;

            auto stats = pipeline_->stats();

            if (sourceFpsLabel_) {
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
            }
        }

        void MainWindow::updateLatencyDisplay() {
            if (!pipeline_) return;

            auto stats = pipeline_->stats();

            if (latencyLabel_) {
                latencyLabel_->setText(QString("延迟: %1 ms")
                    .arg(stats.avgTotalMs, 0, 'f', 1));
            }
            if (qualityLabel_) {
                qualityLabel_->setText(QString("画质: %1")
                    .arg(static_cast<int>(stats.currentQuality)));
            }
        }

        void MainWindow::updateStatusIndicators() {
            if (!pipeline_) return;

            auto stats = pipeline_->stats();

            if (gpuLabel_) {
                gpuLabel_->setText(QString("GPU: %1% / %2\u00b0C")
                    .arg(static_cast<int>(stats.gpuUtilization))
                    .arg(static_cast<int>(stats.gpuTempC)));
            }
        }

        void MainWindow::onRippleTriggered(const QPoint& position) {
            // 水波由主窗口背景层统一绘制
            Q_UNUSED(position);
            update();
        }

        void MainWindow::onTrayToggleFrameGen() {
            if (!pipeline_) return;

            if (pipeline_->state() == PipelineState::Running) {
                pipeline_->pause();
            } else {
                pipeline_->start();
            }
        }

        void MainWindow::onTrayStop() {
            if (!pipeline_) return;
            pipeline_->stop();
        }
        void MainWindow::onTrayExit() {
            quitting_ = true;
            close();
        }

        void MainWindow::showAndActivate() {
            show();
            raise();
            activateWindow();
        }

        // ============================================================================
        // 壁纸
        // ============================================================================

        void MainWindow::reloadWallpaper() {
            if (!wallpaperEngine_) return;

            QSettings settings;
            const QString wp = settings.value(
                "ui/wallpaper_path", QString()).toString();

            if (wp.isEmpty()) {
                wallpaperEngine_->useProcedural();
                return;
            }

            const QFileInfo info(wp);
            const bool isMovie = info.suffix().compare("gif", Qt::CaseInsensitive) == 0;

            bool ok = false;
            if (isMovie) {
                ok = wallpaperEngine_->loadMovie(wp);
            } else {
                ok = wallpaperEngine_->loadImage(wp);
            }

            if (!ok) {
                wallpaperEngine_->useProcedural();
            }
        }

        // ============================================================================
        // 绘制与事件
        // ============================================================================

        void MainWindow::paintEvent(QPaintEvent* event) {
            QPainter p(this);
            p.setRenderHint(QPainter::Antialiasing);

            // 强制全窗口绘制：ReplaceClip 替换 region 裁剪，壁纸层不受子控件遮挡优化影响
            p.setClipRect(rect(), Qt::ReplaceClip);

            // 整体背景
            p.fillRect(rect(), QColor(0x1E, 0x26, 0x30));

            // 设置面板打开时不画动态壁纸：内容区是独立绘制层，透出 GIF 会形成红色观感。
            // 设置面板自身各层已强制不透明深色，此时画深色底即可彻底隔绝壁纸。
            const bool settingsOpen = contentStack_ && contentStack_->currentIndex() == 1;
            if (!settingsOpen) {
                // 动态壁纸背景层
                if (wallpaperEngine_) {
                    wallpaperEngine_->render(p, rect());
                }

                // 水波层（叠加在壁纸上，坐标转换到窗口坐标系）
                if (rippleWidget_) {
                    QPointF rippleOffset = rippleWidget_->mapTo(this, QPoint(0, 0));
                    rippleWidget_->render(p, rippleOffset);
                }
            }
        }

        void MainWindow::mousePressEvent(QMouseEvent* event) {
            if (event->button() == Qt::LeftButton) {
                QPoint pos = event->pos();

                // 标题栏拖动
                if (titleBar_ && titleBar_->geometry().contains(pos)) {
                    dragging_ = true;
                    dragStartPos_ = pos;
                    event->accept();
                    return;
                }
            }

            QMainWindow::mousePressEvent(event);
        }

        void MainWindow::mouseMoveEvent(QMouseEvent* event) {
            if (dragging_) {
                QPoint delta = event->globalPosition().toPoint() - dragStartPos_;
                move(delta);
                event->accept();
                return;
            }

            QMainWindow::mouseMoveEvent(event);
        }

        void MainWindow::mouseReleaseEvent(QMouseEvent* event) {
            if (dragging_) {
                dragging_ = false;
                event->accept();
                return;
            }

            QMainWindow::mouseReleaseEvent(event);
        }

        bool MainWindow::eventFilter(QObject* obj, QEvent* event) {
            if (obj == centerArea_ && event->type() == QEvent::Resize) {
                // 同步壁纸与水波层几何
                if (wallpaperEngine_) {
                    wallpaperEngine_->setGeometry(0, 0, centerArea_->width(),
                        centerArea_->height());
                }
                if (rippleWidget_) {
                    rippleWidget_->setGeometry(0, 0, centerArea_->width(),
                        centerArea_->height());
                }
                return false;
            }

            return QMainWindow::eventFilter(obj, event);
        }

        void MainWindow::closeEvent(QCloseEvent* event) {
            if (quitting_) {
                event->accept();
                return;
            }

            QSettings settings;
            const bool minimizeToTray = settings.value(
                "general/minimize_to_tray", true).toBool();

            if (minimizeToTray) {
                // 最小化到托盘而不是退出
                hide();
                event->ignore();
                return;
            }

            event->accept();
        }

        void MainWindow::changeEvent(QEvent* event) {
            if (event->type() == QEvent::WindowStateChange) {
                if (isMinimized()) {
                    QSettings settings;
                    const bool minimizeToTray = settings.value(
                        "general/minimize_to_tray", true).toBool();

                    if (minimizeToTray) {
                        QTimer::singleShot(0, this, [this]() {
                            hide();
                        });
                    }
                }
            }

            QMainWindow::changeEvent(event);
        }

        // ============================================================================
        // 更新检查（启动自动 + 设置面板手动共用）
        // ============================================================================

        void MainWindow::checkForUpdates() {
            auto* updater = new Lingjing::Core::UpdateChecker(this);
            connect(updater,
                &Lingjing::Core::UpdateChecker::updateAvailable,
                this,
                [this](const QString& version, const QString& url,
                    const QString& notes) {
                    // 自定义暗色更新对话框
                    QDialog dlg(this);
                    dlg.setWindowTitle(QStringLiteral("灵境 - 发现新版本"));
                    dlg.setModal(true);
                    dlg.setFixedWidth(420);
                    dlg.setStyleSheet(
                        "QDialog { background-color: #141920; }"
                        "QLabel { background: transparent; }"
                        "QPushButton { background-color: #1A2029; color: #E8ECF2;"
                        "  border: 1px solid #2A3240; border-radius: 6px;"
                        "  padding: 8px 22px; font-size: 12px; }"
                        "QPushButton:hover { border-color: #4A9EFF; }"
                        "QPushButton#dl { background-color: #4A9EFF; color: white;"
                        "  border: none; }"
                        "QPushButton#dl:hover { background-color: #5AAEFF; }");

                    auto* lay = new QVBoxLayout(&dlg);
                    lay->setContentsMargins(20, 16, 20, 16);
                    lay->setSpacing(10);

                    auto* title = new QLabel(
                        QStringLiteral("发现新版本 %1").arg(version), &dlg);
                    title->setStyleSheet(
                        "font-size: 15px; font-weight: bold; color: #E8ECF2;");
                    lay->addWidget(title);

                    if (!notes.isEmpty()) {
                        auto* body = new QLabel(notes, &dlg);
                        body->setWordWrap(true);
                        body->setStyleSheet("font-size: 12px; color: #9AA4B4;");
                        lay->addWidget(body);
                    }

                    auto* btns = new QHBoxLayout();
                    btns->addStretch();
                    auto* laterBtn = new QPushButton(QStringLiteral("以后再说"), &dlg);
                    auto* dlBtn = new QPushButton(QStringLiteral("下载更新"), &dlg);
                    dlBtn->setObjectName("dl");
                    if (url.isEmpty()) dlBtn->setEnabled(false);
                    connect(dlBtn, &QPushButton::clicked, &dlg,
                        [this, &dlg, url]() {
                            dlg.accept();
                            if (url.isEmpty()) return;
                            LOG_INFO("开始下载更新: %s", url.toUtf8().constData());
                            // 安装包统一经 GitHub 仓库树/Blob API 下载（国内可直连），
                            // 不在浏览器中打开网页。
                            const QString apiBase =
                                QStringLiteral("https://api.github.com/repos/shengtsxk/-");
                            const QString fileName =
                                url.contains('/') ? url.mid(url.lastIndexOf('/') + 1)
                                                  : url;
                            auto* nam = new QNetworkAccessManager(this);
                            QNetworkRequest treq{
                                QUrl(apiBase + QStringLiteral("/git/trees/main?recursive=1")) };
                            treq.setTransferTimeout(15000);
                            treq.setRawHeader("User-Agent",
                                "Lingjing/" LINGJING_VERSION_STRING);
                            auto* treply = nam->get(treq);
                            connect(treply, &QNetworkReply::finished,
                                this, [this, nam, treply, fileName, apiBase]() {
                                    treply->deleteLater();
                                    nam->deleteLater();
                                    if (treply->error() != QNetworkReply::NoError) {
                                        LOG_WARN("更新文件查询失败: %s",
                                            treply->errorString().toUtf8().constData());
                                        return;
                                    }
                                    const QByteArray treeData = treply->readAll();
                                    QJsonParseError perr;
                                    QJsonDocument tdoc =
                                        QJsonDocument::fromJson(treeData, &perr);
                                    if (perr.error != QJsonParseError::NoError ||
                                        !tdoc.isObject()) {
                                        LOG_WARN("更新文件清单解析失败");
                                        return;
                                    }
                                    QString blobSha;
                                    const QJsonArray entries =
                                        tdoc.object().value("tree").toArray();
                                    for (const auto& v : entries) {
                                        const QJsonObject o = v.toObject();
                                        if (o.value("path").toString() == fileName) {
                                            blobSha = o.value("sha").toString();
                                            break;
                                        }
                                    }
                                    if (blobSha.isEmpty()) {
                                        LOG_WARN("更新文件未找到: %s",
                                            fileName.toUtf8().constData());
                                        return;
                                    }
                                    // 下载 Blob 原始字节
                                    auto* nam2 = new QNetworkAccessManager(this);
                                    QNetworkRequest breq{ QUrl(apiBase
                                        + QStringLiteral("/git/blobs/") + blobSha) };
                                    breq.setTransferTimeout(0); // 大文件不限时
                                    breq.setRawHeader("User-Agent",
                                        "Lingjing/" LINGJING_VERSION_STRING);
                                    breq.setRawHeader("Accept",
                                        "application/vnd.github.raw");
                                    auto* breply = nam2->get(breq);
                                    connect(breply, &QNetworkReply::finished,
                                        this, [breply, nam2, this]() {
                                            breply->deleteLater();
                                            nam2->deleteLater();
                                            if (breply->error() != QNetworkReply::NoError) {
                                                LOG_WARN("更新下载失败: %s",
                                                    breply->errorString().toUtf8().constData());
                                                return;
                                            }
                                            const QByteArray data = breply->readAll();
                                            const QString dir = QStandardPaths::writableLocation(
                                                QStandardPaths::DownloadLocation);
                                            QDir().mkpath(dir);
                                            const QString path =
                                                dir + QStringLiteral("/灵境Lingjing安装程序.exe");
                                            QFile f(path);
                                            if (f.open(QIODevice::WriteOnly)) {
                                                f.write(data);
                                                f.close();
                                                LOG_INFO("更新已下载: %s (%lld 字节)",
                                                    path.toUtf8().constData(),
                                                    static_cast<long long>(data.size()));
                                                QProcess::startDetached(path, QStringList());
                                            } else {
                                                LOG_WARN("更新保存失败: %s",
                                                    path.toUtf8().constData());
                                            }
                                        });
                                });
                        });
                    connect(laterBtn, &QPushButton::clicked, &dlg,
                        &QDialog::reject);
                    btns->addWidget(laterBtn);
                    btns->addWidget(dlBtn);
                    lay->addLayout(btns);

                    dlg.exec();
                });
            connect(updater,
                &Lingjing::Core::UpdateChecker::upToDate,
                this,
                [this](const QString& latest) {
                    // 已是最新版本：动态弹出提示
                    QDialog dlg(this);
                    dlg.setWindowTitle(QStringLiteral("灵境 - 检查更新"));
                    dlg.setModal(true);
                    dlg.setFixedWidth(340);
                    dlg.setStyleSheet(
                        "QDialog { background-color: #141920; }"
                        "QLabel { background: transparent; }"
                        "QPushButton { background-color: #1A2029; color: #E8ECF2;"
                        "  border: 1px solid #2A3240; border-radius: 6px;"
                        "  padding: 8px 22px; font-size: 12px; }"
                        "QPushButton:hover { border-color: #4A9EFF; }"
                        "QPushButton#ok { background-color: #4A9EFF; color: white;"
                        "  border: none; }"
                        "QPushButton#ok:hover { background-color: #5AAEFF; }");

                    auto* lay = new QVBoxLayout(&dlg);
                    lay->setContentsMargins(20, 16, 20, 16);
                    lay->setSpacing(10);

                    auto* icon = new QLabel(QStringLiteral("✓"), &dlg);
                    icon->setAlignment(Qt::AlignCenter);
                    icon->setStyleSheet(
                        "font-size: 26px; color: #34C77B; background: transparent;");

                    auto* title = new QLabel(
                        QStringLiteral("当前已是最新版本"), &dlg);
                    title->setAlignment(Qt::AlignCenter);
                    title->setStyleSheet(
                        "font-size: 14px; font-weight: bold; color: #E8ECF2;");

                    auto* sub = new QLabel(
                        QStringLiteral("灵境 Lingjing v%1").arg(latest), &dlg);
                    sub->setAlignment(Qt::AlignCenter);
                    sub->setStyleSheet("font-size: 12px; color: #AEB8C8;");

                    lay->addWidget(icon);
                    lay->addWidget(title);
                    lay->addWidget(sub);

                    auto* btns = new QHBoxLayout();
                    btns->addStretch();
                    auto* okBtn = new QPushButton(QStringLiteral("好的"), &dlg);
                    okBtn->setObjectName("ok");
                    connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
                    btns->addWidget(okBtn);
                    btns->addStretch();
                    lay->addLayout(btns);

                    dlg.exec();
                });
            updater->checkForUpdates();
        }

    } // namespace UI
} // namespace Lingjing
