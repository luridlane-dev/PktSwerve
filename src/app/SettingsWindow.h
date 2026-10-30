#pragma once
#include <QMainWindow>

class BypassEngine;
class UpdateChecker;
class UpdateInstaller;
struct BypassConfig;

class DomainListEditor;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QTextEdit;
class QWidget;

class SettingsWindow : public QMainWindow {
    Q_OBJECT
public:
    SettingsWindow(BypassEngine* engine, UpdateChecker* updates, UpdateInstaller* installer,
                   QWidget* parent = nullptr);

    void showAboutTab();

private slots:
    void onApply();
    void onModeChanged(int index);
    void onDnsPresetSelected(int index);
    void onCheckConnectivity();
    void onResetDefaults();

private:
    void buildUi();
    void buildGeneralTab(QWidget* tab);
    void buildMethodsTab(QWidget* tab);
    void buildDnsTab(QWidget* tab);
    void buildHostlistTab(QWidget* tab);
    void buildAboutTab(QWidget* tab);
    void refreshUpdateUi();

    void loadToUi(const BypassConfig& cfg);
    BypassConfig uiToConfig();
    void runDnsProbe(bool restartEngine);

    BypassEngine*  m_engine;
    UpdateChecker* m_updates;
    UpdateInstaller* m_installer;
    QTabWidget*    m_tabs = nullptr;
    QLabel*        m_updateStatus = nullptr;
    QPushButton*   m_updateBtn = nullptr;
    QPushButton*   m_installBtn = nullptr;

    QComboBox*    m_modeCombo = nullptr;
    QComboBox*    m_languageCombo = nullptr;
    QCheckBox*    m_autoStartCheck = nullptr;

    QCheckBox*    m_fragHttp = nullptr;
    QCheckBox*    m_fragHttps = nullptr;
    QCheckBox*    m_wrongChecksum = nullptr;
    QCheckBox*    m_wrongSeq = nullptr;
    QCheckBox*    m_reverseFrag = nullptr;
    QCheckBox*    m_tlsRecSplit = nullptr; // macOS only
    QCheckBox*    m_fakePackets = nullptr;
    QCheckBox*    m_voiceUdp = nullptr;
    QSpinBox*     m_httpFragSize = nullptr;
    QSpinBox*     m_httpsFragSize = nullptr;
    QSpinBox*     m_fakeTTL = nullptr;

    QCheckBox*    m_dnsRedirect = nullptr;
    QLineEdit*    m_dnsServer = nullptr;
    QSpinBox*     m_dnsPort = nullptr;
    QComboBox*    m_dnsPresetCombo = nullptr;
    QTextEdit*    m_dnsStatusLabel = nullptr;
    QPushButton*  m_checkBtn = nullptr;

    DomainListEditor* m_blacklist = nullptr;
    DomainListEditor* m_apps = nullptr;
    QLabel*       m_hostListHint = nullptr;
};
