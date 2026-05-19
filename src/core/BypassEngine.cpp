#include "BypassEngine.h"
#include "app/Logger.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QStandardPaths>
#include <QDir>
#include <QSettings>
#include <QTextStream>

#ifdef PLATFORM_WINDOWS
#  include "platform/windows/WinDivertBackend.h"
#elif defined(PLATFORM_LINUX)
#  include "platform/linux/NFQueueBackend.h"
#elif defined(PLATFORM_MACOS)
#  include "platform/mac/PfBackend.h"
#endif


BypassEngine::BypassEngine(QObject* parent) : QObject(parent)
{
    LOG_INFO0("BypassEngine created");
    loadConfig();
}

BypassEngine::~BypassEngine()
{
    LOG_INFO0("BypassEngine destroying - stopping backend if running");
    if (m_backend && m_backend->isRunning()) {
        m_backend->blockSignals(true);
        m_backend->stop();
    }
}


IPlatformBackend* BypassEngine::createBackend()
{
#ifdef PLATFORM_WINDOWS
    LOG_INFO0("Creating WinDivertBackend");
    return new WinDivertBackend(this);
#elif defined(PLATFORM_LINUX)
    LOG_INFO0("Creating NFQueueBackend");
    return new NFQueueBackend(this);
#elif defined(PLATFORM_MACOS)
    LOG_INFO0("Creating PfBackend");
    return new PfBackend(this);
#else
    LOG_ERR0("No backend available for this platform");
    return nullptr;
#endif
}


bool BypassEngine::start()
{
    LOG_INFO0("BypassEngine::start() called");

    if (m_backend && m_backend->isRunning()) {
        LOG_WARN0("start() called but backend already running - ignoring");
        return true;
    }

    if (!m_backend) {
        m_backend = createBackend();
        if (!m_backend) {
            m_lastError = "This platform is not supported.";
            LOG_ERROR("Backend creation failed: %1", m_lastError);
            emit errorOccurred(m_lastError);
            return false;
        }
        connect(m_backend, &IPlatformBackend::started,
                this,      &BypassEngine::started);
        connect(m_backend, &IPlatformBackend::stopped,
                this,      &BypassEngine::stopped);
        connect(m_backend, &IPlatformBackend::errorOccurred,
                this,      &BypassEngine::errorOccurred);
        connect(m_backend, &IPlatformBackend::statsUpdated,
                this,      &BypassEngine::statsUpdated);

        // Forward error messages to log
        connect(m_backend, &IPlatformBackend::errorOccurred,
                this, [](const QString& msg) {
                    LOG_ERROR("Backend error: %1", msg);
                });

        LOG_INFO0("Backend signals connected");
    }

    if (!m_backend->hasRequiredPrivileges()) {
        LOG_WARN0("Insufficient privileges - requesting elevation and exiting current instance");
        if (!m_elevationRequested) {
            m_elevationRequested = true;
            m_backend->requestPrivilegeElevation();
            QCoreApplication::exit(0);
        }
        return false;
    }

    LOG_INFO("Starting backend | mode=%1 | dnsRedirect=%2 | dnsServer=%3:%4",
             static_cast<int>(m_config.mode),
             m_config.dnsRedirect ? "yes" : "no",
             m_config.dnsServer,
             m_config.dnsPort);
    LOG_INFO("Fragment config | httpFrag=%1 (%2B) | httpsFrag=%3 (%4B) | reverse=%5",
             m_config.fragmentHttpRequests ? "on" : "off",
             m_config.httpFragmentSize,
             m_config.fragmentHttpsPersist ? "on" : "off",
             m_config.httpsFragmentSize,
             m_config.reverseFrag ? "yes" : "no");
    LOG_INFO("Deception config | fakePackets=%1 ttl=%2 | wrongChecksum=%3 | wrongSeq=%4",
             m_config.fakePackets ? "on" : "off",
             m_config.fakePacketTTL,
             m_config.wrongChecksum ? "on" : "off",
             m_config.wrongSeq ? "on" : "off");


    bool ok = m_backend->start(m_config);
    if (!ok)
        LOG_ERR0("Backend::start() returned false");
    else
        LOG_INFO0("Backend started successfully");
    return ok;
}

