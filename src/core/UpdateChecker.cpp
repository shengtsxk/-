#include "core/UpdateChecker.h"

#include "core/Config.h"
#include "core/Logger.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QUrl>

namespace Lingjing {
namespace Core {

UpdateChecker::UpdateChecker(QObject* parent)
    : QObject(parent)
{
}

void UpdateChecker::checkForUpdates()
{
    QSettings settings;
    const bool enabled = settings.value("check_updates", true).toBool();
    if (!enabled) {
        LOG_INFO("Update check skipped (disabled in settings)");
        return;
    }
    const QString url = settings.value("update_url",
        QStringLiteral("https://api.github.com/repos/shengtsxk/-/contents/version.json"))
        .toString();
    if (url.trimmed().isEmpty()) {
        LOG_INFO("Update check skipped (no update url configured)");
        return;
    }

    auto* nam = new QNetworkAccessManager(this);
    QNetworkRequest req{ QUrl(url) };
    req.setTransferTimeout(8000);
    req.setRawHeader("User-Agent", "Lingjing/" LINGJING_VERSION_STRING);
    QNetworkReply* reply = nam->get(req);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            LOG_WARN("Update check failed: %s",
                reply->errorString().toUtf8().constData());
            return;
        }
        const QByteArray data = reply->readAll();
        QJsonParseError perr;
        QJsonDocument doc = QJsonDocument::fromJson(data, &perr);
        QString latest, downloadUrl, notes;
        if (perr.error == QJsonParseError::NoError && doc.isObject() &&
            doc.object().contains(QStringLiteral("content"))) {
            // GitHub Contents API：正文为 Base64，需先解码
            const QByteArray decoded = QByteArray::fromBase64(
                doc.object().value("content").toString().toUtf8());
            QJsonDocument d2 = QJsonDocument::fromJson(decoded, &perr);
            if (perr.error != QJsonParseError::NoError || !d2.isObject()) {
                LOG_WARN("Update manifest decode failed");
                return;
            }
            QJsonObject obj2 = d2.object();
            latest = obj2.value("latest_version").toString();
            downloadUrl = obj2.value("download_url").toString();
            notes = obj2.value("notes").toString();
        } else {
            if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
                LOG_WARN("Update manifest parse failed");
                return;
            }
            QJsonObject obj = doc.object();
            latest = obj.value("latest_version").toString();
            downloadUrl = obj.value("download_url").toString();
            notes = obj.value("notes").toString();
        }
        if (latest.isEmpty()) {
            LOG_WARN("Update manifest missing latest_version");
            return;
        }

        const QString current = QStringLiteral(LINGJING_VERSION_STRING);
        if (versionIsNewer(latest, current)) {
            LOG_INFO("Update available: %s -> %s",
                current.toUtf8().constData(), latest.toUtf8().constData());
            emit updateAvailable(latest, downloadUrl, notes);
        } else {
            LOG_INFO("Up to date (latest %s)", latest.toUtf8().constData());
            emit upToDate(latest);
        }
    });
}

bool UpdateChecker::versionIsNewer(const QString& latest, const QString& current)
{
    auto parse = [](const QString& s) {
        QVector<int> v(3, 0);
        const QStringList parts = s.split('.');
        for (int i = 0; i < parts.size() && i < 3; ++i) {
            bool ok = false;
            const int n = parts[i].toInt(&ok);
            if (ok) v[i] = n;
        }
        return v;
    };
    const QVector<int> a = parse(latest);
    const QVector<int> b = parse(current);
    for (int i = 0; i < 3; ++i) {
        if (a[i] != b[i]) return a[i] > b[i];
    }
    return false;
}

} // namespace Core
} // namespace Lingjing
