p = r"D:\lingjing\src\ui\MainWindow.cpp"
s = open(p, encoding="utf-8").read()

old = """            contentArea_->setStyleSheet("background: transparent;");

            QHBoxLayout* layout = new QHBoxLayout(contentArea_);
            layout->setContentsMargins(0, 0, 0, 0);
            layout->setSpacing(0);

            // 左侧面板：游戏列表
            gameList_ = new GameListWidget(this);
            gameList_->setFixedWidth(320);
            layout->addWidget(gameList_);
"""
new = """            contentArea_->setStyleSheet("background: transparent;");

            QVBoxLayout* layout = new QVBoxLayout(contentArea_);
            layout->setContentsMargins(0, 0, 0, 0);
            layout->setSpacing(0);

            // 主内容 / 设置面板 双页切换（点击设置不弹独立窗口，直接内嵌跳转）
            contentStack_ = new QStackedWidget(contentArea_);
            contentStack_->setStyleSheet("background: transparent;");
            contentStack_->setFrameShape(QFrame::NoFrame);

            QWidget* mainPage = new QWidget(contentStack_);
            mainPage->setStyleSheet("background: transparent;");
            QHBoxLayout* mainLayout = new QHBoxLayout(mainPage);
            mainLayout->setContentsMargins(0, 0, 0, 0);
            mainLayout->setSpacing(0);

            // 左侧面板：游戏列表
            gameList_ = new GameListWidget(mainPage);
            gameList_->setFixedWidth(320);
            mainLayout->addWidget(gameList_);
"""
if old in s:
    s = s.replace(old, new, 1); print("setup content start fixed")
else:
    print("MISS content start")

# rightContainer parent 指向 mainPage，layout 归属 mainLayout
old2 = """            // 中间+右侧容器
            QWidget* rightContainer = new QWidget(this);
            rightContainer->setStyleSheet("background: transparent;");
            QVBoxLayout* rightLayout = new QVBoxLayout(rightContainer);
"""
new2 = """            // 中间+右侧容器
            QWidget* rightContainer = new QWidget(mainPage);
            rightContainer->setStyleSheet("background: transparent;");
            QVBoxLayout* rightLayout = new QVBoxLayout(rightContainer);
"""
if old2 in s:
    s = s.replace(old2, new2, 1); print("right container fixed")
else:
    print("MISS right container")

old3 = """            rightLayout->addWidget(perfGraph_);

            layout->addWidget(rightContainer, 1);
        }
"""
new3 = """            rightLayout->addWidget(perfGraph_);

            mainLayout->addWidget(rightContainer, 1);

            contentStack_->addWidget(mainPage);

            // 设置面板（内嵌）
            settingsPanel_ = new SettingsDialog(contentStack_);
            settingsPanel_->setEmbedded(true);
            contentStack_->addWidget(settingsPanel_);

            layout->addWidget(contentStack_);
        }
"""
if old3 in s:
    s = s.replace(old3, new3, 1); print("content end fixed")
else:
    print("MISS content end")
open(p, "w", encoding="utf-8", newline="").write(s)
