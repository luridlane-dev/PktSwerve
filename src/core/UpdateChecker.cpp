#include "UpdateChecker.h"
#include "app/Logger.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSettings>
#include <QTimer>

namespace {

const char* const kLatestReleaseApi = "https://api.github.com/repos/luridlane-dev/PktSwerve/releases/latest";
const char* const kReleasesPage     = "https://github.com/luridlane-dev/PktSwerve/releases/latest";

bool isTrustedDownload(const QUrl& url)
{
    return url.scheme() == "https" && url.host() == "github.com"
        && url.path().startsWith("/luridlane-dev/PktSwerve/releases/download/");
}

constexpr qint64 kCheckIntervalSecs = 24 * 60 * 60;
constexpr int    kStartupDelayMs    = 10 * 1000;
constexpr int    kScheduleTickMs    = 60 * 60 * 1000;
constexpr int    kRequestTimeoutMs  = 15 * 1000;

const char* const kAutoCheckKey = "updates/autoCheck";
const char* const kLastCheckKey = "updates/lastCheck";

}

UpdateChecker::UpdateChecker(QObject* parent)
    : QObject(parent)
    , m_net(new QNetworkAccessManager(this))
    , m_timer(new QTimer(this))
{
    m_timer->setInterval(kScheduleTickMs);
    connect(m_timer, &QTimer::timeout, this, &UpdateChecker::maybeCheck);
}

QString UpdateChecker::platformAssetName()
{
#if defined(PLATFORM_WINDOWS)
    return "PktSwerve-win64.zip";
#elif defined(PLATFORM_MACOS)
    return "PktSwerve-macOS.dmg";
#else
    return {};
#endif
}

bool UpdateChecker::autoCheckEnabled() const
{
    return QSettings().value(kAutoCheckKey, true).toBool();
}

void UpdateChecker::setAutoCheckEnabled(bool enabled)
{
    QSettings().setValue(kAutoCheckKey, enabled);
    if (enabled)
        startAutoCheck();
    else
        m_timer->stop();
}

void UpdateChecker::startAutoCheck()
{
    if (!autoCheckEnabled())
        return;
    m_timer->start();
    QTimer::singleShot(kStartupDelayMs, this, &UpdateChecker::maybeCheck);
}

void UpdateChecker::maybeCheck()
{
    if (!autoCheckEnabled() || m_checking)
        return;
    const qint64 last = QSettings().value(kLastCheckKey, 0).toLongLong();
    if (QDateTime::currentSecsSinceEpoch() - last >= kCheckIntervalSecs)
        check(false);
}

void UpdateChecker::check(bool userInitiated)
{
    if (m_checking) {
        // A background check already running now also answers a manual one.
        m_userInitiated = m_userInitiated || userInitiated;
        return;
    }
    m_checking = true;
    m_userInitiated = userInitiated;

    QNetworkRequest req{QUrl(kLatestReleaseApi)};
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  "PktSwerve/" + QCoreApplication::applicationVersion().toUtf8());
    req.setTransferTimeout(kRequestTimeoutMs);

    LOG_INFO("Checking for updates (%1)", userInitiated ? "manual" : "background");
    QNetworkReply* reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, &UpdateChecker::onReply);
}

void UpdateChecker::onReply()
{
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply)
        return;
    reply->deleteLater();

    const bool userInitiated = m_userInitiated;
    m_checking = false;
    m_userInitiated = false;

    // 404 means no (non-draft, non-prerelease) release has been published yet.
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError && status != 404) {
        LOG_WARN("Update check failed: %1", reply->errorString());
        if (userInitiated)
            emit checkFailed(reply->errorString());
        return;
    }

    QSettings().setValue(kLastCheckKey, QDateTime::currentSecsSinceEpoch());

    QString tag;
    Release found;
    found.page = QUrl(QString(kReleasesPage));
    if (status != 404) {
        const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        tag = obj.value("tag_name").toString();
        const QUrl htmlUrl(obj.value("html_url").toString());
        if (htmlUrl.isValid() && htmlUrl.scheme() == "https" && htmlUrl.host() == "github.com")
            found.page = htmlUrl;
        found.notes = obj.value("body").toString();

        // Only files published by this repository's releases are offered for install.
        const QString wanted = platformAssetName();
        for (const QJsonValue& value : obj.value("assets").toArray()) {
            const QJsonObject asset = value.toObject();
            const QString name = asset.value("name").toString();
            const QUrl url(asset.value("browser_download_url").toString());
            if (!isTrustedDownload(url))
                continue;
            if (!wanted.isEmpty() && name == wanted) {
                found.assetName = name;
                found.assetUrl = url;
            } else if (name == "SHA256SUMS") {
                found.checksumsUrl = url;
            }
        }
        if (tag.isEmpty()) {
            LOG_WARN0("Update check: reply has no tag_name");
            if (userInitiated)
                emit checkFailed(tr("Unexpected reply from GitHub."));
            return;
        }
    }

    const QString current = QCoreApplication::applicationVersion();
    if (!tag.isEmpty() && isNewer(tag, current)) {
        found.version = tag.startsWith('v') ? tag.mid(1) : tag;
        m_available = found;
        LOG_INFO("Update available: %1 (running %2, installable: %3)", found.version, current,
                 found.canInstall() ? "yes" : "no");
        emit updateAvailable(m_available, userInitiated);
    } else {
        m_available = Release();
        LOG_INFO("No update (latest %1, running %2)", tag.isEmpty() ? "none" : tag, current);
        if (userInitiated)
            emit upToDate();
    }
}

QVector<int> UpdateChecker::parseVersion(const QString& text)
{
    QString core = text.trimmed();
    if (core.startsWith('v') || core.startsWith('V'))
        core.remove(0, 1);
    const int cut = core.indexOf(QRegularExpression("[-+ ]"));
    if (cut >= 0)
        core.truncate(cut);

    QVector<int> parts;
    for (const QString& piece : core.split('.', Qt::SkipEmptyParts)) {
        bool ok = false;
        const int n = piece.toInt(&ok);
        if (!ok)
            return {};
        parts.push_back(n);
    }
    return parts;
}

bool UpdateChecker::isNewer(const QString& candidate, const QString& current)
{
    const QVector<int> a = parseVersion(candidate);
    const QVector<int> b = parseVersion(current);
    if (a.isEmpty() || b.isEmpty())
        return false;

    const int n = qMax(a.size(), b.size());
    for (int i = 0; i < n; ++i) {
        const int x = i < a.size() ? a[i] : 0;
        const int y = i < b.size() ? b[i] : 0;
        if (x != y)
            return x > y;
    }
    return false;
}
