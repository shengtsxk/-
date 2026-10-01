#pragma once

#include "core/Types.h"
#include "ui/Theme.h"

#include <QWidget>
#include <memory>

class QLabel;
class QSlider;
class QComboBox;
class QPushButton;
class QCheckBox;
class QVBoxLayout;
class QHBoxLayout;
class QButtonGroup;
class QStackedWidget;

namespace Lingjing {

    class FrameGenPipeline;

    namespace UI {

        class ControlPanel : public QWidget {
            Q_OBJECT

        public:
            explicit ControlPanel(QWidget* parent = nullptr);
            ~ControlPanel() override;

            void attachPipeline(FrameGenPipeline* pipeline);
            void setTargetGame(const QString& name, void* hwnd);

            // 从设置恢复开关状态（不触发管线联动信号）
            void restoreFrameGenEnabled(bool enabled);
            void restoreSuperResolution(bool enabled, uint32_t scale);

            // 当前开关状态（供启动时组装配置，主界面开关优先于已存设置）
            bool isFrameGenEnabled() const;
            bool isSuperResEnabled() const;
            uint32_t superResScale() const;

        signals:
            void multiplierChanged(uint32_t multiplier);
            void qualityChanged(uint32_t level);
            void frameGenToggled(bool enabled);
            void superResChanged(bool enabled, uint32_t scale);
            void settingsChanged();

        private slots:
            void onMultiplierSlider(int value);
            void onQualityChanged(int index);
            void onFrameGenToggled(bool checked);
            void onSuperResToggled(bool checked);
            void onAiRepairToggled(bool checked);
            void onDeJellyToggled(bool checked);
            void onOcclusionAwareToggled(bool checked);
            void onTemporalToggled(bool checked);
            void onLatencyTargetChanged(int index);

        private:
            void setupUi();
            void applyTheme();

            // 模块切换（JEXE 帧生成 / ASESS 超分辨率）
            QPushButton* jexeTab_ = nullptr;
            QPushButton* asessTab_ = nullptr;
            QButtonGroup* moduleTabGroup_ = nullptr;
            QStackedWidget* moduleStack_ = nullptr;
            QWidget* jexePage_ = nullptr;
            QWidget* asessPage_ = nullptr;

            QLabel* gameLabel_ = nullptr;
            QLabel* gameName_ = nullptr;

            // 插值倍率
            QCheckBox* frameGenCheck_ = nullptr;
            QLabel* multiplierLabel_ = nullptr;
            QSlider* multiplierSlider_ = nullptr;
            QLabel* multiplierValue_ = nullptr;

            // 质量档位
            QLabel* qualityLabel_ = nullptr;
            QComboBox* qualityCombo_ = nullptr;

            // 延迟目标
            QLabel* latencyLabel_ = nullptr;
            QComboBox* latencyCombo_ = nullptr;

            // 特性开关
            QCheckBox* superResCheck_ = nullptr;
            QComboBox* superResScaleCombo_ = nullptr;
            QCheckBox* aiRepairCheck_ = nullptr;
            QCheckBox* deJellyCheck_ = nullptr;
            QCheckBox* occlusionAwareCheck_ = nullptr;
            QCheckBox* temporalCheck_ = nullptr;

            // 参数微调
            QLabel* deJellyLabel_ = nullptr;
            QSlider* deJellySlider_ = nullptr;
            QLabel* deJellyValue_ = nullptr;

            QLabel* temporalLabel_ = nullptr;
            QSlider* temporalSlider_ = nullptr;
            QLabel* temporalValue_ = nullptr;

            FrameGenPipeline* pipeline_ = nullptr;
        };

    } // namespace UI
} // namespace Lingjing