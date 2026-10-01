#include "ui/ControlPanel.h"
#include "pipeline/FrameGenPipeline.h"
#include "core/Logger.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QGroupBox>
#include <QSettings>
#include <QSignalBlocker>
#include <QFrame>
#include <QPainter>
#include <QPainterPath>
#include <QButtonGroup>
#include <QStackedWidget>

namespace Lingjing {
    namespace UI {

        // ============================================================================
        // 构造/析构
        // ============================================================================

        ControlPanel::ControlPanel(QWidget* parent)
            : QWidget(parent)
        {
            setupUi();
            applyTheme();
        }

        ControlPanel::~ControlPanel() = default;

        // ============================================================================
        // UI 设置
        // ============================================================================

        void ControlPanel::setupUi() {
            QVBoxLayout* mainLayout = new QVBoxLayout(this);
            mainLayout->setContentsMargins(16, 16, 16, 16);
            mainLayout->setSpacing(16);

            // ============================================================
            // 标题区
            // ============================================================

            {
                QWidget* header = new QWidget(this);

                QVBoxLayout* layout = new QVBoxLayout(header);
                layout->setContentsMargins(0, 0, 0, 0);
                layout->setSpacing(4);

                gameLabel_ = new QLabel("当前游戏", header);
                gameLabel_->setStyleSheet(
                    "color: #9AA4B4;"
                    "font-size: 11px;");
                layout->addWidget(gameLabel_);

                gameName_ = new QLabel("未选择", header);
                gameName_->setStyleSheet(
                    "color: #E8ECF2;"
                    "font-size: 18px;"
                    "font-weight: bold;");
                layout->addWidget(gameName_);

                mainLayout->addWidget(header);
            }

            // ============================================================
            // 模块切换条（JEXE 帧生成 / ASESS 超分辨率）
            // ============================================================

            {
                QWidget* switcher = new QWidget(this);
                QHBoxLayout* swLayout = new QHBoxLayout(switcher);
                swLayout->setContentsMargins(0, 0, 0, 0);
                swLayout->setSpacing(8);

                const QString tabQss =
                    "QPushButton { background-color: #1A1E28; border: 1px solid #2A3240;"
                    " border-radius: 6px; padding: 8px 18px; color: #C0C8D4; font-weight: bold; }"
                    "QPushButton:hover { color: #FFFFFF; border-color: #4A9EFF; }"
                    "QPushButton:checked { background-color: #4A9EFF; border-color: #4A9EFF;"
                    " color: white; }";

                jexeTab_ = new QPushButton("JEXE 帧生成", switcher);
                jexeTab_->setCheckable(true);
                jexeTab_->setChecked(true);
                jexeTab_->setCursor(Qt::PointingHandCursor);
                jexeTab_->setStyleSheet(tabQss);
                swLayout->addWidget(jexeTab_);

                asessTab_ = new QPushButton("ASESS 超分辨率", switcher);
                asessTab_->setCheckable(true);
                asessTab_->setCursor(Qt::PointingHandCursor);
                asessTab_->setStyleSheet(tabQss);
                swLayout->addWidget(asessTab_);

                swLayout->addStretch();
                mainLayout->addWidget(switcher);

                moduleTabGroup_ = new QButtonGroup(this);
                moduleTabGroup_->setExclusive(true);
                moduleTabGroup_->addButton(jexeTab_);
                moduleTabGroup_->addButton(asessTab_);
            }

            // ============================================================
            // 模块堆叠容器（JEXE 页 / ASESS 页）
            // ============================================================

            moduleStack_ = new QStackedWidget(this);
            jexePage_ = new QWidget(moduleStack_);
            asessPage_ = new QWidget(moduleStack_);
            moduleStack_->addWidget(jexePage_);
            moduleStack_->addWidget(asessPage_);
            mainLayout->addWidget(moduleStack_, 1);

            {
                QVBoxLayout* jl = new QVBoxLayout(jexePage_);
                jl->setContentsMargins(0, 0, 0, 0);
                jl->setSpacing(16);

                // ============================================================
                // JEXE 帧生成页：插值倍率组
                // ============================================================

                {
                QGroupBox* group = new QGroupBox("插值倍率", this);

                QVBoxLayout* layout = new QVBoxLayout(group);
                layout->setSpacing(12);

                // 帧生成总开关（关 = 直通，不插帧）
                frameGenCheck_ = new QCheckBox("JEXE 帧生成", group);
                frameGenCheck_->setChecked(true);
                frameGenCheck_->setToolTip("JEXE 总开关：关闭后不插帧，画面直通显示");
                layout->addWidget(frameGenCheck_);

                // 倍率滑块
                QHBoxLayout* sliderRow = new QHBoxLayout();

                QLabel* minLabel = new QLabel("1x", group);
                minLabel->setStyleSheet("color: #9AA4B4; font-size: 11px;");
                minLabel->setMinimumWidth(30);
                sliderRow->addWidget(minLabel);
                multiplierSlider_ = new QSlider(Qt::Horizontal, group);
                multiplierSlider_->setMinimum(1);
                multiplierSlider_->setMaximum(20);
                multiplierSlider_->setValue(2);
                multiplierSlider_->setTickPosition(QSlider::TicksBelow);
                multiplierSlider_->setTickInterval(1);
                multiplierSlider_->setMinimumHeight(20);
                sliderRow->addWidget(multiplierSlider_, 1);

                multiplierLabel_ = new QLabel("20x", group);
                multiplierLabel_->setStyleSheet("color: #9AA4B4; font-size: 11px;");
                multiplierLabel_->setMinimumWidth(30);
                sliderRow->addWidget(multiplierLabel_);

                layout->addLayout(sliderRow);

                // 当前值
                multiplierValue_ = new QLabel("2x", group);
                multiplierValue_->setAlignment(Qt::AlignCenter);
                multiplierValue_->setStyleSheet(
                    "color: #4A9EFF;"
                    "font-size: 32px;"
                    "font-weight: bold;");
                layout->addWidget(multiplierValue_);

                jl->addWidget(group);
            }

            // ============================================================
            // 质量档位组
            // ============================================================

            {
                QGroupBox* group = new QGroupBox("画质档位", this);

                QVBoxLayout* layout = new QVBoxLayout(group);
                layout->setSpacing(12);

                qualityLabel_ = new QLabel("选择画质", group);
                qualityLabel_->setStyleSheet("color: #9AA4B4; font-size: 11px;");
                layout->addWidget(qualityLabel_);

                qualityCombo_ = new QComboBox(group);
                qualityCombo_->addItem("性能优先");
                qualityCombo_->addItem("平衡");
                qualityCombo_->addItem("画质优先");
                qualityCombo_->addItem("极致（AI 修复）");
                qualityCombo_->setCurrentIndex(1);
                qualityCombo_->setMinimumHeight(36);
                layout->addWidget(qualityCombo_);

                jl->addWidget(group);
            }

            // ============================================================
            // 延迟目标组
            // ============================================================

            {
                QGroupBox* group = new QGroupBox("延迟目标", this);

                QVBoxLayout* layout = new QVBoxLayout(group);
                layout->setSpacing(12);

                latencyLabel_ = new QLabel("选择延迟目标", group);
                latencyLabel_->setStyleSheet("color: #9AA4B4; font-size: 11px;");
                layout->addWidget(latencyLabel_);

                latencyCombo_ = new QComboBox(group);
                latencyCombo_->addItem("极限低延迟 (1ms)");
                latencyCombo_->addItem("低延迟 (2ms)");
                latencyCombo_->addItem("平衡 (4ms)");
                latencyCombo_->addItem("画质优先 (8ms)");
                latencyCombo_->setCurrentIndex(3);
                latencyCombo_->setMinimumHeight(36);
                layout->addWidget(latencyCombo_);

                jl->addWidget(group);
            }

            // ============================================================
            // 特性开关组
            // ============================================================

            {
                QGroupBox* group = new QGroupBox("高级特性", this);

                QVBoxLayout* layout = new QVBoxLayout(group);
                layout->setSpacing(8);

                aiRepairCheck_ = new QCheckBox("AI 修复", group);
                aiRepairCheck_->setChecked(true);
                aiRepairCheck_->setToolTip("启用 AI 模型修复插值帧的画质");
                layout->addWidget(aiRepairCheck_);

                deJellyCheck_ = new QCheckBox("去果冻效应", group);
                deJellyCheck_->setChecked(true);
                deJellyCheck_->setToolTip("消除插值帧的果冻抖动");
                layout->addWidget(deJellyCheck_);

                occlusionAwareCheck_ = new QCheckBox("遮挡感知", group);
                occlusionAwareCheck_->setChecked(true);
                occlusionAwareCheck_->setToolTip("正确处理遮挡区域，避免鬼影");
                layout->addWidget(occlusionAwareCheck_);

                temporalCheck_ = new QCheckBox("时域平滑", group);
                temporalCheck_->setChecked(true);
                temporalCheck_->setToolTip("平滑帧间过渡，减少闪烁");
                layout->addWidget(temporalCheck_);

                jl->addWidget(group);
            }

            // ============================================================
            // 参数微调组
            // ============================================================

            {
                QGroupBox* group = new QGroupBox("参数微调", this);

                QVBoxLayout* layout = new QVBoxLayout(group);
                layout->setSpacing(12);

                // 去果冻强度
                {
                    QHBoxLayout* row = new QHBoxLayout();

                    deJellyLabel_ = new QLabel("去果冻强度", group);
                    deJellyLabel_->setStyleSheet("color: #9AA4B4; font-size: 11px;");
                    row->addWidget(deJellyLabel_);

                    row->addStretch();

                    deJellyValue_ = new QLabel("60%", group);
                    deJellyValue_->setStyleSheet(
                        "color: #4A9EFF;"
                        "font-size: 11px;"
                        "font-weight: bold;");
                    row->addWidget(deJellyValue_);

                    layout->addLayout(row);
                }

                deJellySlider_ = new QSlider(Qt::Horizontal, group);
                deJellySlider_->setMinimum(0);
                deJellySlider_->setMaximum(100);
                deJellySlider_->setValue(60);
                deJellySlider_->setMinimumHeight(20);
                layout->addWidget(deJellySlider_);

                // 时域平滑权重
                {
                    QHBoxLayout* row = new QHBoxLayout();

                    temporalLabel_ = new QLabel("时域平滑权重", group);
                    temporalLabel_->setStyleSheet("color: #9AA4B4; font-size: 11px;");
                    row->addWidget(temporalLabel_);

                    row->addStretch();

                    temporalValue_ = new QLabel("15%", group);
                    temporalValue_->setStyleSheet(
                        "color: #4A9EFF;"
                        "font-size: 11px;"
                        "font-weight: bold;");
                    row->addWidget(temporalValue_);

                    layout->addLayout(row);
                }

                temporalSlider_ = new QSlider(Qt::Horizontal, group);
                temporalSlider_->setMinimum(0);
                temporalSlider_->setMaximum(50);
                temporalSlider_->setValue(15);
                temporalSlider_->setMinimumHeight(20);
                layout->addWidget(temporalSlider_);

                jl->addWidget(group);
                jl->addStretch();
            }

            // ============================================================
            // JEXE 页结束（闭合 jl 布局块）
            // ============================================================

            }

            // ============================================================
            // ASESS 超分辨率页
            // ============================================================

            {
                QVBoxLayout* al = new QVBoxLayout(asessPage_);
                al->setContentsMargins(0, 0, 0, 0);
                al->setSpacing(16);

                QGroupBox* group = new QGroupBox("ASESS 超分辨率", asessPage_);

                QVBoxLayout* layout = new QVBoxLayout(group);
                layout->setSpacing(12);

                {
                    QHBoxLayout* srRow = new QHBoxLayout();

                    superResCheck_ = new QCheckBox("ASESS 超分辨率", group);
                    superResCheck_->setChecked(false);
                    superResCheck_->setToolTip(
                        "ASESS 输出前放大（D3D11 通用加速，无需 Tensor Core），画质明显优于双三次");
                    srRow->addWidget(superResCheck_);

                    srRow->addStretch();

                    superResScaleCombo_ = new QComboBox(group);
                    superResScaleCombo_->addItem("2x");
                    superResScaleCombo_->addItem("3x");
                    superResScaleCombo_->addItem("4x");
                    superResScaleCombo_->addItem("5x");
                    superResScaleCombo_->setCurrentIndex(0);
                    superResScaleCombo_->setMinimumHeight(26);
                    superResScaleCombo_->setEnabled(false);
                    srRow->addWidget(superResScaleCombo_);

                    layout->addLayout(srRow);
                }

                QLabel* info = new QLabel(
                    "原创边缘引导超分算法（ASESS v2）：8 方向边缘分类 + 边缘强度调制细节 + 振铃抑制。\n"
                    "无需 Tensor Core / 深度学习。可单独使用，也可与 JEXE 帧生成叠加。",
                    group);
                info->setWordWrap(true);
                info->setStyleSheet("color: #9AA4B4; font-size: 11px;");
                layout->addWidget(info);

                al->addWidget(group);
                al->addStretch();
            }

            // ============================================================
            // 连接
            // ============================================================

            connect(jexeTab_, &QPushButton::toggled, this, [this](bool on) {
                if (on) moduleStack_->setCurrentIndex(0);
            });

            connect(asessTab_, &QPushButton::toggled, this, [this](bool on) {
                if (on) moduleStack_->setCurrentIndex(1);
            });

            connect(multiplierSlider_, &QSlider::valueChanged,
                this, &ControlPanel::onMultiplierSlider);

            connect(frameGenCheck_, &QCheckBox::toggled,
                this, &ControlPanel::onFrameGenToggled);

            connect(superResCheck_, &QCheckBox::toggled,
                this, &ControlPanel::onSuperResToggled);

            connect(superResScaleCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int) {
                    superResScaleCombo_->setEnabled(superResCheck_->isChecked());
                    onSuperResToggled(superResCheck_->isChecked());
                });

