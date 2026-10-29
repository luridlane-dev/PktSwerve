#pragma once
#include <QMainWindow>

class BypassEngine;
struct BypassConfig;

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QTextEdit;
class QWidget;

class SettingsWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit SettingsWindow(BypassEngine* engine, QWidget* parent = nullptr);

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

    void loadToUi(const BypassConfig& cfg);
    BypassConfig uiToConfig();
    void runDnsProbe(bool restartEngine);

    BypassEngine* m_engine;
    QTabWidget*   m_tabs = nullptr;

    QComboBox*    m_modeCombo = nullptr;
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

    QTextEdit*    m_blacklistEdit = nullptr;
    QWidget*      m_hostListWidget = nullptr;
};
