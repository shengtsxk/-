#pragma once

#include <QWidget>
#include <QListWidget>
#include <memory>

class QLabel;
class QLineEdit;
class QTimer;
class QVBoxLayout;

namespace Lingjing {
    namespace UI {

        class GameListWidget : public QWidget {
            Q_OBJECT

        public:
            explicit GameListWidget(QWidget* parent = nullptr);
            void applyTheme();
            ~GameListWidget() override;

        signals:
            void gameSelected(uint64_t hash, const QString& name, void* hwnd);
            void refreshRequested();

        private slots:
            void onItemClicked(QListWidgetItem* item);
            void onSearchChanged(const QString& text);
            void onRefreshTimer();

        private:
            void setupUi();
            void refreshList();

            struct GameEntry {
                uint64_t hash;
                QString name;
                QString exeName;
                void* hwnd;
                bool isActive;
                float confidence;
            };

            std::vector<GameEntry> games_;

            QLabel* titleLabel_ = nullptr;
            QLineEdit* searchBox_ = nullptr;
            QListWidget* listWidget_ = nullptr;
            QTimer* refreshTimer_ = nullptr;
        };

    } // namespace UI
} // namespace Lingjing
