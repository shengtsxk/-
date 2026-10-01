#include "ui/GameListWidget.h"
#include "ui/Theme.h"
#include "learning/LearningManager.h"
#include "capture/WindowEnumerator.h"
#include "core/Logger.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QTimer>
#include <QPainter>
#include <QPainterPath>

#ifdef _WIN32
#include <windows.h>
#endif

namespace Lingjing {
    namespace UI {

        // ============================================================================
        // 构造/析构
        // ============================================================================

        GameListWidget::GameListWidget(QWidget* parent)
            : QWidget(parent)
        {
            setAttribute(Qt::WA_TranslucentBackground);
            setupUi();
            applyTheme();

            // 刷新定时器
            refreshTimer_ = new QTimer(this);
            connect(refreshTimer_, &QTimer::timeout,
                this, &GameListWidget::onRefreshTimer);
            refreshTimer_->start(2000);

            refreshList();
        }

        GameListWidget::~GameListWidget() {
            if (refreshTimer_) {
                refreshTimer_->stop();
            }
        }

        // ============================================================================
        // UI
        // ============================================================================

        void GameListWidget::setupUi() {
            QVBoxLayout* layout = new QVBoxLayout(this);
            layout->setContentsMargins(16, 16, 16, 16);
            layout->setSpacing(12);

            // 标题
            titleLabel_ = new QLabel("运行中的游戏", this);
            titleLabel_->setStyleSheet(
                "color: #E8ECF2;"
                "font-size: 14px;"
                "font-weight: bold;");
            layout->addWidget(titleLabel_);

            // 搜索框
            searchBox_ = new QLineEdit(this);
            searchBox_->setPlaceholderText("搜索游戏...");
            searchBox_->setClearButtonEnabled(true);
            layout->addWidget(searchBox_);

            // 列表
            listWidget_ = new QListWidget(this);
            listWidget_->setSelectionMode(QAbstractItemView::SingleSelection);
            listWidget_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            listWidget_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
            layout->addWidget(listWidget_, 1);

            // 连接
            connect(searchBox_, &QLineEdit::textChanged,
                this, &GameListWidget::onSearchChanged);

            connect(listWidget_, &QListWidget::itemClicked,
                this, &GameListWidget::onItemClicked);
        }

        // ============================================================================
        // 主题
        // ============================================================================

        void GameListWidget::applyTheme() {
            // 面板控件透明度（0-100）
            const int alpha = Theme::instance().panelOpacity();

            QColor containerColor(0x14, 0x18, 0x20, alpha * 255 / 100);
            QColor itemColor(0x1A, 0x1E, 0x28, alpha * 255 / 100);
            QColor hoverColor(0x22, 0x28, 0x32, alpha * 255 / 100);

            setStyleSheet(QString(
                "GameListWidget {"
                "  background: transparent;"
                "  border-right: 1px solid #2A3240;"
                "}"
                "QLineEdit {"
                "  background-color: %2;"
                "  color: #E8ECF2;"
                "  border: 1px solid #2A3240;"
                "  border-radius: 6px;"
                "  padding: 8px 12px;"
                "  font-size: 12px;"
                "}"
                "QLineEdit:focus {"
                "  border-color: #4A9EFF;"
                "}"
                "QListWidget {"
                "  background-color: transparent;"
                "  border: none;"
                "  outline: none;"
                "  color: #E8ECF2;"
                "}"
                "QListWidget::viewport, QAbstractScrollArea::viewport {"
                "  background: transparent;"
                "}"
                "QListWidget::item {"
                "  background-color: %3;"
                "  border-radius: 8px;"
                "  padding: 12px;"
                "  margin: 2px 0;"
                "  border: 1px solid transparent;"
                "}"
                "QListWidget::item:hover {"
                "  background-color: %3;"
                "  border-color: #2A3240;"
                "}"
                "QListWidget::item:selected {"
                "  background-color: #1E3048;"
                "  border-color: #4A9EFF;"
                "}"
            ).arg(containerColor.name(QColor::HexArgb))
             .arg(itemColor.name(QColor::HexArgb))
             .arg(hoverColor.name(QColor::HexArgb)));
        }

        // ============================================================================
        // 刷新列表
        // ============================================================================

        void GameListWidget::refreshList() {
            // 保存当前选择
            uint64_t currentHash = 0;

            QListWidgetItem* currentItem = listWidget_->currentItem();

            if (currentItem) {
                currentHash = currentItem->data(Qt::UserRole).toULongLong();
            }

            listWidget_->clear();
            games_.clear();

            // 枚举窗口
            auto windows = WindowEnumerator::enumerate();

            for (const auto& w : windows) {
                // 过滤
                if (!w.isRenderable) continue;
                if (w.frameSize.width < 640) continue;
                if (w.frameSize.height < 480) continue;

                GameEntry entry;
                entry.hash = std::hash<std::wstring>{}(
                    w.processPath.empty() ? w.title : w.processPath);
                entry.name = QString::fromStdWString(w.title);
                entry.exeName = QString::fromStdWString(w.processName);
                entry.hwnd = w.hwnd;
                entry.isActive = w.foreground;
                entry.confidence = 0.0f;

                games_.push_back(entry);

                // 创建列表项
                QListWidgetItem* item = new QListWidgetItem();

                QString displayText;

                if (entry.isActive) {
                    displayText = "▶ " + entry.name;
                }
                else {
                    displayText = entry.name;
                }

                item->setText(displayText);
                item->setData(Qt::UserRole, QVariant::fromValue(entry.hash));
                item->setData(Qt::UserRole + 1, QVariant::fromValue<void*>(entry.hwnd));
                item->setData(Qt::UserRole + 2, entry.name);

                // 大小信息
                item->setToolTip(QString("%1\n%2\n%3x%4")
                    .arg(entry.name)
                    .arg(entry.exeName)
                    .arg(w.frameSize.width)
                    .arg(w.frameSize.height));

                // 高亮
                if (entry.isActive) {
                    item->setForeground(QColor(0x4A, 0x9E, 0xFF));
                }

                listWidget_->addItem(item);

                // 恢复选择
                if (entry.hash == currentHash) {
                    listWidget_->setCurrentItem(item);
                }
            }

            // 更新标题
            titleLabel_->setText(QString("运行中的游戏 (%1)")
                .arg(games_.size()));
        }

        // ============================================================================
        // 槽函数
        // ============================================================================

        void GameListWidget::onItemClicked(QListWidgetItem* item) {
            if (!item) return;

            uint64_t hash = item->data(Qt::UserRole).toULongLong();
            void* hwnd = item->data(Qt::UserRole + 1).value<void*>();
            QString name = item->data(Qt::UserRole + 2).toString();

            emit gameSelected(hash, name, hwnd);
        }

        void GameListWidget::onSearchChanged(const QString& text) {
            for (int i = 0; i < listWidget_->count(); ++i) {
                QListWidgetItem* item = listWidget_->item(i);

                bool visible = text.isEmpty() ||
                    item->text().contains(text, Qt::CaseInsensitive);

                item->setHidden(!visible);
            }
        }

        void GameListWidget::onRefreshTimer() {
            refreshList();
        }

    } // namespace UI
} // namespace Lingjing
