#pragma once
#include <QString>
#include <QStringList>

// How applications are named in the "Selected Apps" list and matched against
// running processes. An entry is one of:
//   discord.exe   an executable file name (Windows)
//   discord.app   an application bundle, including every helper inside it (macOS)
//   discord       either of the above
namespace AppMatcher {

// What gets stored for a file the user picked or typed: just the last path
// component in lower case, so "C:\...\app-1.0.9\Discord.exe" survives updates.
// Empty when the text cannot be an application name.
inline QString normalizeEntry(QString text)
{
    text = text.trimmed();
    if (text.size() >= 2 && text.startsWith('"') && text.endsWith('"'))
        text = text.mid(1, text.size() - 2).trimmed();
    text.replace('\\', '/');
    while (text.endsWith('/'))
        text.chop(1);
    text = text.mid(text.lastIndexOf('/') + 1).trimmed().toLower();

    static const QString forbidden = QStringLiteral("<>:\"|?*");
    if (text.isEmpty() || text.size() > 128)
        return {};
    for (const QChar c : text)
        if (forbidden.contains(c) || c.unicode() < 32)
            return {};
    return text;
}

// `exeName` and `exePath` are what the operating system reports for a process.
inline bool entryMatches(const QString& entry, const QString& exeName, const QString& exePath)
{
    const QString e = entry.trimmed().toLower();
    if (e.isEmpty())
        return false;
    const QString name = exeName.toLower();
    QString path = exePath.toLower();
    path.replace('\\', '/');

    if (e.endsWith(".app"))
        return path.contains("/" + e + "/");
    if (e.endsWith(".exe"))
        return name == e;
    return name == e || name == e + ".exe" || path.contains("/" + e + ".app/");
}

inline bool anyMatches(const QStringList& entries, const QString& exeName, const QString& exePath)
{
    for (const QString& entry : entries)
        if (entryMatches(entry, exeName, exePath))
            return true;
    return false;
}

} // namespace AppMatcher