            connect(qualityCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &ControlPanel::onQualityChanged);

            connect(latencyCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &ControlPanel::onLatencyTargetChanged);

            connect(aiRepairCheck_, &QCheckBox::toggled,
                this, &ControlPanel::onAiRepairToggled);

            connect(deJellyCheck_, &QCheckBox::toggled,
                this, &ControlPanel::onDeJellyToggled);

            connect(occlusionAwareCheck_, &QCheckBox::toggled,
                this, &ControlPanel::onOcclusionAwareToggled);

            connect(temporalCheck_, &QCheckBox::toggled,
                this, &ControlPanel::onTemporalToggled);

            connect(deJellySlider_, &QSlider::valueChanged, this, [this](int v) {
                deJellyValue_->setText(QString("%1%").arg(v));
                emit settingsChanged();
                });

            connect(temporalSlider_, &QSlider::valueChanged, this, [this](int v) {
                temporalValue_->setText(QString("%1%").arg(v));
                emit settingsChanged();
                });
        }

        // ============================================================================
        // 主题
        // ============================================================================

        void ControlPanel::applyTheme() {
            // 依赖全局主题样式表，仅设置面板自身背景
            setAutoFillBackground(true);
        }

        // ============================================================================
        // 绑定
        // ============================================================================

        void ControlPanel::attachPipeline(FrameGenPipeline* pipeline) {
            pipeline_ = pipeline;
        }

