#pragma once

#include <QWidget>
#include <QPointF>
#include <deque>
#include <chrono>

class QPainter;

namespace Lingjing {
    namespace UI {

        // ============================================================================
        // 水波触动效果
        // ============================================================================

        class RippleWidget : public QWidget {
            Q_OBJECT

        public:
            explicit RippleWidget(QWidget* parent = nullptr);
            ~RippleWidget() override;

            // 触发水波
            void triggerRipple(const QPointF& center);

            // 强度

            // 渲染水波（供父窗口背景层调用）
            void render(QPainter& p, const QPointF& offset);
            void setStrength(float strength) { strength_ = strength; }
            float strength() const { return strength_; }

        signals:
            void rippleTriggered(const QPoint& position);
            // 动画帧更新（供主窗口背景层统一重绘）
            void frameUpdated();

        protected:
            void paintEvent(QPaintEvent* event) override;
            void mousePressEvent(QMouseEvent* event) override;
            void resizeEvent(QResizeEvent* event) override;

        private slots:
            void onUpdateTimer();

        private:
            struct Ripple {
                QPointF center;
                std::chrono::steady_clock::time_point startTime;
                QColor color;
                float maxRadius;
            };

            void updateRipples();

            std::deque<Ripple> ripples_;
            float strength_ = 0.8f;
            class QTimer* updateTimer_ = nullptr;
        };

    } // namespace UI
} // namespace Lingjing
