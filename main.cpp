
#include <QApplication>
#include <QIcon>
#include "app/TrayApplication.h"
#include "app/Language.h"
#include "app/Logger.h"
#include "app/Theme.h"

#ifdef PLATFORM_MACOS
#include <cstring>
#include "platform/mac/PfHelper.h"
#endif

#ifdef PLATFORM_WINDOWS
#include <windows.h>
#include <shellapi.h>

LONG WINAPI crashHandler(EXCEPTION_POINTERS*)
{
    Logger::instance().log(Logger::Error, "CRASH - unhandled exception", __FILE__, __LINE__);
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

#ifdef PLATFORM_WINDOWS
namespace {

bool isRunningElevated()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return false;
    }

    TOKEN_ELEVATION elevation{};
    DWORD size = sizeof(elevation);
    const bool ok = GetTokenInformation(token, TokenElevation, &elevation, size, &size) != FALSE;
    CloseHandle(token);

    return ok && elevation.TokenIsElevated != 0;
}

bool relaunchElevated()
{
    wchar_t path[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, path, MAX_PATH) == 0) {
        return false;
    }

    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.lpVerb = L"runas";
    sei.lpFile = path;
    sei.nShow = SW_SHOWNORMAL;

    return ShellExecuteExW(&sei) != FALSE;
}

} // namespace
#endif

int main(int argc, char* argv[]) {
#ifdef PLATFORM_MACOS
    // The root half of the app: PktSwerve --pf-helper <socket> <uid> <gid>
    if (argc >= 2 && std::strcmp(argv[1], "--pf-helper") == 0)
        return runPfHelper(argc, argv);
#endif
#ifdef PLATFORM_WINDOWS
    SetUnhandledExceptionFilter(crashHandler);

    if (!isRunningElevated()) {
        return relaunchElevated() ? 0 : 1;
    }
#endif

    TrayApplication app(argc, argv);
    Theme::apply(app);
    Language::install();

    const QIcon appIcon(":/icons/app.ico");
    if (!appIcon.isNull())
        QApplication::setWindowIcon(appIcon);

    if (!app.init()) return 1;
    return app.exec();
}