        void ControlPanel::setTargetGame(const QString& name, void* hwnd) {
            gameName_->setText(name);

            if (hwnd) {
                gameName_->setStyleSheet(
                    "color: #4AD98A;"
                    "font-size: 18px;"
                    "font-weight: bold;");
            }
            else {
                gameName_->setStyleSheet(
                    "color: #E8ECF2;"
                    "font-size: 18px;"
                    "font-weight: bold;");
            }
        }

        // ============================================================================
        // 槽函数
        // ============================================================================

        void ControlPanel::onMultiplierSlider(int value) {
            multiplierValue_->setText(QString("%1x").arg(value));

            if (pipeline_) {
                pipeline_->setMultiplier(static_cast<uint32_t>(value));
            }

            emit multiplierChanged(static_cast<uint32_t>(value));
        }

        void ControlPanel::onQualityChanged(int index) {
            QualityLevel level = static_cast<QualityLevel>(index);

            if (pipeline_) {
                pipeline_->setQualityLevel(level);
            }

            emit qualityChanged(static_cast<uint32_t>(index));
        }

        void ControlPanel::onLatencyTargetChanged(int index) {
            LatencyTarget target = static_cast<LatencyTarget>(index);

            if (pipeline_) {
                pipeline_->setLatencyTarget(target);
            }
        }

