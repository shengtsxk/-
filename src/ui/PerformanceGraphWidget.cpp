#include "ui/PerformanceGraphWidget.h"
#include "ui/Theme.h"
#include "pipeline/FrameGenPipeline.h"

#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QLinearGradient>
#include <QLabel>
#include <QVBoxLayout>
#include <chrono>

namespace Lingjing {
    namespace UI {

        // ============================================================================
        // 构造/析构
        // ============================================================================

        PerformanceGraphWidget::PerformanceGraphWidget(QWidget* parent)
            : QWidget(parent)
        {
            setupUi();
            applyTheme();

            // 更新定时器（30 FPS）
            updateTimer_ = new QTimer(this);
            connect(updateTimer_, &QTimer::timeout,
                this, &PerformanceGraphWidget::onUpdateTimer);
            updateTimer_->start(33);
        }

        PerformanceGraphWidget::~PerformanceGraphWidget() {
            if (updateTimer_) {
                updateTimer_->stop();
            }
        }

        // ============================================================================
        // UI
        // ============================================================================

        void PerformanceGraphWidget::setupUi() {
            setMinimumHeight(180);
        }

        void PerformanceGraphWidget::applyTheme() {
            const int alpha = Theme::instance().panelOpacity();
            QColor bg(0x0A, 0x0D, 0x12, alpha * 255 / 100);
            setStyleSheet(QString(
                "PerformanceGraphWidget {"
                "  background-color: %1;"
                "  border-top: 1px solid rgba(42, 50, 64, 180);"
                "}"
            ).arg(bg.name(QColor::HexArgb)));
        }

        // ============================================================================
        // 绑定
        // ============================================================================

        void PerformanceGraphWidget::attachPipeline(FrameGenPipeline* pipeline) {
            pipeline_ = pipeline;
        }

        // ============================================================================
        // 更新
        // ============================================================================

        void PerformanceGraphWidget::onUpdateTimer() {
            if (!pipeline_) return;

            addSample();
            update();
        }

        void PerformanceGraphWidget::addSample() {
            auto stats = pipeline_->stats();

            auto now = std::chrono::steady_clock::now();
            double t = std::chrono::duration<double>(
                now.time_since_epoch()).count();

            if (startTime_ == 0.0) {
                startTime_ = t;
            }

            Sample s;
            s.timestamp = t - startTime_;
            s.sourceFps = stats.currentSourceFps;
            s.outputFps = stats.currentOutputFps;
            s.latencyMs = stats.avgTotalMs;
            s.gpuUtil = stats.gpuUtilization;

            samples_.push_back(s);

            // 清理旧数据
            while (samples_.size() > kMaxSamples) {
                samples_.pop_front();
            }

            // 动态调整最大值
            if (s.outputFps > maxFps_) {
                maxFps_ = s.outputFps * 1.2;
            }

            if (s.latencyMs > maxLatency_) {
                maxLatency_ = s.latencyMs * 1.2;
            }
        }

        // ============================================================================
        // 绘制
        // ============================================================================

        void PerformanceGraphWidget::paintEvent(QPaintEvent* event) {
            QPainter p(this);
            p.setRenderHint(QPainter::Antialiasing);

            // 背景
            const int alpha = Theme::instance().panelOpacity();
            p.fillRect(rect(), QColor(0x0A, 0x0D, 0x12, alpha * 255 / 100));

            if (samples_.empty()) {
                p.setPen(QColor(0x5A, 0x62, 0x70));
                p.drawText(rect(), Qt::AlignCenter, "等待数据...");
                return;
            }

            drawGrid(p);
            drawFpsCurves(p);
            drawLatencyCurve(p);
            drawLegend(p);
        }

        QRect PerformanceGraphWidget::graphRect() const {
            return QRect(50, 20, width() - 70, height() - 60);
        }

        void PerformanceGraphWidget::drawGrid(QPainter& p) {
            QRect r = graphRect();

            if (r.width() <= 0 || r.height() <= 0) return;

            // 背景
            const int alpha = Theme::instance().panelOpacity();
            p.fillRect(r, QColor(0x14, 0x18, 0x20, alpha * 255 / 100));

            // 水平网格线
            p.setPen(QPen(QColor(0x2A, 0x32, 0x40), 1, Qt::DashLine));

            const int hLines = 4;

            for (int i = 0; i <= hLines; ++i) {
                int y = r.top() + r.height() * i / hLines;

                p.drawLine(r.left(), y, r.right(), y);

                // Y 轴标签
                double value = maxFps_ * (1.0 - static_cast<double>(i) / hLines);

                p.setPen(QColor(0x5A, 0x62, 0x70));

                p.drawText(QRect(0, y - 8, 45, 16),
                    Qt::AlignRight | Qt::AlignVCenter,
                    QString::number(static_cast<int>(value)));

                p.setPen(QPen(QColor(0x2A, 0x32, 0x40), 1, Qt::DashLine));
            }

            // 垂直网格线
            const int vLines = 5;

            for (int i = 0; i <= vLines; ++i) {
                int x = r.left() + r.width() * i / vLines;

                p.drawLine(x, r.top(), x, r.bottom());

                // X 轴标签
                double sec = windowSeconds_ * static_cast<double>(i) / vLines;

                p.setPen(QColor(0x5A, 0x62, 0x70));

                p.drawText(QRect(x - 20, r.bottom() + 4, 40, 16),
                    Qt::AlignCenter,
                    QString("-%1s").arg(static_cast<int>(
                        windowSeconds_ - sec)));

                p.setPen(QPen(QColor(0x2A, 0x32, 0x40), 1, Qt::DashLine));
            }
        }