bool BypassEngine::stop()
{
    LOG_INFO0("BypassEngine::stop() called");
    if (!m_backend) {
        LOG_WARN0("stop() called but backend is null");
        return true;
    }
    bool ok = m_backend->stop();
    LOG_INFO("Backend stop returned: %1", ok ? "true" : "false");
    return ok;
}


bool BypassEngine::isRunning() const
{
    return m_backend && m_backend->isRunning();
}

void BypassEngine::setConfig(const BypassConfig& cfg)
{
    LOG_INFO("Config updated | mode=%1 | dnsRedirect=%2 | dnsServer=%3:%4",
             static_cast<int>(cfg.mode),
             cfg.dnsRedirect ? "yes" : "no",
             cfg.dnsServer,
             cfg.dnsPort);
    LOG_INFO("Blacklist size=%1 | Whitelist size=%2",
             cfg.blacklist.size(),
             cfg.whitelist.size());
    m_config = cfg;
    saveConfig();
    emit configChanged();
}


void BypassEngine::loadConfig()
{
    QString path = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
    + "/config.json";
    LOG_INFO("Loading config from: %1", path);

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        LOG_WARN("Config file not found or unreadable - using defaults | path=%1", path);
        return;
    }

    QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
    if (obj.isEmpty()) {
        LOG_WARN0("Config file is empty or invalid JSON - using defaults");
        emit configLoadFailed("Config file is corrupted, using defaults.");
        return;
    }

    m_config.mode                 = static_cast<BypassMode>(obj["mode"].toInt(1));
    m_config.fragmentHttpRequests = obj["fragmentHttpRequests"].toBool(true);
    m_config.fragmentHttpsPersist = obj["fragmentHttpsPersist"].toBool(true);
    m_config.wrongChecksum        = obj["wrongChecksum"].toBool(false);
    m_config.wrongSeq             = obj["wrongSeq"].toBool(false);
    m_config.nativeFrag           = obj["nativeFrag"].toBool(true);
    m_config.reverseFrag          = obj["reverseFrag"].toBool(false);
    m_config.fakePackets          = obj["fakePackets"].toBool(true);
    m_config.httpFragmentSize     = obj["httpFragmentSize"].toInt(2);
    m_config.httpsFragmentSize    = obj["httpsFragmentSize"].toInt(2);
    m_config.fakePacketTTL        = obj["fakePacketTTL"].toInt(8);
    m_config.dnsRedirect          = obj["dnsRedirect"].toBool(true);
    m_config.dnsServer            = obj["dnsServer"].toString("9.9.9.10");
    m_config.dnsPort              = obj["dnsPort"].toInt(9953);

    m_config.blacklist.clear();
    m_config.whitelist.clear();
    for (const auto& v : obj["blacklist"].toArray())
        m_config.blacklist << v.toString();
    for (const auto& v : obj["whitelist"].toArray())
        m_config.whitelist << v.toString();

    LOG_INFO("Config loaded | mode=%1 | dnsRedirect=%2 | dnsServer=%3:%4 "
             "| blacklist=%5 | whitelist=%6",
             static_cast<int>(m_config.mode),
             m_config.dnsRedirect ? "yes" : "no",
             m_config.dnsServer,
             m_config.dnsPort,
             m_config.blacklist.size(),
             m_config.whitelist.size());
}