        void ControlPanel::restoreFrameGenEnabled(bool enabled) {
            QSignalBlocker b(frameGenCheck_);
            frameGenCheck_->setChecked(enabled);
        }

        void ControlPanel::restoreSuperResolution(bool enabled, uint32_t scale) {
            {
                QSignalBlocker b(superResCheck_);
                superResCheck_->setChecked(enabled);
            }
            {
                QSignalBlocker b(superResScaleCombo_);
                superResScaleCombo_->setCurrentIndex(
                    (scale >= 2 && scale <= 5) ? (int)scale - 2 : 0);
            }
            superResScaleCombo_->setEnabled(enabled);
        }

        bool ControlPanel::isFrameGenEnabled() const {
            return frameGenCheck_ && frameGenCheck_->isChecked();
        }

        bool ControlPanel::isSuperResEnabled() const {
            return superResCheck_ && superResCheck_->isChecked();
        }

        uint32_t ControlPanel::superResScale() const {
            if (!superResScaleCombo_) return 2;
            uint32_t s = static_cast<uint32_t>(superResScaleCombo_->currentIndex() + 2);
            return (s >= 2 && s <= 5) ? s : 2;
        }

        void ControlPanel::onFrameGenToggled(bool checked) {
            {
                QSettings s;
                s.beginGroup("frame_gen");
                s.setValue("enabled", checked);
                s.endGroup();
            }
            LOG_INFO("JEXE frame generation: %s", checked ? "enabled" : "disabled");
            emit frameGenToggled(checked);
        }

