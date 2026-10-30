#pragma once
#include <QList>
#include <QString>

// Programs that are running right now, for picking apps in the settings.
namespace RunningApps {

struct Entry {
    QString entry;   // what goes into the app list, e.g. "discord.exe" or "discord.app"
    QString label;   // what the user sees, e.g. "Discord.exe" or "Discord"
};

// One entry per program, sorted by label. System components are left out.
// Empty on platforms where the list cannot be read.
QList<Entry> list();

} // namespace RunningApps
