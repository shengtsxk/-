import io, sys
p = r"D:\lingjing\src\main.cpp"
with io.open(p, "r", encoding="utf-8") as f:
    s = f.read()

old = """            [&window](const QString& version, const QString& url,
                const QString& notes) {
                QString text = QStringLiteral("发现新版本 %1").arg(version);
                if (!notes.isEmpty()) text += QStringLiteral("\\n\\n%1").arg(notes);
                const auto r = QMessageBox::question(&window,
                    QStringLiteral("灵境 - 发现新版本"), text,
                    QMessageBox::Yes | QMessageBox::No);
                if (r == QMessageBox::Yes && !url.isEmpty()) {
                    QDesktopServices::openUrl(QUrl(url));
                }
            });"""

new = """            [&window](const QString& version, const QString& url,
                const QString& notes) {
                // 自定义暗色更新对话框（与灵境深色主题一致，避免系统默认配色）
                QDialog dlg(&window);
                dlg.setWindowTitle(QStringLiteral("灵境 - 发现新版本"));
                dlg.setModal(true);
                dlg.setFixedWidth(420);
                dlg.setStyleSheet(
                    "QDialog { background-color: #141920; }"
                    "QLabel { background: transparent; }"
                    "QPushButton { background-color: #1A2029; color: #E8ECF2;"
                    "  border: 1px solid #2A3240; border-radius: 6px;"
                    "  padding: 8px 22px; font-size: 12px; }"
                    "QPushButton:hover { border-color: #4A9EFF; }"
                    "QPushButton#dl { background-color: #4A9EFF; color: white;"
                    "  border: none; }"
                    "QPushButton#dl:hover { background-color: #5AAEFF; }");

                auto* lay = new QVBoxLayout(&dlg);
                lay->setContentsMargins(20, 16, 20, 16);
                lay->setSpacing(10);

                auto* title = new QLabel(
                    QStringLiteral("发现新版本 %1").arg(version), &dlg);
                title->setStyleSheet(
                    "font-size: 15px; font-weight: bold; color: #E8ECF2;");
                lay->addWidget(title);

                if (!notes.isEmpty()) {
                    auto* body = new QLabel(notes, &dlg);
                    body->setWordWrap(true);
                    body->setStyleSheet("font-size: 12px; color: #9AA4B4;");
                    lay->addWidget(body);
                }

                auto* btns = new QHBoxLayout();
                btns->addStretch();
                auto* laterBtn = new QPushButton(QStringLiteral("以后再说"), &dlg);
                auto* dlBtn = new QPushButton(QStringLiteral("下载更新"), &dlg);
                dlBtn->setObjectName("dl");
                if (url.isEmpty()) dlBtn->setEnabled(false);
                QObject::connect(dlBtn, &QPushButton::clicked, &dlg, [&dlg, url]() {
                    if (!url.isEmpty()) QDesktopServices::openUrl(QUrl(url));
                    dlg.accept();
                });
                QObject::connect(laterBtn, &QPushButton::clicked, &dlg,
                    &QDialog::reject);
                btns->addWidget(laterBtn);
                btns->addWidget(dlBtn);
                lay->addLayout(btns);

                dlg.exec();
            });"""

if old in s:
    s = s.replace(old, new)
    # add includes
    inc_old = "#include <QMessageBox>"
    inc_new = "#include <QMessageBox>\n#include <QDialog>\n#include <QVBoxLayout>\n#include <QHBoxLayout>\n#include <QLabel>\n#include <QPushButton>"
    if "#include <QDialog>" not in s:
        s = s.replace(inc_old, inc_new, 1)
    with io.open(p, "w", encoding="utf-8", newline="") as f:
        f.write(s)
    print("update dialog replaced + includes added")
else:
    print("anchor not found")
