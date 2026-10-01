#include "ui/WallpaperEngine.h"

#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QLinearGradient>
#include <QRadialGradient>
#include <QResizeEvent>
#include <cmath>
#include <random>
#include "core/Logger.h"

namespace Lingjing {
    namespace UI {

        // ============================================================================
        // 构造/析构
        // ============================================================================

        WallpaperEngine::WallpaperEngine(QWidget* parent)
            : QWidget(parent)
        {
            setAttribute(Qt::WA_OpaquePaintEvent);
            setAttribute(Qt::WA_TransparentForMouseEvents);

            proceduralTimer_ = new QTimer(this);
            connect(proceduralTimer_, &QTimer::timeout,
                this, &WallpaperEngine::onProceduralTimer);

            // 默认使用生成式壁纸
            useProcedural();
        }

        WallpaperEngine::~WallpaperEngine() {
            stop();
        }

        // ============================================================================
        // 加载
        // ============================================================================

        bool WallpaperEngine::loadImage(const QString& path) {
            stop();

            if (!image_.load(path)) return false;

            mode_ = Mode::Image;

            // 缩放
            if (!image_.isNull()) {
            }

            update();
            return true;
        }

        bool WallpaperEngine::loadMovie(const QString& path) {
            stop();

            movie_ = new QMovie(path, QByteArray(), this);

            if (!movie_->isValid()) {
                delete movie_;
                movie_ = nullptr;
                return false;
            }

            connect(movie_, &QMovie::frameChanged,
                this, &WallpaperEngine::onMovieFrame);

            movie_->start();

            mode_ = Mode::Movie;

            return true;
        }

        void WallpaperEngine::useProcedural(int seed) {
            stop();

            proceduralSeed_ = seed;
            proceduralTime_ = 0.0;
            mode_ = Mode::Procedural;

            proceduralTimer_->start(33);  // 30 FPS

            update();
        }

        void WallpaperEngine::stop() {
            if (movie_) {
                movie_->stop();
                delete movie_;
                movie_ = nullptr;
            }

            if (proceduralTimer_) {
                proceduralTimer_->stop();
            }

            mode_ = Mode::None;
        }

        // ============================================================================
        // 更新
        // ============================================================================

        void WallpaperEngine::onMovieFrame() {
            emit frameUpdated();
        }

        void WallpaperEngine::onProceduralTimer() {
            proceduralTime_ += 0.033;
            emit frameUpdated();
        }

        // ============================================================================
        // 绘制（空实现：壁纸由主窗口 paintEvent 背景层统一渲染，避免双重绘制造成错位/接缝）
        // ============================================================================

        void WallpaperEngine::paintEvent(QPaintEvent* event) {
            Q_UNUSED(event);
        }

        // ============================================================================
        // 生成式壁纸
        // ============================================================================

