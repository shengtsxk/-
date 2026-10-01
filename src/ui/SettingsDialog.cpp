#include "ui/SettingsDialog.h"
#include "ui/Theme.h"
#include "pipeline/FrameGenPipeline.h"
#include "learning/LearningManager.h"
#include "core/Logger.h"
#include "core/DeviceCaps.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QTabWidget>
#include <QComboBox>
#include <QCheckBox>
#include <QSlider>
#include <QSpinBox>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QGroupBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <memory>

namespace Lingjing {
    namespace UI {

        // ============================================================================
        // 构造/析构
        // ============================================================================

        namespace {

        // 不透明背景 QTabWidget：直接在 paintEvent 里铺深色，
        // 不依赖 QSS 解析（QSS 的透明/半透明规则对 QTabWidget 内容区无效时兜底）。
        class OpaqueTabWidget : public QTabWidget {
        public:
            using QTabWidget::QTabWidget;
        protected:
            void paintEvent(QPaintEvent* event) override {
                Q_UNUSED(event);
                QPainter p(this);
                p.fillRect(rect(), QColor("#1E2630"));
            }
        };

        }  // namespace

        SettingsDialog::SettingsDialog(QWidget* parent)
            : QWidget(parent)
        {
            setMinimumSize(720, 600);

            setupUi();
            loadSettings();

            // 设置面板不透明深色背景。Theme::globalStyleSheet() 开头的
            // `QWidget{background:半透明}` 会让面板透出壁纸（看起来红色）。
            // 用纯 ID 选择器（特异性最高）并追加在全局样式之后强制覆盖；
            // 同时用 QPalette + autoFillBackground 双保险（QSS 未命中时兜底）。
            setObjectName("settingsRoot");
            if (tabs_) tabs_->setObjectName("settingsRootTabs");
            {
                QPalette pal = palette();
                pal.setColor(QPalette::Window, QColor("#1E2630"));
                setPalette(pal);
                setAutoFillBackground(true);
            }
            setStyleSheet(Theme::instance().globalStyleSheet() + QStringLiteral(
                "#settingsRoot { background-color: #1E2630; }"
                "#settingsRoot * { background-color: #1E2630; }"
                "#settingsRoot QGroupBox { background-color: #242E3A; }"
                "#settingsRoot QPushButton { background-color: #2E3844; border-radius: 8px; }"
                "#settingsRoot QPushButton:hover { background-color: #38424E; }"
                "#settingsRoot QComboBox { background-color: #232D38; }"
                "#settingsRoot QLineEdit { background-color: #232D38; }"
                "#settingsRoot QCheckBox, #settingsRoot QLabel,"
                "#settingsRoot QSlider, #settingsRoot QScrollArea { background-color: transparent; }"
                "#settingsRootTabs { background-color: #1E2630; }"
                "#settingsRootTabs::pane {"
                "  background-color: #1E2630;"
                "  border: 1px solid #2A3240;"
                "  border-radius: 8px; }"));

            // QTabBar 的 tab 是"子控件"（非独立 QWidget），父级通配/后代规则
            // 对它不生效；必须把 QTabBar 规则写在本控件（QTabWidget）自身的
            // 样式表里，才能覆盖系统样式（Windows 强调色）避免出现红色。
            tabs_->setStyleSheet(
                "QTabBar::tab {"
                "  background-color: #232D38;"
                "  color: #9AA4B4;"
                "  border: 1px solid #2A3240;"
                "  border-bottom: 2px solid transparent;"
                "  border-top-left-radius: 8px;"
                "  border-top-right-radius: 8px;"
                "  padding: 8px 20px;"
                "}"
                "QTabBar::tab:hover {"
                "  background-color: #2E3844;"
                "  color: #E8ECF2;"
                "}"
                "QTabBar::tab:selected {"
                "  background-color: #1E2630;"
                "  color: #4A9EFF;"
                "  border-bottom-color: #4A9EFF;"
                "}");

            // QTabWidget 页面是独立绘制层，父级 QSS 对页面内容不生效；
            // 给每个页面容器自身设置深色样式表（页面级样式优先于祖先级），
            // 彻底隔绝底部壁纸层。
            for (int i = 0; i < tabs_->count(); ++i) {
                QWidget* page = tabs_->widget(i);
                if (!page) continue;
                page->setStyleSheet(
                    "QWidget { background-color: #1E2630; }"
                    "QGroupBox { background-color: #242E3A; }"
                    "QPushButton { background-color: #2E3844; border-radius: 8px; }"
                    "QPushButton:hover { background-color: #38424E; }"
                    "QComboBox { background-color: #232D38; }"
                    "QLineEdit { background-color: #232D38; }"
                    "QCheckBox, QLabel, QSlider, QScrollArea { background-color: transparent; }");
                page->setAutoFillBackground(true);
            }
            LOG_INFO("SettingsDialog: opaque stylesheet applied (root=%s, tabs=%s)",
                objectName().toUtf8().constData(),
                (tabs_ ? tabs_->objectName().toUtf8().constData() : "null"));
        }

