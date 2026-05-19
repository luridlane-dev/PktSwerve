#pragma once
#include <QMainWindow>

class BypassEngine;
struct BypassConfig;

class QTabWidget;
class QCheckBox;
class QSpinBox;
class QLineEdit;
class QTextEdit;
class QComboBox;
class QPushButton;
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

    BypassEngine* m_engine;
    QTabWidget*   m_tabs;

    QComboBox*    m_modeCombo;
    QCheckBox*    m_autoStartCheck;

    QCheckBox*    m_fragHttp;
    QCheckBox*    m_fragHttps;
    QCheckBox*    m_wrongChecksum;
    QCheckBox*    m_wrongSeq;
    QCheckBox*    m_reverseFrag;
    QCheckBox*    m_fakePackets;
    QSpinBox*     m_httpFragSize;
    QSpinBox*     m_httpsFragSize;
    QSpinBox*     m_fakeTTL;

    QCheckBox*    m_dnsRedirect;
    QLineEdit*    m_dnsServer;
    QSpinBox*     m_dnsPort;

    QComboBox*    m_dnsPresetCombo;
    QTextEdit*    m_dnsStatusLabel;
    QPushButton*  m_checkBtn;

    QPushButton*  m_dnsTestBtn = nullptr;

    QTextEdit*    m_blacklistEdit;

    QWidget*      m_hostListWidget;
};