        void WallpaperEngine::drawProcedural(QPainter& p, const QRect& r) {

            double t = proceduralTime_;

            // 主背景渐变
            QLinearGradient bg(0, 0, r.width(), r.height());

            double phase = std::sin(t * 0.1) * 0.5 + 0.5;

            QColor c1(
                static_cast<int>(10 + 15 * phase),
                static_cast<int>(15 + 20 * phase),
                static_cast<int>(25 + 30 * phase)
            );

            QColor c2(
                static_cast<int>(5 + 10 * (1 - phase)),
                static_cast<int>(8 + 15 * (1 - phase)),
                static_cast<int>(15 + 20 * (1 - phase))
            );

            bg.setColorAt(0.0, c1);
            bg.setColorAt(1.0, c2);

            p.fillRect(r, bg);

            // 动态光晕
            {
                // 光晕 1
                double angle1 = t * 0.3;

                double cx1 = r.width() * (0.3 + 0.2 * std::sin(angle1));
                double cy1 = r.height() * (0.4 + 0.15 * std::cos(angle1 * 1.3));

                double radius1 = r.width() * 0.5;

                QRadialGradient grad1(cx1, cy1, radius1);

                QColor halo1(0x4A, 0x9E, 0xFF);
                halo1.setAlpha(60);
                grad1.setColorAt(0.0, halo1);

                QColor haloTransparent = halo1;
                haloTransparent.setAlpha(0);
                grad1.setColorAt(1.0, haloTransparent);

                p.fillRect(r, grad1);

                // 光晕 2
                double angle2 = t * -0.25 + 1.0;

                double cx2 = r.width() * (0.7 + 0.2 * std::cos(angle2));
                double cy2 = r.height() * (0.6 + 0.15 * std::sin(angle2 * 1.5));

                double radius2 = r.width() * 0.45;

                QRadialGradient grad2(cx2, cy2, radius2);

                QColor halo2(0x8A, 0x5A, 0xFF);
                halo2.setAlpha(50);
                grad2.setColorAt(0.0, halo2);

                QColor halo2Transparent = halo2;
                halo2Transparent.setAlpha(0);
                grad2.setColorAt(1.0, halo2Transparent);

                p.fillRect(r, grad2);
            }

            // 波动线条
            {
                p.setRenderHint(QPainter::Antialiasing);

                const int numWaves = 4;

                for (int w = 0; w < numWaves; ++w) {
                    double wavePhase = t * 0.5 + w * 0.7;
                    double amplitude = 20 + 10 * std::sin(t * 0.3 + w);
                    double yBase = r.height() * (0.3 + 0.15 * w);

                    QPainterPath path;

                    bool first = true;

                    for (int x = 0; x <= r.width(); x += 4) {
                        double y = yBase +
                            amplitude * std::sin(x * 0.01 + wavePhase) +
                            amplitude * 0.5 * std::sin(x * 0.025 - wavePhase * 1.3);

                        if (first) {
                            path.moveTo(x, y);
                            first = false;
                        }
                        else {
                            path.lineTo(x, y);
                        }
                    }

                    QColor waveColor(0x4A, 0x9E, 0xFF);
                    waveColor.setAlpha(30 - w * 5);

                    p.setPen(QPen(waveColor, 1.5));
                    p.drawPath(path);
                }
            }

            // 星尘粒子
            {
                static std::mt19937 rng(42);
                static std::uniform_real_distribution<double> distX(0.0, 1.0);
                static std::uniform_real_distribution<double> distY(0.0, 1.0);
                static std::uniform_real_distribution<double> distS(0.5, 2.0);

                // 静态粒子（不会重新生成）
                static std::vector<std::array<double, 3>> particles;

                if (particles.empty()) {
                    for (int i = 0; i < 100; ++i) {
                        particles.push_back({
                            distX(rng),
                            distY(rng),
                            distS(rng)
                            });
                    }
                }

                for (const auto& pt : particles) {
                    double px = pt[0] * r.width();
                    double py = pt[1] * r.height();

                    // 上下浮动
                    py += 5 * std::sin(t * 0.5 + px * 0.01);

                    double alpha = 0.3 + 0.3 * std::sin(t * 1.0 + px * 0.02 + py * 0.01);

                    QColor starColor(0xE8, 0xEC, 0xF2);
                    starColor.setAlphaF(alpha * 0.6);

                    p.setPen(Qt::NoPen);
                    p.setBrush(starColor);

                    double size = pt[2];

                    p.drawEllipse(QPointF(px, py), size, size);
                }
            }

            // 顶部和底部渐隐
            {
                QLinearGradient topFade(0, 0, 0, r.height() * 0.2);
                topFade.setColorAt(0.0, QColor(0x0F, 0x12, 0x18, 200));
                topFade.setColorAt(1.0, QColor(0x0F, 0x12, 0x18, 0));

                p.fillRect(QRect(0, 0, r.width(), static_cast<int>(r.height() * 0.2)),
                    topFade);

                QLinearGradient bottomFade(0, r.height() * 0.8, 0, r.height());
                bottomFade.setColorAt(0.0, QColor(0x0F, 0x12, 0x18, 0));
                bottomFade.setColorAt(1.0, QColor(0x0F, 0x12, 0x18, 200));

                p.fillRect(QRect(0, static_cast<int>(r.height() * 0.8),
                    r.width(), static_cast<int>(r.height() * 0.2)),
                    bottomFade);
            }
        }

        void WallpaperEngine::resizeEvent(QResizeEvent* event) {
            QWidget::resizeEvent(event);
            // 高清渲染：render 时按原图实时缩放，无需预缩放
        }


        void WallpaperEngine::render(QPainter& p, const QRect& target) {
            switch (mode_) {
            case Mode::Image:
                if (!image_.isNull()) {
                    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

                    // 保持比例铺满并裁剪到目标区域（不变形、清晰）
                    QSize scaled = image_.size();
                    scaled.scale(target.size(), Qt::KeepAspectRatioByExpanding);
                    QRect dest(QPoint(0, 0), scaled);
                    dest.moveCenter(target.center());

                    QRect clipped = dest.intersected(target);
                    if (clipped.isEmpty()) break;

                    const double sx = (clipped.x() - dest.x()) * image_.width() / double(dest.width());
                    const double sy = (clipped.y() - dest.y()) * image_.height() / double(dest.height());
                    const double sw = clipped.width() * image_.width() / double(dest.width());
                    const double sh = clipped.height() * image_.height() / double(dest.height());
                    p.drawImage(clipped, image_, QRectF(sx, sy, sw, sh));
                }
                break;
            case Mode::Movie:
                if (movie_ && movie_->isValid()) {
                    QPixmap pix = movie_->currentPixmap();

                    if (!pix.isNull()) {
                        p.setRenderHint(QPainter::SmoothPixmapTransform, true);

                        // 保持比例铺满并裁剪（不变形）
                        QSize scaled = pix.size();
                        scaled.scale(target.size(), Qt::KeepAspectRatioByExpanding);
                        QRect dest(QPoint(0, 0), scaled);
                        dest.moveCenter(target.center());

                        QRect clipped = dest.intersected(target);
                        if (clipped.isEmpty()) break;

                        const double sx = (clipped.x() - dest.x()) * pix.width() / double(dest.width());
                        const double sy = (clipped.y() - dest.y()) * pix.height() / double(dest.height());
                        const double sw = clipped.width() * pix.width() / double(dest.width());
                        const double sh = clipped.height() * pix.height() / double(dest.height());
                        p.drawPixmap(clipped, pix, QRectF(sx, sy, sw, sh));
                    }
                }
                break;
            case Mode::Procedural:
                drawProcedural(p, target);
                break;

            default:
                p.fillRect(target, QColor(0x14, 0x18, 0x20));
                break;
            }
        }

    } // namespace UI
} // namespace Lingjing