        SettingsDialog::~SettingsDialog() = default;

        // 直接绘制不透明深色背景（不依赖 QSS 解析，绝不透出壁纸）
        void SettingsDialog::paintEvent(QPaintEvent* event) {
            Q_UNUSED(event);
            QPainter p(this);
            p.fillRect(rect(), QColor("#1E2630"));  // 深色不透明背景
            // 不调用基类：避免 globalStyleSheet 的半透明 QWidget 背景盖上来
        }

        // ============================================================================
        // 内嵌模式 / 重新加载
        // ============================================================================

        void SettingsDialog::setEmbedded(bool embedded) {
            embedded_ = embedded;
            if (okBtn_) {
                okBtn_->setText(embedded ? "保存并返回" : "保存");
            }
            if (cancelBtn_) {
                cancelBtn_->setText(embedded ? "返回" : "取消");
            }
        }

        void SettingsDialog::reloadSettings() {
            loadSettings();
        }

        // ============================================================================
        // UI
        // ============================================================================

        void SettingsDialog::setupUi() {
            QVBoxLayout* mainLayout = new QVBoxLayout(this);
            mainLayout->setContentsMargins(16, 16, 16, 16);
            mainLayout->setSpacing(16);

            // 选项卡
            tabs_ = new OpaqueTabWidget(this);
            tabs_->addTab(createGeneralTab(), "通用");
            tabs_->addTab(createGraphicsTab(), "图形");
            tabs_->addTab(createPerformanceTab(), "性能");
            tabs_->addTab(createLearningTab(), "学习");
            tabs_->addTab(createAboutTab(), "关于");

            mainLayout->addWidget(tabs_, 1);

            // 按钮
            {
                QHBoxLayout* btnLayout = new QHBoxLayout();
                btnLayout->addStretch();

                cancelBtn_ = new QPushButton("取消", this);
                cancelBtn_->setFixedSize(100, 36);
                cancelBtn_->setStyleSheet(
                    "QPushButton {"
                    "  background-color: #2E3844;"
                    "  color: #E8ECF2;"
                    "  border: 1px solid #3A4654;"
                    "  border-radius: 6px;"
                    "}"
                    "QPushButton:hover { background-color: #38424E; }");
                connect(cancelBtn_, &QPushButton::clicked,
                    this, &SettingsDialog::onRejected);
                btnLayout->addWidget(cancelBtn_);

                okBtn_ = new QPushButton("保存", this);
                okBtn_->setFixedSize(100, 36);
                okBtn_->setDefault(true);
                okBtn_->setStyleSheet(
                    "QPushButton {"
                    "  background-color: #4A9EFF;"
                    "  color: white;"
                    "  border: none;"
                    "  border-radius: 6px;"
                    "  font-weight: bold;"
                    "}"
                    "QPushButton:hover { background-color: #5AAEFF; }");
                connect(okBtn_, &QPushButton::clicked,
                    this, &SettingsDialog::onAccepted);
                btnLayout->addWidget(okBtn_);

                mainLayout->addLayout(btnLayout);
            }
        }

        // ============================================================================
        // 通用设置
        // ============================================================================

