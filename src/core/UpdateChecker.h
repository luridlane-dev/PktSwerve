#pragma once
#include <QObject>
#include <QUrl>
#include <QVector>

class QNetworkAccessManager;
class QTimer;

// Asks GitHub for the latest published release and reports whether it is newer
// than the running build. It only notifies; installing is left to the user.
class UpdateChecker : public QObject {
    Q_OBJECT
public:
    struct Release {
        QString version;        // "1.0.4", no leading 'v'
        QUrl    page;           // release page in the browser
        QString notes;          // release description (markdown)
        QString assetName;      // package for this platform; empty when the release has none
        QUrl    assetUrl;
        QUrl    checksumsUrl;   // SHA256SUMS of the release

        bool canInstall() const { return !assetUrl.isEmpty() && !checksumsUrl.isEmpty(); }
    };

    explicit UpdateChecker(QObject* parent = nullptr);

    // The newer release found by the last check, or an empty one.
    const Release& available() const { return m_available; }
    bool hasUpdate() const { return !m_available.version.isEmpty(); }

    // File name of the package this build is updated with ("" on other systems).
    static QString platformAssetName();

    bool autoCheckEnabled() const;
    void setAutoCheckEnabled(bool enabled);

    // Starts the background schedule: one check shortly after launch, then
    // whenever a day has passed since the last one. Does nothing while disabled.
    void startAutoCheck();

    // userInitiated checks always report their outcome; background ones stay
    // silent unless a newer release exists.
    void check(bool userInitiated);

    bool isChecking() const { return m_checking; }

    // "1.2.10" > "1.2.9"; a leading 'v' and a "-suffix" are ignored.
    static bool isNewer(const QString& candidate, const QString& current);

signals:
    void updateAvailable(const UpdateChecker::Release& release, bool userInitiated);
    void upToDate();
    void checkFailed(const QString& reason);

private:
    static QVector<int> parseVersion(const QString& text);
    void maybeCheck();
    void onReply();

    QNetworkAccessManager* m_net = nullptr;
    QTimer*                m_timer = nullptr;
    bool                   m_checking = false;
    bool                   m_userInitiated = false;
    Release                m_available;
};
