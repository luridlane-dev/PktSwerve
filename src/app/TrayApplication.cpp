#include "TrayApplication.h"
#include "core/BypassEngine.h"
#include "SettingsWindow.h"
#include "PopupWindow.h"
#include "app/Logger.h"

#include <QAction>
#include <QCursor>
#include <QDir>
#include <QIcon>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QStandardPaths>
#include <QTimer>

namespace {

#ifdef Q_OS_MACOS
// Menu bar icons are monochrome masks; the idle state is drawn fainter.
QIcon menuBarIcon(bool running)
{
    constexpr int kPixels = 44;
    QPixmap logo = QPixmap(":/icons/active.png").scaled(kPixels, kPixels, Qt::KeepAspectRatio,
                                                         Qt::SmoothTransformation);
    QPixmap px(kPixels, kPixels);
    px.fill(Qt::transparent);
    {
        QPainter p(&px);
        p.setOpacity(running ? 1.0 : 0.45);
        p.drawPixmap((kPixels - logo.width()) / 2, (kPixels - logo.height()) / 2, logo);
    }
    px.setDevicePixelRatio(2);

    QIcon icon(px);
    icon.setIsMask(true);
    return icon;
}
#endif

}

TrayApplication::TrayApplication(int& argc, char** argv)
    : QApplication(argc, argv)
{
    setQuitOnLastWindowClosed(false);
    setApplicationName("PktSwerve");
    setApplicationVersion("1.0.3");
    setOrganizationName("PktSwerve");

    const QString logDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(logDir);
    const QString logPath = logDir + "/pktswerve.log";

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
    if (m_tray) m_tray->hide();
    delete m_menu;
}

bool TrayApplication::init()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        LOG_ERR0("System tray not available - aborting init");
        QMessageBox::critical(nullptr, "Error", "System tray is not available.");
        return false;
    }

    m_engine = new BypassEngine(this);
    connect(m_engine, &BypassEngine::started,       this, &TrayApplication::onEngineStarted);
    connect(m_engine, &BypassEngine::stopped,       this, &TrayApplication::onEngineStopped);
    connect(m_engine, &BypassEngine::errorOccurred, this, &TrayApplication::onEngineError);
    connect(m_engine, &BypassEngine::configLoadFailed, this, [](const QString& reason) {
        QMessageBox::warning(nullptr, "PktSwerve", reason);
    });

    m_tray = new QSystemTrayIcon(this);
    connect(m_tray, &QSystemTrayIcon::activated, this, &TrayApplication::onTrayActivated);
    buildTrayMenu();
    updateTrayIcon();

    m_popup = new PopupWindow(m_engine);
    connect(m_popup, &PopupWindow::settingsRequested, this, &TrayApplication::onOpenSettings);

    m_tray->show();
    LOG_INFO0("Tray icon shown - init complete");

    if (m_engine->autoStart()) {
        LOG_INFO0("AutoStart is enabled - starting on launch");
        m_engine->start();
    } else {
        showPopup();
    }
    return true;
}

void TrayApplication::showPopup()
{
    m_popup->showAtTray(m_tray->geometry());
}

void TrayApplication::buildTrayMenu()
{
    m_menu = new QMenu;

    m_statusAction = m_menu->addAction("PktSwerve - Ready");
    m_statusAction->setEnabled(false);
    m_menu->addSeparator();

    m_toggleAction = m_menu->addAction("Start", this, &TrayApplication::onToggleBypass);
    m_menu->addSeparator();
    m_menu->addAction("Settings", this, &TrayApplication::onOpenSettings);
    m_menu->addSeparator();
    m_menu->addAction("Quit", qApp, &QCoreApplication::quit);

#ifndef Q_OS_MACOS
    // On macOS a tray menu swallows the left click, so it is shown on right click instead.
    m_tray->setContextMenu(m_menu);
#endif
}

void TrayApplication::onTrayActivated(QSystemTrayIcon::ActivationReason reason)
{
#ifdef Q_OS_MACOS
    if (reason == QSystemTrayIcon::Context) {
        m_popup->hide();
        m_menu->popup(QCursor::pos());
        return;
    }
#endif

    bool clicked = reason == QSystemTrayIcon::Trigger;
#ifdef Q_OS_WIN
    clicked |= reason == QSystemTrayIcon::DoubleClick;
#endif
    if (!clicked)
        return;

    if (m_popup->isVisible()) {
        m_popup->hide();
        return;
    }
    QTimer::singleShot(50, this, &TrayApplication::showPopup);
}

void TrayApplication::onToggleBypass()
{
    if (m_engine->isRunning())
        m_engine->stop();
    else
        m_engine->start();
}

void TrayApplication::onOpenSettings()
{
    if (!m_settingsWindow) {
        m_settingsWindow = new SettingsWindow(m_engine);
        m_settingsWindow->setAttribute(Qt::WA_DeleteOnClose);
        connect(m_settingsWindow, &QObject::destroyed, this, [this] { m_settingsWindow = nullptr; });
    }
    m_settingsWindow->show();
    m_settingsWindow->raise();
    m_settingsWindow->activateWindow();
}

void TrayApplication::onEngineStarted()
{
    m_statusAction->setText("PktSwerve - Active");
    m_toggleAction->setText("Stop");
    updateTrayIcon();
    m_tray->showMessage("PktSwerve", "Bypass active.", QSystemTrayIcon::Information, 1500);
}

void TrayApplication::onEngineStopped()
{
    m_statusAction->setText("PktSwerve - Stopped");
    m_toggleAction->setText("Start");
    updateTrayIcon();
}

void TrayApplication::onEngineError(const QString& error)
{
    LOG_ERROR("Engine error reported to tray: %1", error);
    m_statusAction->setText("PktSwerve - Error");
    updateTrayIcon();
    m_tray->showMessage("PktSwerve - Error", error, QSystemTrayIcon::Critical, 4000);
}

void TrayApplication::updateTrayIcon()
{
    const bool running = m_engine->isRunning();
#ifdef Q_OS_MACOS
    m_tray->setIcon(menuBarIcon(running));
#else
    m_tray->setIcon(QIcon(running ? ":/icons/active.png" : ":/icons/inactive.png"));
#endif
    m_tray->setToolTip(running ? "PktSwerve - Active" : "PktSwerve - Inactive");
}