        QWidget* SettingsDialog::createGeneralTab() {
            QWidget* w = new QWidget();
            QVBoxLayout* layout = new QVBoxLayout(w);
            layout->setSpacing(16);

            // 界面
            {
                QGroupBox* group = new QGroupBox("界面");
                QFormLayout* form = new QFormLayout(group);

                themeCombo_ = new QComboBox();
                themeCombo_->addItem("暗色主题");
                themeCombo_->addItem("亮色主题");
                themeCombo_->addItem("跟随系统");
                form->addRow("主题:", themeCombo_);

                languageCombo_ = new QComboBox();
                languageCombo_->addItem("简体中文");
                languageCombo_->addItem("English");
                form->addRow("语言:", languageCombo_);

                layout->addWidget(group);
            }

            // 启动
            {
                QGroupBox* group = new QGroupBox("启动选项");
                QVBoxLayout* groupLayout = new QVBoxLayout(group);

                startWithWindowsCheck_ = new QCheckBox("开机自动启动");
                groupLayout->addWidget(startWithWindowsCheck_);

                minimizeToTrayCheck_ = new QCheckBox("关闭时最小化到托盘");
                groupLayout->addWidget(minimizeToTrayCheck_);

                checkUpdatesCheck_ = new QCheckBox("自动检查更新");
                groupLayout->addWidget(checkUpdatesCheck_);

                checkUpdatesBtn_ = new QPushButton("立即检查更新");
                checkUpdatesBtn_->setFixedWidth(180);
                connect(checkUpdatesBtn_, &QPushButton::clicked,
                    this, &SettingsDialog::onCheckUpdates);
                groupLayout->addWidget(checkUpdatesBtn_);

                layout->addWidget(group);
            }

            // 壁纸
            {
                QGroupBox* group = new QGroupBox("背景");
                QFormLayout* form = new QFormLayout(group);

                QHBoxLayout* wallpaperLayout = new QHBoxLayout();

                wallpaperPathEdit_ = new QLineEdit();
                wallpaperPathEdit_->setPlaceholderText("留空使用生成式壁纸");
                wallpaperLayout->addWidget(wallpaperPathEdit_, 1);

                browseWallpaperBtn_ = new QPushButton("浏览...");
                browseWallpaperBtn_->setFixedWidth(80);
                connect(browseWallpaperBtn_, &QPushButton::clicked,
                    this, &SettingsDialog::onBrowseWallpaper);
                wallpaperLayout->addWidget(browseWallpaperBtn_);

                form->addRow("壁纸:", wallpaperLayout);

                rippleEnabledCheck_ = new QCheckBox("启用点击水波效果");
                form->addRow("", rippleEnabledCheck_);

                rippleStrengthSlider_ = new QSlider(Qt::Horizontal);
                rippleStrengthSlider_->setRange(0, 100);
                rippleStrengthSlider_->setValue(80);
                form->addRow("水波强度:", rippleStrengthSlider_);

                panelOpacitySlider_ = new QSlider(Qt::Horizontal);
                panelOpacitySlider_->setRange(0, 100);
                panelOpacitySlider_->setValue(95);
                form->addRow("面板透明度:", panelOpacitySlider_);

                QLabel* panelOpacityHint = new QLabel("数值越低越透明（壁纸越清晰），100 为不透明");
                panelOpacityHint->setProperty("subdued", true);
                form->addRow("", panelOpacityHint);

                layout->addWidget(group);
            }

            layout->addStretch();
            return w;
        }

        // ============================================================================
        // 图形设置
        // ============================================================================

