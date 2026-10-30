#include "Language.h"
#include "app/Logger.h"

#include <QCoreApplication>
#include <QLibraryInfo>
#include <QSettings>
#include <QTranslator>

namespace {

const char* const kKey = "ui/language";

}

QString Language::saved()
{
    const QString code = QSettings().value(kKey).toString();
    return code == "tr" ? code : QString("en");
}

void Language::save(const QString& code)
{
    QSettings().setValue(kKey, code);
}

void Language::install()
{
    const QString code = saved();
    if (code == "en")
        return;     // the source strings are English

    auto* app = new QTranslator(qApp);
    if (app->load(":/i18n/pktswerve_" + code + ".qm")) {
        qApp->installTranslator(app);
    } else {
        LOG_WARN("No translation found for language '%1'", code);
        delete app;
    }

    // Standard dialog buttons ("Yes", "Cancel", ...) come from Qt's own catalog.
    auto* qt = new QTranslator(qApp);
    if (qt->load("qtbase_" + code, QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        qApp->installTranslator(qt);
    else
        delete qt;
}
