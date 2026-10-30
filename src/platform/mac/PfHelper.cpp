#ifdef PLATFORM_MACOS
#include "PfHelper.h"
#include "PfBackend.h"
#include "PfIpc.h"
#include "app/Logger.h"

#include <QCoreApplication>
#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>
#include <QStandardPaths>
#include <QTimer>

#include <unistd.h>

namespace {

constexpr int kWaitForAppMs = 120000;   // the app connects right after the password prompt

// Messages are one line each.
QString oneLine(QString s)
{
    s.replace('\n', ' ').replace('\r', ' ');
    return s;
}

void giveToUser(const QString& path, uid_t uid, gid_t gid)
{
    const int r = ::chown(path.toUtf8().constData(), uid, gid);
    (void)r;
}

} // namespace

int runPfHelper(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("PktSwerve");
    QCoreApplication::setOrganizationName("PktSwerve");

    const QStringList args = app.arguments();   // exe, --pf-helper, socket, uid, gid
    if (args.size() < 5)
        return 2;
    const QString sockPath = args.at(2);
    const uid_t uid = static_cast<uid_t>(args.at(3).toUInt());
    const gid_t gid = static_cast<gid_t>(args.at(4).toUInt());

    // Same folder as the app's log, so both are easy to find. HOME points at the user's home.
    const QString logDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(logDir);
    const QString logPath = logDir + "/pktswerve-helper.log";
    Logger::instance().init(logPath, Logger::Info);
    giveToUser(logPath, uid, gid);
    LOG_INFO("Helper started | uid=%1 | socket=%2", uid, sockPath);

    QLocalServer::removeServer(sockPath);
    QLocalServer server;
    server.setSocketOptions(QLocalServer::UserAccessOption);   // root only until handed over below
    if (!server.listen(sockPath)) {
        LOG_ERROR("Cannot listen on %1: %2", sockPath, server.errorString());
        return 1;
    }
    giveToUser(sockPath, uid, gid);

    PfBackend backend;
    QLocalSocket* client = nullptr;
    bool running = false;

    auto send = [&](const QString& line) {
        if (client && client->state() == QLocalSocket::ConnectedState) {
            client->write(line.toUtf8() + '\n');
            client->flush();
        }
    };

    QObject::connect(&backend, &IPlatformBackend::errorOccurred, &app,
                     [&](const QString& m) { send("ERROR " + oneLine(m)); });
    QObject::connect(&backend, &IPlatformBackend::statsUpdated, &app,
                     [&](quint64 conns, quint64 bytes) { send(QString("STATS %1 %2").arg(conns).arg(bytes)); });
    // The proxy can also end on its own (a poll() failure, for instance).
    QObject::connect(&backend, &IPlatformBackend::stopped, &app, [&] {
        if (running) {
            running = false;
            send("STOPPED");
        }
    });

    auto handleLine = [&](const QByteArray& line) {
        if (line.startsWith("START ")) {
            BypassConfig cfg;
            if (!PfIpc::configFromJson(line.mid(6), cfg)) {
                send("ERROR The helper could not read the settings it was sent.");
                send("STOPPED");
                return;
            }
            if (running) {
                running = false;
                backend.stop();   // a restart: apply the new settings from a clean state
            }
            // On failure the backend has already reported the reason through errorOccurred.
            running = backend.start(cfg);
            send(running ? "STARTED" : "STOPPED");
        } else if (line == "STOP") {
            running = false;   // set first, so the stopped signal does not report it twice
            backend.stop();
            send("STOPPED");
        }
    };

    QObject::connect(&server, &QLocalServer::newConnection, &app, [&] {
        QLocalSocket* s = server.nextPendingConnection();
        if (!s) return;
        if (client) {   // one app at a time
            s->disconnectFromServer();
            s->deleteLater();
            return;
        }
        client = s;
        LOG_INFO0("App connected");

        QObject::connect(s, &QLocalSocket::readyRead, &app, [&, s] {
            while (s->canReadLine())
                handleLine(s->readLine().trimmed());
        });
        QObject::connect(s, &QLocalSocket::disconnected, &app, [&] {
            LOG_INFO0("App disconnected - cleaning up and exiting");
            running = false;
            backend.stop();
            QCoreApplication::quit();
        });
    });

    QTimer::singleShot(kWaitForAppMs, &app, [&] {
        if (!client) {
            LOG_WARN0("No app connected - exiting");
            QCoreApplication::quit();
        }
    });

    const int code = app.exec();
    LOG_INFO("Helper exiting | code=%1", code);
    return code;
}

#endif // PLATFORM_MACOS