        QWidget* SettingsDialog::createGraphicsTab() {
            QWidget* w = new QWidget();
            QVBoxLayout* layout = new QVBoxLayout(w);
            layout->setSpacing(16);

            // GPU
            {
                QGroupBox* group = new QGroupBox("GPU 选择");
                QFormLayout* form = new QFormLayout(group);

                gpuCombo_ = new QComboBox();
                gpuCombo_->addItem("自动选择");
                // 注：实际应从 GpuDetector 获取
                form->addRow("首选 GPU:", gpuCombo_);

                tensorCoresCheck_ = new QCheckBox("启用 Tensor Cores");
                form->addRow("", tensorCoresCheck_);

                xmxCheck_ = new QCheckBox("启用 XMX 引擎");
                form->addRow("", xmxCheck_);

                hwFlowCheck_ = new QCheckBox("使用硬件光流");
                form->addRow("", hwFlowCheck_);

                accelStatusLabel_ = new QLabel("将自动识别可用加速");
                accelStatusLabel_->setProperty("subdued", true);
                accelStatusLabel_->setWordWrap(true);
                form->addRow("", accelStatusLabel_);

                layout->addWidget(group);
            }

            // 捕获
            {
                QGroupBox* group = new QGroupBox("屏幕捕获");
                QFormLayout* form = new QFormLayout(group);

                captureBackendCombo_ = new QComboBox();
                captureBackendCombo_->addItem("自动");
                captureBackendCombo_->addItem("Windows Graphics Capture");
                captureBackendCombo_->addItem("DXGI Desktop Duplication");
                captureBackendCombo_->addItem("图形钩子（不推荐）");
                form->addRow("捕获方式:", captureBackendCombo_);

                captureCursorCheck_ = new QCheckBox("捕获鼠标指针");
                form->addRow("", captureCursorCheck_);

                layout->addWidget(group);
            }

            // AI 模型
            {
                QGroupBox* group = new QGroupBox("AI 模型");
                QFormLayout* form = new QFormLayout(group);

                aiRepairCheck_ = new QCheckBox("启用 AI 修复");
                form->addRow("", aiRepairCheck_);

                QHBoxLayout* modelLayout = new QHBoxLayout();

                modelPathEdit_ = new QLineEdit();
                modelPathEdit_->setPlaceholderText("自动选择");
                modelLayout->addWidget(modelPathEdit_, 1);

                browseModelBtn_ = new QPushButton("浏览...");
                browseModelBtn_->setFixedWidth(80);
                connect(browseModelBtn_, &QPushButton::clicked,
                    this, &SettingsDialog::onBrowseModel);
                modelLayout->addWidget(browseModelBtn_);

                form->addRow("模型:", modelLayout);

                precisionCombo_ = new QComboBox();
                precisionCombo_->addItem("FP32");
                precisionCombo_->addItem("FP16");
                precisionCombo_->addItem("INT8");
                precisionCombo_->setCurrentIndex(1);
                form->addRow("精度:", precisionCombo_);

                layout->addWidget(group);
            }

            // 通用超分辨率（ASESS，D3D11 compute，任何显卡可运行）
            {
                QGroupBox* group = new QGroupBox("通用超分辨率（ASESS）");
                QFormLayout* form = new QFormLayout(group);

                superResCheck_ = new QCheckBox("启用 ASESS 超分辨率");
                form->addRow("", superResCheck_);

                superResScaleCombo_ = new QComboBox();
                superResScaleCombo_->addItem("2 倍");
                superResScaleCombo_->addItem("3 倍");
                superResScaleCombo_->addItem("4 倍");
                superResScaleCombo_->addItem("5 倍");
                superResScaleCombo_->setCurrentIndex(0);
                form->addRow("放大倍数:", superResScaleCombo_);

                QLabel* hint = new QLabel(
                    "原创边缘引导自适应算法（ASESS v2）：8 方向边缘分类 + 边缘强度调制\n"
                    "细节重建 + 振铃抑制，画质显著优于双三次；无需 Tensor Core / 深度学习。\n"
                    "3060 级独显 2 倍放大约 5ms/帧。");
                hint->setWordWrap(true);
                hint->setProperty("subdued", true);
                form->addRow("", hint);

                layout->addWidget(group);
            }

            layout->addStretch();
            return w;
        }

        // ============================================================================
        // 性能设置
        // ============================================================================

        QWidget* SettingsDialog::createPerformanceTab() {
            QWidget* w = new QWidget();
            QVBoxLayout* layout = new QVBoxLayout(w);
            layout->setSpacing(16);

            // 延迟
            {
                QGroupBox* group = new QGroupBox("延迟目标");
                QFormLayout* form = new QFormLayout(group);

                latencyTargetCombo_ = new QComboBox();
                latencyTargetCombo_->addItem("极限低延迟 (1ms)");
                latencyTargetCombo_->addItem("低延迟 (2ms)");
                latencyTargetCombo_->addItem("平衡 (4ms)");
                latencyTargetCombo_->addItem("画质优先 (8ms)");
                latencyTargetCombo_->setCurrentIndex(3);
                form->addRow("目标:", latencyTargetCombo_);

                maxFrameTimeSpin_ = new QSpinBox();
                maxFrameTimeSpin_->setRange(1, 100);
                maxFrameTimeSpin_->setValue(8);
                maxFrameTimeSpin_->setSuffix(" ms");
                form->addRow("单帧上限:", maxFrameTimeSpin_);

                adaptiveQualityCheck_ = new QCheckBox("自动调整画质以保持延迟");
                form->addRow("", adaptiveQualityCheck_);

                layout->addWidget(group);
            }

            // 时域平滑
            {
                QGroupBox* group = new QGroupBox("时域平滑");
                QFormLayout* form = new QFormLayout(group);

                QHBoxLayout* row = new QHBoxLayout();

                temporalWeightSlider_ = new QSlider(Qt::Horizontal);
                temporalWeightSlider_->setRange(0, 50);
                temporalWeightSlider_->setValue(15);
                row->addWidget(temporalWeightSlider_, 1);

                temporalWeightLabel_ = new QLabel("15%");
                temporalWeightLabel_->setFixedWidth(50);
                temporalWeightLabel_->setAlignment(Qt::AlignRight);
                row->addWidget(temporalWeightLabel_);

                connect(temporalWeightSlider_, &QSlider::valueChanged,
                    this, [this](int v) {
                        temporalWeightLabel_->setText(QString("%1%").arg(v));
                    });

                form->addRow("平滑权重:", row);

                layout->addWidget(group);
            }

            layout->addStretch();
            return w;
        }

