#pragma once

#include <QString>

// UI language: "en" (default) or "tr".
namespace Language {

QString saved();
void    save(const QString& code);

// Installs the translators for the saved language. Call once, before any window is created.
void install();

}
