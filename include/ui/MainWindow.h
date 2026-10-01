#pragma once

#include "core/Types.h"
#include "ui/Theme.h"

#include <QMainWindow>
#include <QSystemTrayIcon>
#include <memory>

class QStackedWidget;
class QLabel;
class QPushButton;
class QVBoxLayout;
class QHBoxLayout;
class QTimer;

namespace Lingjing {

    class FrameGenPipeline;
    struct PipelineConfig;

    namespace UI {

        class ControlPanel;
        class GameListWidget;
        class PerformanceGraphWidget;
        class RippleWidget;
        class WallpaperEngine;
        class TrayIcon;
        class SettingsDialog;

        class MainWindow : public QMainWindow {
            Q_OBJECT

        public:
            explicit MainWindow(QWidget* parent = nullptr);
            ~MainWindow() override;

            // 绑定管线
            void attachPipeline(FrameGenPipeline* pipeline);

            // 显示/隐藏
            void showAndActivate();

            // 手动检查更新（弹更新对话框）
            void checkForUpdates();

        protected:
            void closeEvent(QCloseEvent* event) override;
            void changeEvent(QEvent* event) override;
            void paintEvent(QPaintEvent* event) override;
            void mousePressEvent(QMouseEvent* event) override;
            void mouseMoveEvent(QMouseEvent* event) override;
            void mouseReleaseEvent(QMouseEvent* event) override;
            bool eventFilter(QObject* obj, QEvent* event) override;

        private slots:
            void onStartClicked();
            void onStopClicked();
            void onPauseClicked();
            void onSettingsClicked();
            void onSettingsBack();
            void onMinimizeClicked();
            void onCloseClicked();
            void onGameSelected(uint64_t hash, const QString& name, void* hwnd);
            void onPipelineStateChanged(int oldState, int newState);
            void onPipelineError(const QString& message);
            void onUpdateTimer();
            void onRippleTriggered(const QPoint& position);
            void onTrayToggleFrameGen();
            void onTrayStop();
            void onTrayExit();

        private:
            void setupUi();
            void setupTitleBar();
            void setupContentArea();
            void setupStatusBar();
            void setupConnections();
            void applyTheme();

            void updateStatusIndicators();
            void reloadWallpaper();
            void updateFpsDisplay();
            void updateLatencyDisplay();

            // 主组件
            QWidget* titleBar_ = nullptr;
            QWidget* contentArea_ = nullptr;
            QWidget* bottomBar_ = nullptr;

            // 标题栏
            QLabel* appIcon_ = nullptr;
            QLabel* appTitle_ = nullptr;
            QPushButton* minimizeBtn_ = nullptr;
            QPushButton* maximizeBtn_ = nullptr;
            QPushButton* closeBtn_ = nullptr;
            QPushButton* settingsBtn_ = nullptr;

            // 内容
            RippleWidget* rippleWidget_ = nullptr;
            WallpaperEngine* wallpaperEngine_ = nullptr;
            QWidget* centerArea_ = nullptr;
            QStackedWidget* contentStack_ = nullptr;
            SettingsDialog* settingsPanel_ = nullptr;
            GameListWidget* gameList_ = nullptr;
            ControlPanel* controlPanel_ = nullptr;
            PerformanceGraphWidget* perfGraph_ = nullptr;

            // 底部
            QLabel* stateLabel_ = nullptr;
            QLabel* sourceFpsLabel_ = nullptr;
            QLabel* outputFpsLabel_ = nullptr;
            QLabel* latencyLabel_ = nullptr;
            QLabel* qualityLabel_ = nullptr;
            QLabel* gpuLabel_ = nullptr;
            QPushButton* startBtn_ = nullptr;
            QPushButton* pauseBtn_ = nullptr;
            QPushButton* stopBtn_ = nullptr;

            // 托盘
            TrayIcon* trayIcon_ = nullptr;

            // 管线
            FrameGenPipeline* pipeline_ = nullptr;

            // 更新定时器
            QTimer* updateTimer_ = nullptr;
            QTimer* selfHealTimer_ = nullptr;

            // 窗口拖动
            bool dragging_ = false;
            QPoint dragStartPos_;

            // 当前选中的游戏
            uint64_t selectedGameHash_ = 0;
            QString selectedGameName_;
            void* selectedGameHwnd_ = nullptr;

            // 真正退出标志（托盘退出时置位，避免被 closeEvent 拦截）
            bool quitting_ = false;
        };

    } // namespace UI
} // namespace Lingjing