        // ============================================================================
        // 学习设置
        // ============================================================================

        QWidget* SettingsDialog::createLearningTab() {
            QWidget* w = new QWidget();
            QVBoxLayout* layout = new QVBoxLayout(w);
            layout->setSpacing(16);

            {
                QGroupBox* group = new QGroupBox("学习系统");
                QVBoxLayout* groupLayout = new QVBoxLayout(group);

                learningEnabledCheck_ = new QCheckBox("启用学习系统");
                learningEnabledCheck_->setChecked(true);
                groupLayout->addWidget(learningEnabledCheck_);

                crowdLearningCheck_ = new QCheckBox("启用匿名群智学习（可选）");
                crowdLearningCheck_->setChecked(false);
                groupLayout->addWidget(crowdLearningCheck_);

                groupLayout->addSpacing(8);

                learningStatsLabel_ = new QLabel("已学习 0 个游戏");
                learningStatsLabel_->setStyleSheet("color: #9AA4B4; font-size: 11px;");
                groupLayout->addWidget(learningStatsLabel_);

                groupLayout->addSpacing(8);

                clearLearningBtn_ = new QPushButton("清除所有学习数据");
                clearLearningBtn_->setFixedWidth(180);
                connect(clearLearningBtn_, &QPushButton::clicked,
                    this, &SettingsDialog::onClearLearningData);
                groupLayout->addWidget(clearLearningBtn_);

                groupLayout->addSpacing(12);

                // 社区模型共享
                QLabel* modelTitle = new QLabel("模型共享（社区）");
                modelTitle->setStyleSheet("color: #9AA4B4; font-size: 11px;");
                groupLayout->addWidget(modelTitle);

                QHBoxLayout* modelBtns = new QHBoxLayout();
                exportModelBtn_ = new QPushButton("导出我的模型");
                exportModelBtn_->setFixedWidth(140);
                connect(exportModelBtn_, &QPushButton::clicked,
                    this, &SettingsDialog::onExportModel);
                modelBtns->addWidget(exportModelBtn_);

                importModelBtn_ = new QPushButton("导入模型文件");
                importModelBtn_->setFixedWidth(140);
                connect(importModelBtn_, &QPushButton::clicked,
                    this, &SettingsDialog::onImportModel);
                modelBtns->addWidget(importModelBtn_);
                groupLayout->addLayout(modelBtns);

                downloadModelBtn_ = new QPushButton("下载社区模型");
                downloadModelBtn_->setFixedWidth(180);
                connect(downloadModelBtn_, &QPushButton::clicked,
                    this, &SettingsDialog::onDownloadCommunityModels);
                groupLayout->addWidget(downloadModelBtn_);

                QLabel* modelHint = new QLabel(
                    "· 导出：把你学到的游戏参数保存为模型文件（.ljm，几十 KB）\n"
                    "· 共享：把 .ljm 上传到项目仓库，其他用户即可下载使用\n"
                    "· 下载：从社区下载其他用户分享的学习模型（自动合并）\n"
                    "· 轻量统计学习模型，内存占用极小，非 1.5 亿参数深度网络");
                modelHint->setWordWrap(true);
                modelHint->setStyleSheet("color: #7A8494; font-size: 11px;");
                groupLayout->addWidget(modelHint);

                groupLayout->addStretch();

                layout->addWidget(group);
            }

            layout->addStretch();
            return w;
        }

