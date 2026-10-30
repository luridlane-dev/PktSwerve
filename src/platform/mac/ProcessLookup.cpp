#ifdef PLATFORM_MACOS
#include "ProcessLookup.h"
#include "core/AppMatcher.h"

#include <arpa/inet.h>
#include <libproc.h>
#include <sys/proc_info.h>

#include <QFileInfo>

#include <chrono>
#include <vector>

namespace {

// A program that started a moment ago must still be noticed, so the process
// list is kept only briefly when sockets have to be read again.
constexpr qint64 kProcessListMaxAgeMs = 100;

qint64 nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

quint32 portKey(quint16 clientPort, quint16 serverPort)
{
    return (static_cast<quint32>(clientPort) << 16) | serverPort;
}

} // namespace

bool ProcessLookup::belongsToApp(const QStringList& apps, quint16 clientPort, quint16 serverPort,
                                 qint64 acceptedAtMs, QString* appName)
{
    if (apps.isEmpty())
        return false;

    QMutexLocker lock(&m_mutex);
    if (apps != m_apps || m_socketsAt < acceptedAtMs)
        rebuild(apps);

    const auto it = m_sockets.constFind(portKey(clientPort, serverPort));
    if (it == m_sockets.constEnd())
        return false;
    if (appName)
        *appName = *it;
    return true;
}

void ProcessLookup::rebuild(const QStringList& apps)
{
    // Stamped before reading: a connection accepted after this moment may be
    // missing from the snapshot and will ask for a newer one.
    const qint64 start = nowMs();
    if (apps != m_apps || start - m_candidatesAt > kProcessListMaxAgeMs) {
        m_apps = apps;
        listCandidates(apps);
        m_candidatesAt = start;
    }

    m_sockets.clear();
    for (const Candidate& process : std::as_const(m_candidates))
        collectSockets(process);
    m_socketsAt = start;
}

void ProcessLookup::listCandidates(const QStringList& apps)
{
    m_candidates.clear();
    const int bytes = proc_listpids(PROC_ALL_PIDS, 0, nullptr, 0);
    if (bytes <= 0)
        return;

    std::vector<pid_t> pids(static_cast<size_t>(bytes) / sizeof(pid_t) + 16);
    const int got = proc_listpids(PROC_ALL_PIDS, 0, pids.data(), static_cast<int>(pids.size() * sizeof(pid_t)));
    const int count = got > 0 ? got / static_cast<int>(sizeof(pid_t)) : 0;
    for (int i = 0; i < count; ++i) {
        if (pids[i] <= 0)
            continue;
        char buf[PROC_PIDPATHINFO_MAXSIZE] = {};
        if (proc_pidpath(pids[i], buf, sizeof(buf)) <= 0)
            continue;
        const QString path = QString::fromLocal8Bit(buf);
        const QString name = QFileInfo(path).fileName();
        if (AppMatcher::anyMatches(apps, name, path))
            m_candidates.append({pids[i], name});
    }
}

void ProcessLookup::collectSockets(const Candidate& process)
{
    const int bytes = proc_pidinfo(process.pid, PROC_PIDLISTFDS, 0, nullptr, 0);
    if (bytes <= 0)
        return;
    std::vector<proc_fdinfo> fds(static_cast<size_t>(bytes) / sizeof(proc_fdinfo) + 8);
    const int got = proc_pidinfo(process.pid, PROC_PIDLISTFDS, 0, fds.data(),
                                 static_cast<int>(fds.size() * sizeof(proc_fdinfo)));
    const int count = got > 0 ? got / static_cast<int>(sizeof(proc_fdinfo)) : 0;

    for (int i = 0; i < count; ++i) {
        if (fds[i].proc_fdtype != PROX_FDTYPE_SOCKET)
            continue;
        socket_fdinfo info{};
        if (proc_pidfdinfo(process.pid, fds[i].proc_fd, PROC_PIDFDSOCKETINFO, &info, sizeof(info)) != sizeof(info))
            continue;
        if (info.psi.soi_kind != SOCKINFO_TCP)
            continue;
        const in_sockinfo& tcp = info.psi.soi_proto.pri_tcp.tcpsi_ini;
        m_sockets.insert(portKey(ntohs(static_cast<uint16_t>(tcp.insi_lport)),
                                 ntohs(static_cast<uint16_t>(tcp.insi_fport))),
                         process.name);
    }
}

#endif // PLATFORM_MACOS
