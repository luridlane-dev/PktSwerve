#ifdef PLATFORM_MACOS
#include "PfRemoteBackend.h"
#include "PfIpc.h"
#include "app/Logger.h"

#include <QCoreApplication>
#include <QDir>

#include <unistd.h>

namespace {

constexpr int kElevationStartMs = 5000;
constexpr int kHelperConnectMs  = 15000;   // counted after the password prompt is done
constexpr int kStopWaitMs       = 8000;

QString shellQuote(QString s) { s.replace("'", "'\\''"); return "'" + s + "'"; }
QString asQuote(QString s)    { s.replace("\\", "\\\\").replace("\"", "\\\""); return s; }

} // namespace

PfRemoteBackend::PfRemoteBackend(QObject* parent) : IPlatformBackend(parent)
{
    m_retry.setInterval(250);
    connect(&m_retry, &QTimer::timeout, this, &PfRemoteBackend::tryConnect);
}

PfRemoteBackend::~PfRemoteBackend()
{
    cancelStartup();
    stop();
    dropSocket();   // the helper cleans up and exits when the socket closes
}

bool PfRemoteBackend::start(const BypassConfig& config)
{
    m_config = config;
    m_lastError.clear();

    if (m_phase == Phase::Ready) {
        sendStart();
        return true;
    }
    if (m_phase == Phase::Idle)
        launchHelper();
    // Otherwise the helper is already on its way and will get the latest settings.
    return true;
}

void PfRemoteBackend::launchHelper()
{
    // HOME is pinned so the root helper writes its log to the user's folder.
    // The trailing '&' makes osascript return as soon as the helper is launched.
    const QString command = QString("HOME=%1 %2 --pf-helper %3 %4 %5 >/dev/null 2>&1 &")
                                .arg(shellQuote(QDir::homePath()),
                                     shellQuote(QCoreApplication::applicationFilePath()),
                                     shellQuote(PfIpc::socketPath(getuid())))
                                .arg(getuid()).arg(getgid());
    const QString script = QString("do shell script \"%1\" with administrator privileges")
                               .arg(asQuote(command));

    m_osascript = new QProcess(this);
    m_osascript->start("/usr/bin/osascript", {"-e", script});
    if (!m_osascript->waitForStarted(kElevationStartMs)) {
        m_osascript->deleteLater();
        m_osascript = nullptr;
        fail("Could not open the administrator password prompt.");
        return;
    }
    m_phase = Phase::Elevating;
    LOG_INFO0("Waiting for administrator approval to start the helper");

    connect(m_osascript, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        const QString err = QString::fromLocal8Bit(m_osascript->readAllStandardError()).trimmed();
        m_osascript->deleteLater();
        m_osascript = nullptr;
        if (m_phase != Phase::Elevating)
            return;   // cancelled in the meantime

        // osascript exits non-zero when the prompt is cancelled.
        if (status != QProcess::NormalExit || code != 0) {
            LOG_WARN("Administrator approval not granted: %1", err);
            m_phase = Phase::Idle;
            fail("Administrator permission was not granted.");
            return;
        }
        m_phase = Phase::Connecting;
        m_connectTimer.start();
        m_retry.start();
    });
}

void PfRemoteBackend::tryConnect()
{
    if (m_phase != Phase::Connecting) {
        m_retry.stop();
        return;
    }
    if (m_connectTimer.hasExpired(kHelperConnectMs)) {
        cancelStartup();
        fail("The privileged helper did not start. See pktswerve-helper.log next to pktswerve.log.");
        return;
    }

    if (!m_socket) {
        m_socket = new QLocalSocket(this);
        connect(m_socket, &QLocalSocket::readyRead,    this, &PfRemoteBackend::onReadyRead);
        connect(m_socket, &QLocalSocket::disconnected, this, &PfRemoteBackend::onDisconnected);
        connect(m_socket, &QLocalSocket::connected, this, [this] {
            m_retry.stop();
            m_phase = Phase::Ready;
            LOG_INFO0("Connected to the privileged helper");
            sendStart();
        });
    }
    // Until the helper is listening this fails quickly; the next tick tries again.
    if (m_socket->state() == QLocalSocket::UnconnectedState)
        m_socket->connectToServer(PfIpc::socketPath(getuid()));
}

void PfRemoteBackend::sendStart()
{
    if (!m_socket)
        return;
    m_socket->write("START " + PfIpc::configToJson(m_config) + '\n');
    m_socket->flush();
}

void PfRemoteBackend::onReadyRead()
{
    while (m_socket && m_socket->canReadLine())
        handleLine(QString::fromUtf8(m_socket->readLine()).trimmed());
}

void PfRemoteBackend::handleLine(const QString& line)
{
    if (line == "STARTED") {
        m_running = true;
        emit started();
    } else if (line == "STOPPED") {
        if (m_running) {
            m_running = false;
            emit stopped();
        }
    } else if (line.startsWith("STATS ")) {
        const QStringList p = line.split(' ');
        emit statsUpdated(p.value(1).toULongLong(), p.value(2).toULongLong());
    } else if (line.startsWith("ERROR ")) {
        m_lastError = line.mid(6);
        LOG_ERROR("Helper: %1", m_lastError);
        emit errorOccurred(m_lastError);
    }
}

void PfRemoteBackend::onDisconnected()
{
    const bool wasRunning = m_running;
    m_running = false;
    m_phase = Phase::Idle;
    m_retry.stop();
    dropSocket();
    if (wasRunning) {
        fail("The privileged helper stopped unexpectedly.");
        emit stopped();
    }
}

bool PfRemoteBackend::stop()
{
    if (m_phase == Phase::Elevating || m_phase == Phase::Connecting) {
        cancelStartup();
        return true;
    }
    if (!m_running || !m_socket)
        return true;

    m_socket->write("STOP\n");
    m_socket->flush();

    QElapsedTimer waited;
    waited.start();
    while (m_running && m_socket && m_socket->state() == QLocalSocket::ConnectedState &&
           waited.elapsed() < kStopWaitMs) {
        m_socket->waitForReadyRead(100);
        onReadyRead();
    }
    if (m_running) {
        LOG_WARN0("The helper did not confirm the stop in time");
        m_running = false;
        emit stopped();
    }
    return true;
}

void PfRemoteBackend::cancelStartup()
{
    m_retry.stop();
    if (m_osascript) {
        disconnect(m_osascript, nullptr, this, nullptr);
        m_osascript->kill();
        m_osascript->deleteLater();
        m_osascript = nullptr;
    }
    if (m_phase != Phase::Ready) {
        dropSocket();
        m_phase = Phase::Idle;
    }
}

void PfRemoteBackend::dropSocket()
{
    if (!m_socket)
        return;
    disconnect(m_socket, nullptr, this, nullptr);
    m_socket->abort();
    m_socket->deleteLater();
    m_socket = nullptr;
    m_phase = Phase::Idle;
}

void PfRemoteBackend::fail(const QString& message)
{
    m_lastError = message;
    LOG_ERROR("PfRemoteBackend: %1", message);
    emit errorOccurred(message);
}

#endif // PLATFORM_MACOS
