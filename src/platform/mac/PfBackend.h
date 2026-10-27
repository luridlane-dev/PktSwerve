#pragma once
#ifdef PLATFORM_MACOS

#include "platform/IPlatformBackend.h"
#include <QThread>
#include <QPointer>
#include <atomic>
#include <memory>

struct PfShared;
class PfWorker;

// macOS has no WinDivert/NFQUEUE equivalent, so this backend works at the
// connection level instead of the packet level:
//
//   app --TCP 80/443--> pf (rdr via lo0) --> PfWorker proxy --> real server
//
// The proxy reads the first flight (HTTP request / TLS ClientHello), decides
// from the Host/SNI whether to bypass, and writes it to the server in several
// TCP segments (and optionally as two TLS records).  DNS queries are caught
// the same way and forwarded to the configured resolver.
//
// pf rules live in a per-process anchor and are removed again on stop().
// A small root watchdog removes them too if the app dies without stopping,
// since stale redirect rules would break every web connection.
namespace PfPorts {
constexpr int Http  = 9876;
constexpr int Https = 9877;
constexpr int Dns   = 9878;
}

class PfBackend : public IPlatformBackend {
    Q_OBJECT
public:
    explicit PfBackend(QObject* parent = nullptr);
    ~PfBackend() override;

    bool start(const BypassConfig& config) override;
    bool stop() override;
    bool isRunning() const override;
    QString lastError() const override;
    bool hasRequiredPrivileges() const override;
    bool requestPrivilegeElevation() override;

private:
    QPointer<QThread>         m_thread;
    std::shared_ptr<PfShared> m_state;     // lets stop() reach the worker without a raw pointer
    QString                   m_lastError;
    QString                   m_pfToken;   // reference token from `pfctl -E`
    QString                   m_anchor;    // per-process pf anchor
    qint64                    m_watchdogPid = 0;
    std::atomic<bool>         m_running{false};
    quint64                   m_workerGeneration = 0;

    bool fail(const QString& message);
    bool setupPf(const BypassConfig& config);
    bool clearPf();
    void clearStaleAnchors();
    void startWatchdog();
    void stopWatchdog();
};

// -----------------------------------------------------------------------
// PfWorker - owns the listening sockets and the accept loop.  Every
// connection is handled on its own detached thread that shares state through
// PfShared, so nothing touches the worker after it has been destroyed.
// -----------------------------------------------------------------------
class PfWorker : public QObject {
    Q_OBJECT
public:
    explicit PfWorker(QObject* parent = nullptr);
    ~PfWorker() override;

    void setConfig(const BypassConfig& cfg);
    std::shared_ptr<PfShared> state() const { return m_shared; }

    // Opens /dev/pf and binds the listeners.  Must succeed before pf rules
    // are loaded, otherwise redirected connections would hit a closed port.
    bool prepare(QString* error);

public slots:
    void run();

signals:
    void finished();
    void errorOccurred(const QString& error);
    void statsUpdated(quint64 packets, quint64 bytes);

private:
    std::shared_ptr<PfShared> m_shared;
    int m_httpFd  = -1;
    int m_httpsFd = -1;

    void closeListeners();
};

#endif // PLATFORM_MACOS
