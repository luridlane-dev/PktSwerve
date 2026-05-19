
#include <QApplication>
#include "app/TrayApplication.h"
#include "app/Logger.h"

#ifdef PLATFORM_WINDOWS
#include <windows.h>

LONG WINAPI crashHandler(EXCEPTION_POINTERS*)
{
    Logger::instance().log(Logger::Error, "CRASH - unhandled exception", __FILE__, __LINE__);
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

int main(int argc, char* argv[]) {
    TrayApplication app(argc, argv);

    QIcon appIcon(":/icons/app.ico");
    if (!appIcon.isNull()) {
        QApplication::setWindowIcon(appIcon);
    }

#ifdef PLATFORM_WINDOWS
    SetUnhandledExceptionFilter(crashHandler);
#endif

    if (!app.init()) return 1;
    app.showPopup();
    return app.exec();
}