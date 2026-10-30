#include "UpdateInstaller.h"
#include "app/Logger.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>

#ifdef PLATFORM_WINDOWS
#include <windows.h>
#endif

namespace {

constexpr qint64 kMaxPackageBytes  = 400LL * 1024 * 1024;
constexpr int    kTransferTimeoutMs = 30 * 1000;

}

UpdateInstaller::UpdateInstaller(QObject* parent)
    : QObject(parent)
    , m_net(new QNetworkAccessManager(this))
{
}

bool UpdateInstaller::supported()
{
#if defined(PLATFORM_WINDOWS)
    return true;
#elif defined(PLATFORM_MACOS)
    // Only an installed .app bundle can be replaced; a bare build tree cannot.
    return QCoreApplication::applicationDirPath().endsWith(".app/Contents/MacOS");
#else
    return false;
#endif
}

QNetworkReply* UpdateInstaller::get(const QUrl& url)
{
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  "PktSwerve/" + QCoreApplication::applicationVersion().toUtf8());
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(kTransferTimeoutMs);
    return m_net->get(req);
}

void UpdateInstaller::setStatus(const QString& text)
{
    m_status = text;
    emit statusChanged(text);
}

// `source` is the English text (marked with QT_TR_NOOP); the log gets it untranslated.
void UpdateInstaller::fail(const char* source, const QString& arg)
{
    const auto fill = [&arg](const QString& text) { return text.contains("%1") ? text.arg(arg) : text; };
    LOG_ERROR("Update failed: %1", fill(QString::fromUtf8(source)));
    m_busy = false;
    m_file.close();
    setStatus(QString());
    emit failed(fill(tr(source)));
}

void UpdateInstaller::install(const UpdateChecker::Release& release)
{
    if (m_busy)
        return;
    if (!supported() || !release.canInstall()) {
        emit failed(tr("This build cannot install updates by itself."));
        return;
    }

    m_busy = true;
    m_release = release;
    m_tooLarge = false;
    m_workDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/updates";
    QDir(m_workDir).removeRecursively();
    if (!QDir().mkpath(m_workDir)) {
        fail(QT_TR_NOOP("Could not create %1"), m_workDir);
        return;
    }
    LOG_INFO("Installing update %1 from %2", release.version, release.assetUrl.toString());
    fetchChecksums();
}

void UpdateInstaller::fetchChecksums()
{
    setStatus(tr("Checking the release..."));
    QNetworkReply* reply = get(m_release.checksumsUrl);
    connect(reply, &QNetworkReply::finished, this, [this, reply] { onChecksums(reply); });
}

void UpdateInstaller::onChecksums(QNetworkReply* reply)
{
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        fail(QT_TR_NOOP("Could not read the checksums: %1"), reply->errorString());
        return;
    }

    // sha256sum format: "<64 hex digits>  <file name>", the name optionally starting with '*'.
    const QRegularExpression line(R"(^([0-9a-fA-F]{64})\s+\*?(.+?)\s*$)");
    const QStringList lines = QString::fromUtf8(reply->read(1024 * 1024)).split('\n');
    for (const QString& text : lines) {
        const QRegularExpressionMatch m = line.match(text.trimmed());
        if (m.hasMatch() && m.captured(2) == m_release.assetName) {
            m_expectedHash = m.captured(1).toLower();
            downloadPackage();
            return;
        }
    }
    fail(QT_TR_NOOP("The release has no checksum for %1."), m_release.assetName);
}

void UpdateInstaller::downloadPackage()
{
    m_packagePath = m_workDir + "/" + m_release.assetName;
    m_file.setFileName(m_packagePath);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        fail(QT_TR_NOOP("Could not write %1"), m_packagePath);
        return;
    }
    m_hash.reset();
    setStatus(tr("Downloading... 0%"));

    QNetworkReply* reply = get(m_release.assetUrl);
    connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
        const QByteArray chunk = reply->readAll();
        m_hash.addData(chunk);
        if (m_file.write(chunk) != chunk.size()) {
            m_tooLarge = false;
            reply->abort();
        } else if (m_file.size() > kMaxPackageBytes) {
            m_tooLarge = true;
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        if (total > 0)
            setStatus(tr("Downloading... %1%").arg(int(received * 100 / total)));
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply] { onPackageDownloaded(reply); });
}

