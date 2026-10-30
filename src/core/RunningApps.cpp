#include "RunningApps.h"
#include "AppMatcher.h"

#include <QFileInfo>
#include <QHash>

#include <algorithm>

#ifdef PLATFORM_WINDOWS
#include <windows.h>
#include <tlhelp32.h>
#elif defined(PLATFORM_MACOS)
#include <libproc.h>
#include <vector>
#endif

namespace RunningApps {

namespace {

void addUnique(QHash<QString, Entry>& found, const QString& entry, const QString& label)
{
    if (!entry.isEmpty() && !found.contains(entry))
        found.insert(entry, {entry, label});
}

} // namespace

QList<Entry> list()
{
    QHash<QString, Entry> found;

#ifdef PLATFORM_WINDOWS
    QString windowsDir = qEnvironmentVariable("SystemRoot").toLower();
    windowsDir.replace('\\', '/');

    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W process{};
        process.dwSize = sizeof(process);
        for (BOOL ok = Process32FirstW(snapshot, &process); ok; ok = Process32NextW(snapshot, &process)) {
            if (process.th32ProcessID <= 4)
                continue;
            QString path;
            if (HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process.th32ProcessID)) {
                wchar_t buf[MAX_PATH * 2] = {};
                DWORD len = static_cast<DWORD>(std::size(buf));
                if (QueryFullProcessImageNameW(h, 0, buf, &len))
                    path = QString::fromWCharArray(buf, static_cast<int>(len));
                CloseHandle(h);
            }
            if (path.isEmpty())
                continue;   // protected or system process
            QString lowerPath = path.toLower();
            lowerPath.replace('\\', '/');
            if (!windowsDir.isEmpty() && lowerPath.startsWith(windowsDir + "/"))
                continue;   // Windows' own programs
            const QString label = QFileInfo(path).fileName();
            addUnique(found, AppMatcher::normalizeEntry(label), label);
        }
        CloseHandle(snapshot);
    }
#elif defined(PLATFORM_MACOS)
    const int bytes = proc_listpids(PROC_ALL_PIDS, 0, nullptr, 0);
    if (bytes > 0) {
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
            if (path.startsWith("/System/") || path.startsWith("/usr/") || path.startsWith("/Library/"))
                continue;
            // The outermost bundle, so a helper inside Discord.app counts as Discord.
            const int app = path.indexOf(".app/");
            if (app < 0)
                continue;
            const QString bundle = path.left(app).mid(path.lastIndexOf('/', app) + 1);
            addUnique(found, AppMatcher::normalizeEntry(bundle + ".app"), bundle);
        }
    }
#endif

    QList<Entry> result = found.values();
    std::sort(result.begin(), result.end(), [](const Entry& a, const Entry& b) {
        return a.label.compare(b.label, Qt::CaseInsensitive) < 0;
    });
    return result;
}

} // namespace RunningApps
