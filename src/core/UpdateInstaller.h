#pragma once
#include "UpdateChecker.h"

#include <QCryptographicHash>
#include <QFile>
#include <QObject>

class QNetworkAccessManager;
class QNetworkReply;

// Downloads the package of a release, checks it against the release's SHA256SUMS
// and hands it to a small script that replaces the installed files after the app
// has quit. The script starts PktSwerve again, also when something goes wrong.
class UpdateInstaller : public QObject {
    Q_OBJECT
public:
    explicit UpdateInstaller(QObject* parent = nullptr);

    // Windows and macOS builds that run from an installed location.
    static bool supported();

    bool isBusy() const { return m_busy; }
    QString status() const { return m_status; }

    void install(const UpdateChecker::Release& release);

signals:
    void statusChanged(const QString& text);
    void failed(const QString& reason);

private:
    QNetworkReply* get(const QUrl& url);
    void setStatus(const QString& text);
    void fail(const char* source, const QString& arg = {});

    void fetchChecksums();
    void onChecksums(QNetworkReply* reply);
    void downloadPackage();
    void onPackageDownloaded(QNetworkReply* reply);
    void launchApplyScript();

    bool writeResource(const QString& resource, const QString& path) const;

    QNetworkAccessManager* m_net = nullptr;
    UpdateChecker::Release m_release;
    QString                m_status;
    QString                m_workDir;
    QString                m_packagePath;
    QString                m_expectedHash;
    QFile                  m_file;
    QCryptographicHash     m_hash{QCryptographicHash::Sha256};
    bool                   m_busy = false;
    bool                   m_tooLarge = false;
};