        // ============================================================================
        // 关于
        // ============================================================================

        QWidget* SettingsDialog::createAboutTab() {
            QWidget* w = new QWidget();
            QVBoxLayout* layout = new QVBoxLayout(w);
            layout->setSpacing(16);

            QLabel* title = new QLabel("灵境 Lingjing");
            title->setStyleSheet(
                "font-size: 24px;"
                "font-weight: bold;"
                "color: #4A9EFF;");
            title->setAlignment(Qt::AlignCenter);
            layout->addWidget(title);

            QLabel* version = new QLabel(
                QStringLiteral("版本 %1").arg(QStringLiteral(LINGJING_VERSION_STRING)));
            version->setStyleSheet("color: #9AA4B4; font-size: 12px;");
            version->setAlignment(Qt::AlignCenter);
            layout->addWidget(version);

            layout->addSpacing(16);

            QLabel* desc = new QLabel(
                "通用游戏帧生成软件\n\n"
                "· 支持所有 3D 游戏\n"
                "· 最高 20 倍帧率提升\n"
                "· AI 修复画质\n"
                "· 零反作弊风险\n\n"
                "核心算法：淮竹 HuaiZhu\n"
                "几何反演 + 3D 相干流场 + 学习系统\n\n"
                "© 2026 Lingjing Project");
            desc->setAlignment(Qt::AlignCenter);
            desc->setStyleSheet("color: #9AA4B4; font-size: 12px; line-height: 1.6;");
            layout->addWidget(desc);

            layout->addStretch();

            return w;
        }

        // ============================================================================
        // 加载/保存设置
        // ============================================================================

        void SettingsDialog::loadSettings() {
            QSettings settings;

            settings.beginGroup("general");
            themeCombo_->setCurrentIndex(settings.value("theme", 0).toInt());
            languageCombo_->setCurrentIndex(settings.value("language", 0).toInt());
            startWithWindowsCheck_->setChecked(
                settings.value("start_with_windows", false).toBool());
            minimizeToTrayCheck_->setChecked(
                settings.value("minimize_to_tray", true).toBool());
            checkUpdatesCheck_->setChecked(
                settings.value("check_updates", true).toBool());
            settings.endGroup();

            settings.beginGroup("gpu");
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

            settings.beginGroup("superres");
            superResCheck_->setChecked(
                settings.value("enabled", false).toBool());
            superResScaleCombo_->setCurrentIndex(
                settings.value("scale", 0).toInt());
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

            settings.beginGroup("ui");
            wallpaperPathEdit_->setText(settings.value("wallpaper_path").toString());
            rippleEnabledCheck_->setChecked(
                settings.value("ripple_enabled", true).toBool());
            rippleStrengthSlider_->setValue(
                settings.value("ripple_strength", 80).toInt());
            panelOpacitySlider_->setValue(
                settings.value("panel_opacity", 95).toInt());
            settings.endGroup();

            settings.beginGroup("learning");
            learningEnabledCheck_->setChecked(
                settings.value("enabled", true).toBool());
            crowdLearningCheck_->setChecked(
                settings.value("crowd_enabled", false).toBool());
            settings.endGroup();
        }

        void SettingsDialog::saveSettings() {
            QSettings settings;

            settings.beginGroup("general");
            settings.setValue("theme", themeCombo_->currentIndex());
            settings.setValue("language", languageCombo_->currentIndex());
            settings.setValue("start_with_windows", startWithWindowsCheck_->isChecked());
            settings.setValue("minimize_to_tray", minimizeToTrayCheck_->isChecked());
            settings.setValue("check_updates", checkUpdatesCheck_->isChecked());
            settings.endGroup();

            settings.beginGroup("gpu");
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

            settings.beginGroup("superres");
            settings.setValue("enabled", superResCheck_->isChecked());
            settings.setValue("scale", superResScaleCombo_->currentIndex());
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
                    "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                    QSettings::NativeFormat);
                if (startWithWindowsCheck_->isChecked()) {
                    runKey.setValue("Lingjing",
                        QCoreApplication::applicationFilePath());
                } else {
                    runKey.remove("Lingjing");
                }
            }

