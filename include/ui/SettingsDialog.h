#pragma once

#include <QDialog>
#include <memory>

class QTabWidget;
class QComboBox;
class QCheckBox;
class QSlider;
class QSpinBox;
class QLineEdit;
class QPushButton;
class QLabel;

namespace Lingjing {

    class FrameGenPipeline;

    namespace UI {

        class SettingsDialog : public QWidget {
            Q_OBJECT

        public:
            explicit SettingsDialog(QWidget* parent = nullptr);
            ~SettingsDialog() override;

            void setPipelineConfig(FrameGenPipeline* pipeline);

            // 内嵌模式：作为主窗口内的设置面板（不弹独立窗口）
            void setEmbedded(bool embedded);
            // 重新读取设置并刷新控件
            void reloadSettings();

        signals:
            // 内嵌模式点“保存并返回”时发出，由宿主切回主界面
            void backRequested();
            // 用户点击“立即检查更新”
            void checkUpdatesRequested();

        private slots:
            void onAccepted();
            void onRejected();
            void onBrowseModel();
            void onBrowseWallpaper();
            void onClearLearningData();
            void onCheckUpdates();
            void onExportModel();
            void onImportModel();
            void onDownloadCommunityModels();

        protected:
            void paintEvent(QPaintEvent* event) override;

        private:
            void setupUi();
            QWidget* createGeneralTab();
            QWidget* createGraphicsTab();
            QWidget* createPerformanceTab();
            QWidget* createLearningTab();
            QWidget* createAboutTab();

            void loadSettings();
            void saveSettings();

            QTabWidget* tabs_ = nullptr;

            // 通用
            QComboBox* themeCombo_ = nullptr;
            QComboBox* languageCombo_ = nullptr;
            QCheckBox* startWithWindowsCheck_ = nullptr;
            QCheckBox* minimizeToTrayCheck_ = nullptr;
            QCheckBox* checkUpdatesCheck_ = nullptr;
            QPushButton* checkUpdatesBtn_ = nullptr;

            // 图形
            QComboBox* gpuCombo_ = nullptr;
            QCheckBox* tensorCoresCheck_ = nullptr;
            QCheckBox* xmxCheck_ = nullptr;
            QCheckBox* hwFlowCheck_ = nullptr;
            QLabel* accelStatusLabel_ = nullptr;
            QComboBox* captureBackendCombo_ = nullptr;
            QCheckBox* captureCursorCheck_ = nullptr;

            // 性能
            QComboBox* latencyTargetCombo_ = nullptr;
            QSpinBox* maxFrameTimeSpin_ = nullptr;
            QCheckBox* adaptiveQualityCheck_ = nullptr;
            QSlider* temporalWeightSlider_ = nullptr;
            QLabel* temporalWeightLabel_ = nullptr;

            // AI
            QLineEdit* modelPathEdit_ = nullptr;
            QPushButton* browseModelBtn_ = nullptr;
            QComboBox* precisionCombo_ = nullptr;
            QCheckBox* aiRepairCheck_ = nullptr;

            // 通用超分辨率（ED-ASR）
            QCheckBox* superResCheck_ = nullptr;
            QComboBox* superResScaleCombo_ = nullptr;

            // 学习
            QCheckBox* learningEnabledCheck_ = nullptr;
            QCheckBox* crowdLearningCheck_ = nullptr;
            QPushButton* clearLearningBtn_ = nullptr;
            QLabel* learningStatsLabel_ = nullptr;
            QPushButton* exportModelBtn_ = nullptr;
            QPushButton* importModelBtn_ = nullptr;
            QPushButton* downloadModelBtn_ = nullptr;

            // UI
            QLineEdit* wallpaperPathEdit_ = nullptr;
            QPushButton* browseWallpaperBtn_ = nullptr;
            QCheckBox* rippleEnabledCheck_ = nullptr;
            QSlider* rippleStrengthSlider_ = nullptr;
            QSlider* panelOpacitySlider_ = nullptr;

            // 按钮
            QPushButton* okBtn_ = nullptr;
            QPushButton* cancelBtn_ = nullptr;

            FrameGenPipeline* pipeline_ = nullptr;
            bool embedded_ = false;
        };

    } // namespace UI
} // namespace Lingjing
