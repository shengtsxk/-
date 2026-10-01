#include "ui/RippleWidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QRandomGenerator>
#include <cmath>

namespace Lingjing {
    namespace UI {

        // ============================================================================
        // 构造/析构
        // ============================================================================

        RippleWidget::RippleWidget(QWidget* parent)
            : QWidget(parent)
        {
            setAttribute(Qt::WA_TransparentForMouseEvents, false);
            setAttribute(Qt::WA_NoSystemBackground);
            setAttribute(Qt::WA_TranslucentBackground);

            updateTimer_ = new QTimer(this);
            connect(updateTimer_, &QTimer::timeout,
                this, &RippleWidget::onUpdateTimer);
            updateTimer_->start(16);  // 60 FPS
        }

        RippleWidget::~RippleWidget() {
            if (updateTimer_) {
                updateTimer_->stop();
            }
        }

        // ============================================================================
        // 触发
        // ============================================================================

        void RippleWidget::triggerRipple(const QPointF& center) {
            Ripple r;
            r.center = center;
            r.startTime = std::chrono::steady_clock::now();
            r.maxRadius = 200.0f;

            // 随机颜色（青蓝色系）
            int hue = 200 + QRandomGenerator::global()->bounded(40) - 20;

            r.color = QColor::fromHsv(hue, 180, 255, 200);

            ripples_.push_back(r);

            // 最多保留 10 个
            while (ripples_.size() > 10) {
                ripples_.pop_front();
            }
        }

        // ============================================================================
        // 鼠标事件
        // ============================================================================

        void RippleWidget::mousePressEvent(QMouseEvent* event) {
            if (event->button() == Qt::LeftButton) {
                triggerRipple(event->position());
                emit rippleTriggered(event->position().toPoint());
            }

            QWidget::mousePressEvent(event);
        }

        // ============================================================================
        // 更新
        // ============================================================================

        void RippleWidget::onUpdateTimer() {
            updateRipples();

            if (!ripples_.empty()) {
                emit frameUpdated();
            }
        }

        void RippleWidget::updateRipples() {
            auto now = std::chrono::steady_clock::now();

            const double lifetime = 1.2;  // 秒

            // 移除过期
            while (!ripples_.empty()) {
                double elapsed = std::chrono::duration<double>(
                    now - ripples_.front().startTime).count();

                if (elapsed > lifetime) {
                    ripples_.pop_front();
                }
                else {
                    break;
                }
            }
        }

        // ============================================================================
        // 绘制
        // ============================================================================

        void RippleWidget::paintEvent(QPaintEvent* event) {
            Q_UNUSED(event);  // 水波由主窗口 paintEvent 背景层统一渲染，避免双重绘制
        }

        void RippleWidget::render(QPainter& p, const QPointF& offset) {
            if (ripples_.empty()) return;

            auto now = std::chrono::steady_clock::now();

            const double lifetime = 1.2;

            for (const auto& r : ripples_) {
                double elapsed = std::chrono::duration<double>(
                    now - r.startTime).count();

                double t = elapsed / lifetime;

                if (t < 0.0 || t > 1.0) continue;

                // 半径
                double radius = t * r.maxRadius * strength_;

                // 透明度（先增后减）
                double alpha = 1.0 - t;
                alpha = alpha * alpha;

                // 绘制多层波纹
                for (int i = 0; i < 3; ++i) {
                    double layerRadius = radius - i * 15.0;

                    if (layerRadius <= 0) continue;

                    double layerAlpha = alpha * (1.0 - i * 0.25);

                    QColor c = r.color;
                    c.setAlphaF(layerAlpha * 0.5f * strength_);

                    p.setPen(QPen(c, 2.0 - i * 0.5));
                    p.setBrush(Qt::NoBrush);

                    p.drawEllipse(r.center + offset, layerRadius, layerRadius);
                }
            }
        }

        void RippleWidget::resizeEvent(QResizeEvent* event) {
            QWidget::resizeEvent(event);
            update();
        }

    } // namespace UI
} // namespace Lingjing
