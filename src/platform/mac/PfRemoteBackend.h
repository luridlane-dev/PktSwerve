#pragma once
#ifdef PLATFORM_MACOS

#include "platform/IPlatformBackend.h"

#include <QElapsedTimer>
#include <QLocalSocket>
#include <QProcess>
#include <QTimer>

// The menu bar app's side of the macOS backend. pf rules and the proxies need
// root, so they live in a helper process (`PktSwerve --pf-helper`, see
// PfHelper.cpp). The user approves it with an administrator password once per
// session; the app itself stays an ordinary user process, which keeps its menu
// bar icon and windows working.
class PfRemoteBackend : public IPlatformBackend {
    Q_OBJECT
public:
    explicit PfRemoteBackend(QObject* parent = nullptr);
    ~PfRemoteBackend() override;

    // Returns at once. started() follows when the helper confirms, or
    // errorOccurred() if the password prompt is declined or the helper fails.
    bool start(const BypassConfig& config) override;
    bool stop() override;   // waits briefly for the helper so callers can start again right away
    bool isRunning() const override { return m_running; }
    QString lastError() const override { return m_lastError; }
    bool hasRequiredPrivileges() const override { return true; }   // the helper holds them
    bool requestPrivilegeElevation() override { return true; }

private slots:
    void tryConnect();
    void onReadyRead();
    void onDisconnected();

private:
    enum class Phase { Idle, Elevating, Connecting, Ready };

    Phase          m_phase = Phase::Idle;
    BypassConfig   m_config;
    QString        m_lastError;
    bool           m_running = false;
    QLocalSocket*  m_socket = nullptr;
    QProcess*      m_osascript = nullptr;
    QTimer         m_retry;
    QElapsedTimer  m_connectTimer;

    void launchHelper();
    void sendStart();
    void handleLine(const QString& line);
    void cancelStartup();
    void dropSocket();
    void fail(const QString& message);
};

#endif // PLATFORM_MACOS
