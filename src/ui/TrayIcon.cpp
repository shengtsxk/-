#include "ui/TrayIcon.h"
#include "ui/MainWindow.h"
#include "core/Logger.h"

#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace Lingjing {
    namespace UI {

        // ============================================================================
        // 构造/析构
        // ============================================================================

        TrayIcon::TrayIcon(QWidget* parent)
            : QSystemTrayIcon(parent)
            , parent_(parent)
        {
            setupUi();

            setIcon(createIcon());
            setToolTip("灵境 Lingjing — 通用游戏帧生成");

            LOG_INFO("System tray available: %d",
                QSystemTrayIcon::isSystemTrayAvailable() ? 1 : 0);
            show();
            LOG_INFO("Tray icon visible after show(): %d",
                isVisible() ? 1 : 0);
        }

        TrayIcon::~TrayIcon() {
            hide();
        }

        // ============================================================================
        // UI
        // ============================================================================

        void TrayIcon::setupUi() {
            menu_ = new QMenu();

            QAction* showAction = menu_->addAction("显示主窗口");
            connect(showAction, &QAction::triggered,
                this, &TrayIcon::showMainWindowRequested);

            menu_->addSeparator();

            QAction* toggleAction = menu_->addAction("开关帧生成");
            connect(toggleAction, &QAction::triggered,
                this, &TrayIcon::toggleFrameGenRequested);

            QAction* stopAction = menu_->addAction("停止帧生成");
            connect(stopAction, &QAction::triggered,
                this, &TrayIcon::stopFrameGenRequested);

            menu_->addSeparator();

            QAction* exitAction = menu_->addAction("退出");
            connect(exitAction, &QAction::triggered,
                this, &TrayIcon::exitRequested);

            setContextMenu(menu_);

            connect(this, &QSystemTrayIcon::activated,
                this, &TrayIcon::onActivated);
        }

        QIcon TrayIcon::createIcon() {
            // 优先加载青鸾图标
            QIcon qingluan("assets/icons/qingluan.png");
            if (!qingluan.isNull()) {
                return qingluan;
            }

            QPixmap pixmap(64, 64);
            pixmap.fill(Qt::transparent);

            QPainter p(&pixmap);
            p.setRenderHint(QPainter::Antialiasing);

            // 圆角矩形背景
            QPainterPath path;
            path.addRoundedRect(QRectF(4, 4, 56, 56), 14, 14);

            QLinearGradient grad(0, 0, 64, 64);
            grad.setColorAt(0.0, QColor(0x4A, 0x9E, 0xFF));
            grad.setColorAt(1.0, QColor(0x8A, 0x5A, 0xFF));

            p.fillPath(path, grad);

            // "灵" 字
            p.setPen(Qt::white);

            QFont font("Microsoft YaHei UI", 28, QFont::Bold);
            p.setFont(font);

            p.drawText(pixmap.rect(), Qt::AlignCenter, "灵");

            return QIcon(pixmap);
        }

        // ============================================================================
        // 事件
        // ============================================================================

        void TrayIcon::onActivated(ActivationReason reason) {
            if (reason == Trigger || reason == DoubleClick) {
                emit showMainWindowRequested();
            }
        }

        // ============================================================================
        // 消息
        // ============================================================================

        void TrayIcon::showMessage(const QString& title, const QString& message,
            int durationMs)
        {
            QSystemTrayIcon::showMessage(title, message,
                QSystemTrayIcon::Information,
                durationMs);
        }

    } // namespace UI
} // namespace Lingjing