        void ControlPanel::onSuperResToggled(bool checked) {
            uint32_t scale = static_cast<uint32_t>(superResScaleCombo_->currentIndex() + 2);
            superResScaleCombo_->setEnabled(checked);
            {
                QSettings s;
                s.beginGroup("superres");
                s.setValue("enabled", checked);
                s.setValue("scale", superResScaleCombo_->currentIndex());
                s.endGroup();
            }
            LOG_INFO("ASESS super-resolution: %s (scale=%ux)", checked ? "enabled" : "disabled", scale);
            emit superResChanged(checked, scale);
        }

        void ControlPanel::onAiRepairToggled(bool checked) {
            LOG_INFO("AI repair: %s", checked ? "enabled" : "disabled");
            emit settingsChanged();
        }

        void ControlPanel::onDeJellyToggled(bool checked) {
            LOG_INFO("De-jelly: %s", checked ? "enabled" : "disabled");
            emit settingsChanged();
        }

        void ControlPanel::onOcclusionAwareToggled(bool checked) {
            LOG_INFO("Occlusion aware: %s", checked ? "enabled" : "disabled");
            emit settingsChanged();
        }

        void ControlPanel::onTemporalToggled(bool checked) {
            LOG_INFO("Temporal smoothing: %s", checked ? "enabled" : "disabled");
            emit settingsChanged();
        }

    } // namespace UI
} // namespace Lingjing
