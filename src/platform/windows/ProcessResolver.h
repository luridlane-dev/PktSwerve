#pragma once
#ifdef PLATFORM_WINDOWS

#include <QHash>
#include <QString>

#include <cstdint>

// Finds which program owns a TCP connection, from the packet's two ports.
// Windows keeps this in its TCP table, so a connection is found as soon as its
// handshake is done, which is always before the first request is sent.
// Used from the packet thread only.
class ProcessResolver {
public:
    struct Owner {
        QString name;   // "Discord.exe"
        QString path;   // full path, empty if Windows would not tell us
    };

    // `localPort` and `remotePort` are host order. Returns false if no process is found.
    bool tcpOwner(bool ipv6, quint16 localPort, quint16 remotePort, Owner& owner);

private:
    // One snapshot of the connection table: (local port << 16 | remote port) -> pid.
    // Ports get reused, so a snapshot is only trusted for a moment.
    struct Table {
        QHash<quint32, quint32> pids;
        qint64                  takenAt = 0;
    };

    Table m_tables[2];   // IPv4, IPv6

    void refresh(int family);
    static Owner describe(quint32 pid);
};

#endif // PLATFORM_WINDOWS