void UpdateInstaller::onPackageDownloaded(QNetworkReply* reply)
{
    reply->deleteLater();
    m_file.close();

    if (m_tooLarge) {
        fail(QT_TR_NOOP("The download is larger than expected."));
        return;
    }
    if (reply->error() != QNetworkReply::NoError) {
        fail(QT_TR_NOOP("Download failed: %1"), reply->errorString());
        return;
    }

    const QString actual = QString::fromLatin1(m_hash.result().toHex());
    if (actual != m_expectedHash) {
        QFile::remove(m_packagePath);
        fail(QT_TR_NOOP("The downloaded file does not match its checksum, so it was discarded."));
        return;
    }
    LOG_INFO("Update package verified (sha256 %1)", actual);
    launchApplyScript();
}

bool UpdateInstaller::writeResource(const QString& resource, const QString& path) const
{
    QFile in(resource);
    if (!in.open(QIODevice::ReadOnly))
        return false;
    QFile::remove(path);
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly))
        return false;
    return out.write(in.readAll()) >= 0;
}

void UpdateInstaller::launchApplyScript()
{
    setStatus(tr("Installing..."));
    const QString log = m_workDir + "/apply-update.log";
    const QString pid = QString::number(QCoreApplication::applicationPid());

#if defined(PLATFORM_WINDOWS)
    const QString script = m_workDir + "/apply-update.ps1";
    if (!writeResource(":/update/apply-update.ps1", script)) {
        fail(QT_TR_NOOP("Could not prepare the update script."));
        return;
    }
    const QString powershell = qEnvironmentVariable("SystemRoot", "C:\\Windows")
        + "\\System32\\WindowsPowerShell\\v1.0\\powershell.exe";
    const QStringList args = {
        "-NoProfile", "-ExecutionPolicy", "Bypass", "-WindowStyle", "Hidden",
        "-File", QDir::toNativeSeparators(script),
        "-AppPid", pid,
        "-Zip", QDir::toNativeSeparators(m_packagePath),
        "-InstallDir", QDir::toNativeSeparators(QCoreApplication::applicationDirPath()),
        "-Exe", QDir::toNativeSeparators(QCoreApplication::applicationFilePath()),
        "-LogFile", QDir::toNativeSeparators(log),
    };

    // Leave the job the app may run in (Task Scheduler ends a task's job with it), so the
    // script survives the app's exit; fall back to a plain start if the job forbids that.
    bool started = false;
    for (bool breakaway : {true, false}) {
        QProcess process;
        process.setProgram(powershell);
        process.setArguments(args);
        process.setCreateProcessArgumentsModifier([breakaway](QProcess::CreateProcessArguments* a) {
            a->flags &= ~(CREATE_NEW_CONSOLE | DETACHED_PROCESS);
            a->flags |= CREATE_NO_WINDOW;
            if (breakaway)
                a->flags |= CREATE_BREAKAWAY_FROM_JOB;
        });
        if (process.startDetached()) {
            started = true;
            break;
        }
    }
#elif defined(PLATFORM_MACOS)
    const QString script = m_workDir + "/apply-update.sh";
    if (!writeResource(":/update/apply-update.sh", script)) {
        fail(QT_TR_NOOP("Could not prepare the update script."));
        return;
    }
    QDir bundle(QCoreApplication::applicationDirPath());
    bundle.cdUp();
    bundle.cdUp();
    const bool started = QProcess::startDetached(
        "/bin/bash", {script, pid, m_packagePath, bundle.absolutePath(), log});
#else
    const bool started = false;
#endif

    if (!started) {
        fail(QT_TR_NOOP("Could not start the update script."));
        return;
    }
    LOG_INFO0("Update script started - quitting so it can replace the files");
    setStatus(tr("Restarting PktSwerve..."));
    QTimer::singleShot(0, qApp, &QCoreApplication::quit);
}
