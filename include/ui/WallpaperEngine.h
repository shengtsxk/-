#pragma once

#include <QWidget>
#include <QImage>
#include <QMovie>
#include <QTimer>
#include <chrono>

class QPainter;

namespace Lingjing {
    namespace UI {

        // ============================================================================
        // 动态壁纸引擎
        // ============================================================================

        class WallpaperEngine : public QWidget {
            Q_OBJECT

        public:
            explicit WallpaperEngine(QWidget* parent = nullptr);
            ~WallpaperEngine() override;

            // 加载壁纸
            bool loadImage(const QString& path);
            bool loadMovie(const QString& path);

            // 使用内置生成式壁纸
            void useProcedural(int seed = 0);

            // 停止

            // 渲染到指定区域（供父窗口背景层调用）
            void render(QPainter& p, const QRect& target);
            void stop();

        signals:
            // 动画帧更新（供主窗口背景层统一重绘，避免子控件与背景层双重绘制）
            void frameUpdated();

        protected:
            void paintEvent(QPaintEvent* event) override;
            void resizeEvent(QResizeEvent* event) override;

        private slots:
            void onMovieFrame();
            void onProceduralTimer();

        private:
            enum class Mode {
                None,
                Image,
                Movie,
                Procedural,
            };

            void drawProcedural(QPainter& p, const QRect& r);

            Mode mode_ = Mode::None;

            // 图像
            QImage image_;
            QImage scaledImage_;
            QMovie* movie_ = nullptr;

            // 生成式
            class QTimer* proceduralTimer_ = nullptr;
            double proceduralTime_ = 0.0;
            int proceduralSeed_ = 0;
        };

    } // namespace UI
} // namespace Lingjing