        void PerformanceGraphWidget::drawFpsCurves(QPainter& p) {
            QRect r = graphRect();

            if (r.width() <= 0 || r.height() <= 0) return;

            double currentTime = samples_.back().timestamp;
            double minTime = currentTime - windowSeconds_;
            if (minTime < 0.0) minTime = 0.0;

            auto drawCurve = [&](const QColor& color,
                std::function<double(const Sample&)> getter) {
                    QPainterPath path;
                    bool first = true;

                    for (const auto& s : samples_) {
                        if (s.timestamp < minTime) continue;

                        double t = (s.timestamp - minTime) /
                            (windowSeconds_ + 1e-6);
                        double value = getter(s);

                        double x = r.left() + t * r.width();
                        double y = r.bottom() - (value / maxFps_) * r.height();

                        if (first) {
                            path.moveTo(x, y);
                            first = false;
                        }
                        else {
                            path.lineTo(x, y);
                        }
                    }

                    if (!path.isEmpty()) {
                        // 渐变填充
                        QPainterPath fillPath = path;
                        fillPath.lineTo(r.right(), r.bottom());
                        fillPath.lineTo(r.left(), r.bottom());
                        fillPath.closeSubpath();

                        QLinearGradient grad(r.topLeft(), r.bottomLeft());
                        QColor gradColor = color;
                        gradColor.setAlpha(80);
                        grad.setColorAt(0.0, gradColor);

                        QColor gradColor2 = color;
                        gradColor2.setAlpha(0);
                        grad.setColorAt(1.0, gradColor2);

                        p.fillPath(fillPath, grad);

                        // 曲线
                        p.setPen(QPen(color, 2));
                        p.drawPath(path);
                    }
                };

            drawCurve(QColor(0x4A, 0x9E, 0xFF),
                [](const Sample& s) { return s.sourceFps; });

            drawCurve(QColor(0x4A, 0xD9, 0x8A),
                [](const Sample& s) { return s.outputFps; });
        }

        void PerformanceGraphWidget::drawLatencyCurve(QPainter& p) {
            QRect r = graphRect();

            if (r.width() <= 0 || r.height() <= 0) return;

            double currentTime = samples_.back().timestamp;
            double minTime = currentTime - windowSeconds_;
            if (minTime < 0.0) minTime = 0.0;

            QPainterPath path;
            bool first = true;

            for (const auto& s : samples_) {
                if (s.timestamp < minTime) continue;

                double t = (s.timestamp - minTime) / (windowSeconds_ + 1e-6);

                double value = s.latencyMs;
                if (value > maxLatency_) value = maxLatency_;

                double x = r.left() + t * r.width();
                double y = r.bottom() - (value / maxLatency_) * r.height();

                if (first) {
                    path.moveTo(x, y);
                    first = false;
                }
                else {
                    path.lineTo(x, y);
                }
            }

            if (!path.isEmpty()) {
                p.setPen(QPen(QColor(0xFF, 0xB8, 0x4A), 2, Qt::DashLine));
                p.drawPath(path);
            }
        }

        void PerformanceGraphWidget::drawLegend(QPainter& p) {
            QRect r = graphRect();

            p.setFont(QFont("Microsoft YaHei UI", 9));

            int y = 10;
            int x = r.left();

            auto drawLegendItem = [&](const QColor& color,
                const QString& text) {
                    p.fillRect(x, y + 4, 12, 3, color);
                    x += 16;

                    p.setPen(QColor(0x9A, 0xA4, 0xB4));
                    p.drawText(x, y + 12, text);

                    QFontMetrics fm(p.font());
                    x += fm.horizontalAdvance(text) + 20;
                };

            drawLegendItem(QColor(0x4A, 0x9E, 0xFF), "源 FPS");
            drawLegendItem(QColor(0x4A, 0xD9, 0x8A), "输出 FPS");
            drawLegendItem(QColor(0xFF, 0xB8, 0x4A), "延迟 (ms)");
        }

        void PerformanceGraphWidget::resizeEvent(QResizeEvent* event) {
            QWidget::resizeEvent(event);
            update();
        }

    } // namespace UI
} // namespace Lingjing
