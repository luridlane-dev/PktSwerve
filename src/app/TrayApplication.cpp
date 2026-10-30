#include "TrayApplication.h"
#include "core/BypassEngine.h"
#include "core/UpdateInstaller.h"
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
#include <QSettings>
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
    setApplicationVersion(PKTSWERVE_VERSION);
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
        QMessageBox::critical(nullptr, tr("Error"), tr("System tray is not available."));
        return false;
    }

    m_engine = new BypassEngine(this);
    connect(m_engine, &BypassEngine::started,       this, &TrayApplication::onEngineStarted);
    connect(m_engine, &BypassEngine::stopped,       this, &TrayApplication::onEngineStopped);
    connect(m_engine, &BypassEngine::errorOccurred, this, &TrayApplication::onEngineError);
    connect(m_engine, &BypassEngine::configLoadFailed, this, [](const QString& reason) {
        QMessageBox::warning(nullptr, "PktSwerve", reason);
    });

    m_updates = new UpdateChecker(this);
    m_installer = new UpdateInstaller(this);
    connect(m_updates, &UpdateChecker::updateAvailable, this, &TrayApplication::onUpdateAvailable);

    m_tray = new QSystemTrayIcon(this);
    connect(m_tray, &QSystemTrayIcon::activated, this, &TrayApplication::onTrayActivated);
    connect(m_tray, &QSystemTrayIcon::messageClicked, this, [this] {
        if (m_updateBalloon)
            showUpdate();
    });
    buildTrayMenu();
    updateTrayIcon();

    m_popup = new PopupWindow(m_engine);
    connect(m_popup, &PopupWindow::settingsRequested, this, &TrayApplication::onOpenSettings);

    m_tray->show();
    LOG_INFO0("Tray icon shown - init complete");

    m_updates->startAutoCheck();

#ifdef Q_OS_MACOS
    // Connecting needs the administrator password, so a login item only puts the
    // icon in the menu bar and leaves the connecting to the user.
    if (arguments().contains("--background"))
        LOG_INFO0("Started at login - waiting in the menu bar");
    else
        showPopup();
#else
    if (m_engine->autoStart()) {
        LOG_INFO0("AutoStart is enabled - starting on launch");
        m_engine->start();
    } else {
        showPopup();
    }
#endif
    return true;
}

void TrayApplication::showPopup()
{
    m_popup->showAtTray(m_tray->geometry());
}

void TrayApplication::buildTrayMenu()
{
    m_menu = new QMenu;

    m_statusAction = m_menu->addAction(tr("PktSwerve - Ready"));
    m_statusAction->setEnabled(false);
    m_menu->addSeparator();

    m_toggleAction = m_menu->addAction(tr("Start"), this, &TrayApplication::onToggleBypass);
    m_menu->addSeparator();
    m_menu->addAction(tr("Settings"), this, &TrayApplication::onOpenSettings);
    m_updateAction = m_menu->addAction(tr("Update available"), this, &TrayApplication::showUpdate);
    m_updateAction->setVisible(false);
    m_menu->addSeparator();
    m_menu->addAction(tr("Quit"), qApp, &QCoreApplication::quit);

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
        m_settingsWindow = new SettingsWindow(m_engine, m_updates, m_installer);
        m_settingsWindow->setAttribute(Qt::WA_DeleteOnClose);
        connect(m_settingsWindow, &QObject::destroyed, this, [this] { m_settingsWindow = nullptr; });
    }
    m_settingsWindow->show();
    m_settingsWindow->raise();
    m_settingsWindow->activateWindow();
}

void TrayApplication::onEngineStarted()
{
    m_statusAction->setText(tr("PktSwerve - Active"));
    m_toggleAction->setText(tr("Stop"));
    updateTrayIcon();
    m_updateBalloon = false;
    m_tray->showMessage("PktSwerve", tr("Connected."), QSystemTrayIcon::Information, 1500);
}

void TrayApplication::onEngineStopped()
{
    m_statusAction->setText(tr("PktSwerve - Stopped"));
    m_toggleAction->setText(tr("Start"));
    updateTrayIcon();
}

void TrayApplication::onEngineError(const QString& error)
{
    LOG_ERR0("Engine error reported to tray (details are logged where it occurred)");
    m_statusAction->setText(tr("PktSwerve - Error"));
    updateTrayIcon();
    m_updateBalloon = false;
    m_tray->showMessage(tr("PktSwerve - Error"), error, QSystemTrayIcon::Critical, 4000);
}

void TrayApplication::onUpdateAvailable(const UpdateChecker::Release& release, bool userInitiated)
{
    const QString& version = release.version;
    m_updateAction->setText(tr("Update available: %1").arg(version));
    m_updateAction->setVisible(true);

    // Background checks announce each version once; a manual check already
    // shows its result in the Settings window.
    QSettings settings;
    if (userInitiated || settings.value("updates/notifiedVersion").toString() == version)
        return;
    settings.setValue("updates/notifiedVersion", version);
    m_updateBalloon = true;
    m_tray->showMessage(tr("PktSwerve update available"),
                        tr("Version %1 is out. Click to see it in Settings.").arg(version),
                        QSystemTrayIcon::Information, 8000);
}

void TrayApplication::showUpdate()
{
    onOpenSettings();
    m_settingsWindow->showAboutTab();
}

void TrayApplication::updateTrayIcon()
{
    const bool running = m_engine->isRunning();
#ifdef Q_OS_MACOS
    m_tray->setIcon(menuBarIcon(running));
#else
    m_tray->setIcon(QIcon(running ? ":/icons/active.png" : ":/icons/inactive.png"));
#endif
    m_tray->setToolTip(running ? tr("PktSwerve - Active") : tr("PktSwerve - Inactive"));
}
