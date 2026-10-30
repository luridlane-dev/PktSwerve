#pragma once
#ifdef PLATFORM_MACOS

#include <QHash>
#include <QMutex>
#include <QString>
#include <QStringList>
#include <QVector>

#include <sys/types.h>

// Tells which program a proxied connection came from. The root helper can read
// every process's open sockets, so the client's source port identifies the
// owner. Only the sockets of processes that match the app list are read.
//
// The sockets are read into a snapshot that is shared by all connections. A
// connection's socket exists from the moment the proxy accepted it, so a
// snapshot taken after that moment is conclusive in both directions; a newer
// snapshot is only built when a connection arrives that the current one
// predates. A burst of connections therefore costs one scan, not one each.
class ProcessLookup {
public:
    // `clientPort` and `serverPort` are host order, `apps` use the AppMatcher
    // syntax. `acceptedAtMs` is when the proxy accepted the connection, on the
    // steady clock in milliseconds.
    bool belongsToApp(const QStringList& apps, quint16 clientPort, quint16 serverPort,
                      qint64 acceptedAtMs, QString* appName = nullptr);

private:
    struct Candidate {
        pid_t   pid;
        QString name;
    };

    QMutex                  m_mutex;
    QStringList             m_apps;
    QVector<Candidate>      m_candidates;   // processes that match m_apps
    qint64                  m_candidatesAt = 0;
    QHash<quint32, QString> m_sockets;      // (client port << 16 | server port) -> app name
    qint64                  m_socketsAt = 0;

    void rebuild(const QStringList& apps);   // mutex held
    void listCandidates(const QStringList& apps);
    void collectSockets(const Candidate& process);
};

#endif // PLATFORM_MACOS
