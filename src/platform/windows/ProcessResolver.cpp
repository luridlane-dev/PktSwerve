#ifdef PLATFORM_WINDOWS
#include "ProcessResolver.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>

#include <QFileInfo>

#include <vector>

namespace {

// A snapshot older than this is read again before it is believed: a closed
// connection's row may still sit in it while another program has taken its port.
constexpr qint64 kTrustMs = 100;

qint64 nowMs() { return static_cast<qint64>(GetTickCount64()); }

quint32 portKey(quint16 localPort, quint16 remotePort)
{
    return (static_cast<quint32>(localPort) << 16) | remotePort;
}

} // namespace

bool ProcessResolver::tcpOwner(bool ipv6, quint16 localPort, quint16 remotePort, Owner& owner)
{
    const int family = ipv6 ? 1 : 0;
    Table& table = m_tables[family];
    const quint32 key = portKey(localPort, remotePort);

    bool fresh = false;
    if (nowMs() - table.takenAt > kTrustMs) {
        refresh(family);
        fresh = true;
    }
    auto it = table.pids.constFind(key);
    if (it == table.pids.constEnd() && !fresh) {
        // A connection newer than the snapshot.
        refresh(family);
        it = table.pids.constFind(key);
    }
    if (it == table.pids.constEnd())
        return false;

    owner = describe(*it);
    return !owner.name.isEmpty();
}

void ProcessResolver::refresh(int family)
{
    Table& table = m_tables[family];
    table.takenAt = nowMs();
    table.pids.clear();

    std::vector<uint8_t> buf(64 * 1024);
    DWORD size = static_cast<DWORD>(buf.size());
    const ULONG af = family == 1 ? AF_INET6 : AF_INET;
    ULONG rc = GetExtendedTcpTable(buf.data(), &size, FALSE, af, TCP_TABLE_OWNER_PID_ALL, 0);
    if (rc == ERROR_INSUFFICIENT_BUFFER) {
        buf.resize(size + 4096);
        size = static_cast<DWORD>(buf.size());
        rc = GetExtendedTcpTable(buf.data(), &size, FALSE, af, TCP_TABLE_OWNER_PID_ALL, 0);
    }
    if (rc != NO_ERROR)
        return;

    if (family == 1) {
        const auto* rows = reinterpret_cast<const MIB_TCP6TABLE_OWNER_PID*>(buf.data());
        table.pids.reserve(static_cast<qsizetype>(rows->dwNumEntries));
        for (DWORD i = 0; i < rows->dwNumEntries; ++i) {
            const auto& row = rows->table[i];
            table.pids.insert(portKey(ntohs(static_cast<u_short>(row.dwLocalPort)),
                                      ntohs(static_cast<u_short>(row.dwRemotePort))),
                              row.dwOwningPid);
        }
    } else {
        const auto* rows = reinterpret_cast<const MIB_TCPTABLE_OWNER_PID*>(buf.data());
        table.pids.reserve(static_cast<qsizetype>(rows->dwNumEntries));
        for (DWORD i = 0; i < rows->dwNumEntries; ++i) {
            const auto& row = rows->table[i];
            table.pids.insert(portKey(ntohs(static_cast<u_short>(row.dwLocalPort)),
                                      ntohs(static_cast<u_short>(row.dwRemotePort))),
                              row.dwOwningPid);
        }
    }
}

// Asked fresh every time: process ids are reused quickly, so a remembered
// name could belong to a program that has since exited.
ProcessResolver::Owner ProcessResolver::describe(quint32 pid)
{
    Owner owner;
    if (HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
        wchar_t path[MAX_PATH * 2] = {};
        DWORD len = static_cast<DWORD>(std::size(path));
        if (QueryFullProcessImageNameW(h, 0, path, &len)) {
            owner.path = QString::fromWCharArray(path, static_cast<int>(len));
            owner.name = QFileInfo(owner.path).fileName();
        }
        CloseHandle(h);
    }
    return owner;
}

#endif // PLATFORM_WINDOWS
