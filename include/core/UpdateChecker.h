#pragma once

#include <QObject>
#include <QString>

namespace Lingjing {
namespace Core {

// 更新检查器：从配置的清单地址（本地文件或线上 URL）读取版本信息，
// 与当前版本比较，发现新版本时发出 updateAvailable 信号。
class UpdateChecker : public QObject {
    Q_OBJECT
public:
    explicit UpdateChecker(QObject* parent = nullptr);

    // 异步检查更新；受 Config.general.check_updates 开关与 update_url 地址控制
    void checkForUpdates();

signals:
    void updateAvailable(const QString& latestVersion, const QString& downloadUrl,
        const QString& notes);
    void upToDate(const QString& latestVersion);

private:
    static bool versionIsNewer(const QString& latest, const QString& current);
};

} // namespace Core
} // namespace Lingjing
