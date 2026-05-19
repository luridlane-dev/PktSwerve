#include "TrayApplication.h"
#include "core/BypassEngine.h"
#include "SettingsWindow.h"
#include "PopupWindow.h"
#include "app/Logger.h"
#include <QMessageBox>
#include <QMenu>
#include <QAction>
#include <QIcon>
#include <QPixmap>
#include <QImage>
#include <QTimer>
#include <QStandardPaths>
#include <QDir>


TrayApplication::TrayApplication(int& argc, char** argv)
    : QApplication(argc, argv)
{
    setQuitOnLastWindowClosed(false);
    setApplicationName("PktSwerve");
    setApplicationVersion("1.0.0");
    setOrganizationName("LuridLane");

    QString logDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(logDir);
    QString logPath = logDir + "/pktswerve.log";

#ifdef QT_DEBUG
    Logger::instance().init(logPath, Logger::Debug);
#else
    Logger::instance().init(logPath, Logger::Info);
#endif

    LOG_INFO("TrayApplication created | version=%1", applicationVersion());
    LOG_INFO("Log file: %1", logPath);
}

TrayApplication::~TrayApplication()
{
    LOG_INFO0("TrayApplication destroying");
    if (m_engine) m_engine->stop();
    if (m_tray)   m_tray->hide();
}

bool TrayApplication::init()
{
    LOG_INFO0("TrayApplication::init() start");

    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        LOG_ERR0("System tray not available - aborting init");
        QMessageBox::critical(nullptr, "Error", "System tray is not available.");
        return false;
    }
    LOG_INFO0("System tray available");

    m_engine = new BypassEngine(this);
    connect(m_engine, &BypassEngine::started,       this, &TrayApplication::onEngineStarted);
    connect(m_engine, &BypassEngine::stopped,       this, &TrayApplication::onEngineStopped);
    connect(m_engine, &BypassEngine::errorOccurred, this, &TrayApplication::onEngineError);
    connect(m_engine, &BypassEngine::statsUpdated,  this, &TrayApplication::onStatsUpdated);
    connect(m_engine, &BypassEngine::configLoadFailed, this, [this](const QString& reason) {
        QMessageBox::warning(nullptr, "PktSwerve", reason);
    });
    LOG_INFO0("BypassEngine created and signals connected");

    m_tray = new QSystemTrayIcon(this);
    connect(m_tray, &QSystemTrayIcon::activated, this, &TrayApplication::onTrayActivated);

    buildTrayMenu();
    updateTrayIcon();

    m_popup = new PopupWindow(m_engine);
    connect(m_popup, &PopupWindow::settingsRequested, this, &TrayApplication::onOpenSettings);
    LOG_INFO0("PopupWindow created");

    m_tray->show();
    LOG_INFO0("Tray icon shown - init complete");
    return true;
}

void TrayApplication::showPopup()
{
    m_popup->showAtTray(m_tray->geometry());
}

void TrayApplication::buildTrayMenu()
{
    LOG_DEBUG0("Building tray context menu");
    delete m_menu;
    m_menu = new QMenu();

    m_statusAction = m_menu->addAction("PktSwerve - Ready");
    m_statusAction->setEnabled(false);
    m_menu->addSeparator();

    m_toggleAction = m_menu->addAction("▶  Start");
    connect(m_toggleAction, &QAction::triggered, this, &TrayApplication::onToggleBypass);

    m_menu->addSeparator();

    QAction* settingsAction = m_menu->addAction("⚙  Settings");
    connect(settingsAction, &QAction::triggered, this, &TrayApplication::onOpenSettings);

    m_menu->addSeparator();

    QAction* quitAction = m_menu->addAction("✕  Quit");
    connect(quitAction, &QAction::triggered, qApp, &QCoreApplication::quit);

    m_tray->setContextMenu(m_menu);
    LOG_DEBUG0("Tray context menu built");
}


void TrayApplication::onTrayActivated(QSystemTrayIcon::ActivationReason reason)
{
    LOG_DEBUG("Tray activated | reason=%1", static_cast<int>(reason));
#ifdef Q_OS_WIN
    if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
#else
    if (reason == QSystemTrayIcon::Trigger)
#endif
    {
        if (m_popup->isVisible()) {
            LOG_DEBUG0("Popup visible - hiding");
            m_popup->hide();
        } else {
            LOG_DEBUG0("Popup hidden - scheduling show");
            QTimer::singleShot(50, this, [this]() {
                m_popup->showAtTray(m_tray->geometry());
            });
        }
    }
}

void TrayApplication::onToggleBypass()
{
    if (m_engine->isRunning()) {
        LOG_INFO0("Toggle: stopping engine");
        m_engine->stop();
    } else {
        LOG_INFO0("Toggle: starting engine");
        m_engine->start();
    }
}

void TrayApplication::onOpenSettings()
{
    LOG_INFO0("Opening settings window");
    if (!m_settingsWindow) {
        m_settingsWindow = new SettingsWindow(m_engine);
        connect(m_settingsWindow, &QObject::destroyed, this, [this]() {
            LOG_DEBUG0("SettingsWindow destroyed");
            m_settingsWindow = nullptr;
        });
        m_settingsWindow->setAttribute(Qt::WA_DeleteOnClose);
    }
    m_settingsWindow->show();
    m_settingsWindow->raise();
    m_settingsWindow->activateWindow();
}

void TrayApplication::onEngineStarted()
{
    LOG_INFO0("Engine started — updating tray");
    m_statusAction->setText("PktSwerve - Active");
    m_toggleAction->setText("■  Stop");
    updateTrayIcon();
    m_tray->showMessage("PktSwerve", "Bypass active.", QSystemTrayIcon::Information, 1500);
}

void TrayApplication::onEngineStopped()
{
    LOG_INFO0("Engine stopped - updating tray");
    m_statusAction->setText("PktSwerve - Stopped");
    m_toggleAction->setText("▶  Start");
    updateTrayIcon();
}

void TrayApplication::onEngineError(const QString& error)
{
    LOG_ERROR("Engine error reported to tray: %1", error);
    m_statusAction->setText("PktSwerve - Error");
    updateTrayIcon();
    m_tray->showMessage("PktSwerve - Error", error, QSystemTrayIcon::Critical, 4000);
}

void TrayApplication::onStatsUpdated(quint64 packets, quint64 bytes)
{
    Q_UNUSED(packets)
    Q_UNUSED(bytes)
}

void TrayApplication::updateTrayIcon()
{
    bool running = m_engine->isRunning();
    LOG_DEBUG("updateTrayIcon | running=%1", running ? "yes" : "no");

    QString resPath = running ? ":/icons/active.png" : ":/icons/inactive.png";
    QIcon icon(resPath);

    if (icon.isNull()) {
        const QString color = running ? "#43ea80" : "#555555";
        QString svg = QString(
                          "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 32 32'>"
                          "<circle cx='16' cy='16' r='14' fill='%1'/>"
                          "<path d='M10 16 Q13 10 16 16 Q19 22 22 16' "
                          "      stroke='white' stroke-width='2.5' fill='none' stroke-linecap='round'/>"
                          "</svg>"
                          ).arg(color);
        icon = QIcon(QPixmap::fromImage(QImage::fromData(svg.toUtf8(), "SVG")));
    }

    m_tray->setIcon(icon);
    m_tray->setToolTip(running ? "PktSwerve - Active" : "PktSwerve - Inactive");
}