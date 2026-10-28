#pragma once
#ifdef PLATFORM_MACOS

#include "platform/IPlatformBackend.h"
#include <QThread>
#include <QPointer>
#include <atomic>
#include <memory>

struct PfShared;
class PfWorker;

// Connection-level backend (macOS has no packet divert API):
//
//   app --TCP 80/443--> pf (rdr via lo0) --> PfWorker proxy --> server
//
// The proxy reads the first flight (HTTP request or TLS ClientHello), checks
// the Host/SNI and writes it out in several segments, optionally as two TLS
// records. DNS queries are caught the same way and sent to the configured
// resolver.
//
// pf rules live in a per-process anchor, removed on stop(). A root watchdog
// removes them if the app dies without stopping.
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
    std::shared_ptr<PfShared> m_state;     // lets stop() reach the worker
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

// Owns the listening sockets and the accept loop. Each connection runs on its
// own detached thread and shares state through PfShared, never the worker.
class PfWorker : public QObject {
    Q_OBJECT
public:
    explicit PfWorker(QObject* parent = nullptr);
    ~PfWorker() override;

    void setConfig(const BypassConfig& cfg);
    std::shared_ptr<PfShared> state() const { return m_shared; }

    // Opens /dev/pf and binds the listeners; must succeed before pf rules load.
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