void BypassEngine::saveConfig()
{
    QString dir  = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(dir);

    QJsonObject obj;
    obj["mode"]                  = static_cast<int>(m_config.mode);
    obj["fragmentHttpRequests"]  = m_config.fragmentHttpRequests;
    obj["fragmentHttpsPersist"]  = m_config.fragmentHttpsPersist;
    obj["wrongChecksum"]         = m_config.wrongChecksum;
    obj["wrongSeq"]              = m_config.wrongSeq;
    obj["nativeFrag"]            = m_config.nativeFrag;
    obj["reverseFrag"]           = m_config.reverseFrag;
    obj["fakePackets"]           = m_config.fakePackets;
    obj["httpFragmentSize"]      = m_config.httpFragmentSize;
    obj["httpsFragmentSize"]     = m_config.httpsFragmentSize;
    obj["fakePacketTTL"]         = m_config.fakePacketTTL;
    obj["dnsRedirect"]           = m_config.dnsRedirect;
    obj["dnsServer"]             = m_config.dnsServer;
    obj["dnsPort"]               = m_config.dnsPort;


    QJsonArray bl, wl;
    for (const auto& s : std::as_const(m_config.blacklist))  bl.append(s);
    for (const auto& s : std::as_const(m_config.whitelist))  wl.append(s);
    obj["blacklist"] = bl;
    obj["whitelist"] = wl;

    QString filePath = dir + "/config.json";
    QFile f(filePath);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(obj).toJson());
        LOG_INFO("Config saved to: %1", filePath);
    } else {
        LOG_ERROR("Failed to save config to: %1", filePath);
    }
}


bool BypassEngine::autoStart() const
{
#ifdef PLATFORM_WINDOWS
    QSettings s("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                QSettings::NativeFormat);
    return s.contains("PktSwerve");
#else
    return false;
#endif
}

void BypassEngine::setAutoStart(bool enabled)
{
    LOG_INFO("setAutoStart: %1", enabled ? "enabled" : "disabled");
    setupAutoStart(enabled);
}

void BypassEngine::setupAutoStart(bool enabled)
{
#ifdef PLATFORM_WINDOWS
    QSettings s("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                QSettings::NativeFormat);
    if (enabled) {
        QString exePath = QString("\"%1\"").arg(QCoreApplication::applicationFilePath());
        s.setValue("PktSwerve", exePath);
        LOG_INFO("AutoStart registry entry set: %1", exePath);
    } else {
        s.remove("PktSwerve");
        LOG_INFO0("AutoStart registry entry removed");
    }

#elif defined(PLATFORM_LINUX)
    QString dir  = QDir::homePath() + "/.config/autostart";
    QString file = dir + "/pktswerve.desktop";
    QDir().mkpath(dir);
    if (enabled) {
        QFile f(file);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&f);
            out << "[Desktop Entry]\n"
                << "Type=Application\n"
                << "Name=PktSwerve\n"
                << "Exec=" << QCoreApplication::applicationFilePath() << "\n"
                << "Hidden=false\n"
                << "NoDisplay=false\n"
                << "X-GNOME-Autostart-enabled=true\n";
            LOG_INFO("AutoStart .desktop written: %1", file);
        } else {
            LOG_ERR("Failed to write autostart .desktop: %1", file);
        }
    } else {
        QFile::remove(file);
        LOG_INFO("AutoStart .desktop removed: %1", file);
    }

#elif defined(PLATFORM_MACOS)
    QString dir  = QDir::homePath() + "/Library/LaunchAgents";
    QString file = dir + "/com.pktswerve.app.plist";
    QDir().mkpath(dir);
    if (enabled) {
        QFile f(file);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&f);
            out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                << "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
                   "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
                << "<plist version=\"1.0\"><dict>\n"
                << "  <key>Label</key><string>com.pktswerve.app</string>\n"
                << "  <key>ProgramArguments</key><array>\n"
                << "    <string>" << QCoreApplication::applicationFilePath() << "</string>\n"
                << "  </array>\n"
                << "  <key>RunAtLoad</key><true/>\n"
                << "</dict></plist>\n";
            LOG_INFO("AutoStart LaunchAgent written: %1", file);
        } else {
            LOG_ERR("Failed to write LaunchAgent plist: %1", file);
        }
    } else {
        QFile::remove(file);
        LOG_INFO("AutoStart LaunchAgent removed: %1", file);
    }
#endif
}

QString BypassEngine::lastError() const { return m_lastError; }