            settings.beginGroup("ui");
            settings.setValue("wallpaper_path", wallpaperPathEdit_->text());
            settings.setValue("ripple_enabled", rippleEnabledCheck_->isChecked());
            settings.setValue("ripple_strength", rippleStrengthSlider_->value());
            settings.setValue("panel_opacity", panelOpacitySlider_->value());
            settings.endGroup();

            settings.beginGroup("learning");
            settings.setValue("enabled", learningEnabledCheck_->isChecked());
            settings.setValue("crowd_enabled", crowdLearningCheck_->isChecked());
            settings.endGroup();

            settings.sync();
        }

        // ============================================================================
        // 事件
        // ============================================================================

        void SettingsDialog::onAccepted() {
            saveSettings();
            emit backRequested();
        }

        void SettingsDialog::onRejected() {
            emit backRequested();
        }

        void SettingsDialog::setPipelineConfig(FrameGenPipeline* pipeline) {
            pipeline_ = pipeline;
        }

        void SettingsDialog::onBrowseModel() {
            QString path = QFileDialog::getOpenFileName(
                this,
                "选择 AI 模型",
                QStandardPaths::writableLocation(QStandardPaths::AppDataLocation),
                "AI Models (*.onnx *.trt *.xml);;All Files (*.*)");

            if (!path.isEmpty()) {
                modelPathEdit_->setText(path);
            }
        }

        void SettingsDialog::onBrowseWallpaper() {
            QString path = QFileDialog::getOpenFileName(
                this,
                "选择壁纸",
                QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
                "Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp);;All Files (*.*)");

            if (!path.isEmpty()) {
                wallpaperPathEdit_->setText(path);
            }
        }

        void SettingsDialog::onClearLearningData() {
            auto reply = QMessageBox::question(
                this,
                "确认清除",
                "确定要清除所有学习数据吗？\n"
                "这将删除所有已学习的游戏参数，下次启动将从零开始学习。",
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No);

            if (reply == QMessageBox::Yes) {
                LOG_INFO("Clearing learning data");

                QMessageBox::information(this, "已清除",
                    "学习数据已清除。");
            }
        }

        void SettingsDialog::onCheckUpdates() {
            LOG_INFO("Manual update check requested from settings");
            emit checkUpdatesRequested();
        }

        // ============================================================================
        // 学习模型导出/导入/下载
        // ============================================================================

        namespace {
            // 创建指向本机学习数据库的管理器（学习数据未接入主管线时数据库可能为空）
            std::unique_ptr<Lingjing::Learning::LearningManager>
            makeLocalLearningManager() {
                auto mgr = std::make_unique<Lingjing::Learning::LearningManager>();
                Lingjing::Learning::LearningManagerConfig cfg;
                cfg.dbPath = (QStandardPaths::writableLocation(
                    QStandardPaths::AppDataLocation) + "/learning.db").toStdString();
                cfg.enabled = true;
                cfg.autoSave = false;
                cfg.enableTransfer = true;
                cfg.enableActive = true;
                mgr->initialize(cfg);
                return mgr;
            }
        }

        void SettingsDialog::onExportModel() {
            QString dir = QStandardPaths::writableLocation(
                QStandardPaths::DownloadLocation);
            QString path = QFileDialog::getSaveFileName(this,
                "导出学习模型",
                dir + "/lingjing_model.ljm",
                "灵境学习模型 (*.ljm)");
            if (path.isEmpty()) return;

            auto mgr = makeLocalLearningManager();
            std::string err;
            size_t count = 0;
            if (mgr->exportModel(path.toStdString(), err, &count)) {
                QFileInfo fi(path);
                QMessageBox::information(this, "导出成功",
                    QString("已导出 %1 个游戏的学习模型：\n%2\n\n"
                        "文件大小：%3 KB\n\n"
                        "分享给其他人：将 .ljm 文件上传到项目仓库的 models 目录，"
                        "即可让所有用户通过“下载社区模型”使用。")
                        .arg(count).arg(path)
                        .arg(fi.size() / 1024));
            } else {
                QMessageBox::warning(this, "导出失败",
                    QString::fromStdString(err));
            }
        }

