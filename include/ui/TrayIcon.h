#pragma once

#include <QSystemTrayIcon>
#include <QMenu>
#include <memory>

namespace Lingjing {
    namespace UI {

        class TrayIcon : public QSystemTrayIcon {
            Q_OBJECT

        public:
            explicit TrayIcon(QWidget* parent = nullptr);
            ~TrayIcon() override;

            void showMessage(const QString& title, const QString& message,
                int durationMs = 3000);

        signals:
            void showMainWindowRequested();
            void toggleFrameGenRequested();
            void stopFrameGenRequested();
            void exitRequested();

        private slots:
            void onActivated(ActivationReason reason);

        private:
            void setupUi();
            QIcon createIcon();

            QMenu* menu_ = nullptr;
            QWidget* parent_ = nullptr;
        };

    } // namespace UI
} // namespace Lingjing