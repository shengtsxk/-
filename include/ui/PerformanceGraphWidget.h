#pragma once

#include <QWidget>
#include <deque>
#include <memory>

class QLabel;
class QTimer;

namespace Lingjing {

    class FrameGenPipeline;

    namespace UI {

        class PerformanceGraphWidget : public QWidget {
            Q_OBJECT

        public:
            explicit PerformanceGraphWidget(QWidget* parent = nullptr);
            void applyTheme();
            ~PerformanceGraphWidget() override;

            void attachPipeline(FrameGenPipeline* pipeline);

        protected:
            void paintEvent(QPaintEvent* event) override;
            void resizeEvent(QResizeEvent* event) override;

        private slots:
            void onUpdateTimer();

        private:
            struct Sample {
                double timestamp;
                double sourceFps;
                double outputFps;
                double latencyMs;
                double gpuUtil;
            };

            void setupUi();
            void addSample();

            void drawGrid(QPainter& p);
            void drawFpsCurves(QPainter& p);
            void drawLatencyCurve(QPainter& p);
            void drawLegend(QPainter& p);

            QRect graphRect() const;

            FrameGenPipeline* pipeline_ = nullptr;

            std::deque<Sample> samples_;
            static constexpr size_t kMaxSamples = 300;

            QTimer* updateTimer_ = nullptr;

            // 图表范围
            double startTime_ = 0.0;
            double windowSeconds_ = 10.0;

            // 最大值
            double maxFps_ = 240.0;
            double maxLatency_ = 20.0;
        };

    } // namespace UI
} // namespace Lingjing