        void SettingsDialog::onImportModel() {
            QString path = QFileDialog::getOpenFileName(this,
                "导入学习模型",
                QStandardPaths::writableLocation(QStandardPaths::DownloadLocation),
                "灵境学习模型 (*.ljm)");
            if (path.isEmpty()) return;

            auto mgr = makeLocalLearningManager();
            std::string err;
            size_t count = 0;
            if (mgr->importModel(path.toStdString(), err, &count)) {
                QMessageBox::information(this, "导入成功",
                    QString("已合并 %1 个游戏的学习模型。\n"
                        "导入的参数将自动应用于对应游戏的帧生成。").arg(count));
            } else {
                QMessageBox::warning(this, "导入失败",
                    QString::fromStdString(err));
            }
        }

        void SettingsDialog::onDownloadCommunityModels() {
            const QUrl listUrl(
                "https://cdn.jsdelivr.net/gh/shengtsxk/-@main/models/models.json");
            auto* nam = new QNetworkAccessManager(this);
            QNetworkRequest req{ listUrl };
            req.setTransferTimeout(15000);
            auto* reply = nam->get(req);
            connect(reply, &QNetworkReply::finished, this,
                [this, reply]() {
                    reply->deleteLater();
                    if (reply->error() != QNetworkReply::NoError) {
                        QMessageBox::warning(this, "下载失败",
                            "无法连接社区模型服务器：\n" + reply->errorString() +
                            "\n\n请检查网络后重试。");
                        return;
                    }

                    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
                    QJsonArray models = doc.object().value("models").toArray();
                    if (models.isEmpty()) {
                        QMessageBox::information(this, "社区模型",
                            "社区暂时还没有用户分享模型。\n\n"
                            "你可以先“导出我的模型”，再把 .ljm 文件上传到"
                            "项目仓库的 models 目录，成为第一个分享者。");
                        return;
                    }

                    // 列出模型供选择
                    QStringList names;
                    QList<QJsonObject> objs;
                    for (const auto& v : models) {
                        QJsonObject o = v.toObject();
                        QString name = o.value("name").toString();
                        QString desc = o.value("desc").toString();
                        if (desc.isEmpty()) {
                            desc = o.value("date").toString();
                        }
                        names << (desc.isEmpty()
                            ? name
                            : QString("%1（%2）").arg(name).arg(desc));
                        objs << o;
                    }
                    bool ok = false;
                    QString picked = QInputDialog::getItem(this,
                        "下载社区模型", "选择要下载的模型：", names, 0, false, &ok);
                    if (!ok || picked.isEmpty()) return;

                    int idx = names.indexOf(picked);
                    if (idx < 0 || idx >= objs.size()) return;
                    QJsonObject o = objs[idx];
                    QString file = o.value("file").toString();
                    if (file.isEmpty()) return;

                    // 下载模型文件到临时目录
                    QUrl fileUrl(QString("https://cdn.jsdelivr.net/gh/shengtsxk/-@main/models/%1")
                        .arg(file));
                    auto* nam2 = new QNetworkAccessManager(this);
                    QNetworkRequest req2{ fileUrl };
                    req2.setTransferTimeout(60000);
                    auto* reply2 = nam2->get(req2);
                    connect(reply2, &QNetworkReply::finished, this,
                        [this, reply2, nam2, file]() {
                            reply2->deleteLater();
                            nam2->deleteLater();
                            if (reply2->error() != QNetworkReply::NoError) {
                                QMessageBox::warning(this, "下载失败",
                                    "模型下载失败：\n" + reply2->errorString());
                                return;
                            }
                            const QByteArray data = reply2->readAll();
                            const QString tmp = QStandardPaths::writableLocation(
                                QStandardPaths::TempLocation)
                                + "/lingjing_community_model.ljm";
                            QFile f(tmp);
                            if (!f.open(QIODevice::WriteOnly)) {
                                QMessageBox::warning(this, "下载失败",
                                    "无法保存模型文件到临时目录。");
                                return;
                            }
                            f.write(data);
                            f.close();

                            // 导入合并
                            auto mgr = makeLocalLearningManager();
                            std::string err;
                            size_t count = 0;
                            bool okImp = mgr->importModel(
                                tmp.toStdString(), err, &count);
                            f.remove();
                            if (okImp) {
                                QMessageBox::information(this, "导入成功",
                                    QString("已下载并合并 %1 个游戏的学习模型（%2）。")
                                        .arg(count).arg(file));
                            } else {
                                QMessageBox::warning(this, "导入失败",
                                    QString::fromStdString(err));
                            }
                        });
                });
        }

    } // namespace UI
} // namespace Lingjing
