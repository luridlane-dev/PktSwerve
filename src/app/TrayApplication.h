#pragma once
#include <QApplication>
#include <QSystemTrayIcon>
#include "core/UpdateChecker.h"

class BypassEngine;
class SettingsWindow;
class PopupWindow;
class UpdateInstaller;
class QAction;
class QMenu;

class TrayApplication : public QApplication {
    Q_OBJECT
public:
    TrayApplication(int& argc, char** argv);
    ~TrayApplication() override;

    bool init();
    void showPopup();

private slots:
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);
    void onToggleBypass();
    void onOpenSettings();
    void onEngineStarted();
    void onEngineStopped();
    void onEngineError(const QString& error);
    void onUpdateAvailable(const UpdateChecker::Release& release, bool userInitiated);
    void showUpdate();

private:
    void buildTrayMenu();
    void updateTrayIcon();

    QSystemTrayIcon* m_tray           = nullptr;
    QMenu*           m_menu           = nullptr;
    QAction*         m_toggleAction   = nullptr;
    QAction*         m_statusAction   = nullptr;
    QAction*         m_updateAction   = nullptr;
    bool             m_updateBalloon  = false;   // the balloon on screen is the update notice
    UpdateChecker*   m_updates        = nullptr;
    UpdateInstaller* m_installer      = nullptr;
    BypassEngine*    m_engine         = nullptr;
    SettingsWindow*  m_settingsWindow = nullptr;
    PopupWindow*     m_popup          = nullptr;
};
