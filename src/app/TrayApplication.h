#pragma once
#include <QApplication>
#include <QSystemTrayIcon>

#ifdef Q_OS_WIN
#include <windows.h>
#include <cstdio>
#endif

class BypassEngine;
class SettingsWindow;
class PopupWindow;

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
    void onStatsUpdated(quint64 packets, quint64 bytes);

private:
    void buildTrayMenu();
    void updateTrayIcon();

    QSystemTrayIcon* m_tray           = nullptr;
    QMenu*           m_menu           = nullptr;
    QAction*         m_toggleAction   = nullptr;
    QAction*         m_statusAction   = nullptr;
    BypassEngine*    m_engine         = nullptr;
    SettingsWindow*  m_settingsWindow = nullptr;
    PopupWindow*     m_popup          = nullptr;
};