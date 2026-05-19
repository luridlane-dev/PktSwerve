#include "SettingsWindow.h"
#include "core/BypassEngine.h"
#include "app/Logger.h"

#include <QTabWidget>
#include <QCheckBox>
#include <QSpinBox>
#include <QLineEdit>
#include <QTextEdit>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>

#include <QApplication>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QScrollArea>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QtConcurrent/QtConcurrent>
#include <QHostInfo>
#include <QFuture>
#include <QPointer>
#include <QDesktopServices>
#include <QUrl>

static const char* NOTE_STYLE =
    "color: #888; font-size: 11px; font-style: italic;";

static QLabel* makeNote(const QString& text)
{
    QLabel* l = new QLabel(text);
    l->setStyleSheet(NOTE_STYLE);
    l->setWordWrap(true);
    return l;
}


SettingsWindow::SettingsWindow(BypassEngine* engine, QWidget* parent)
    : QMainWindow(parent), m_engine(engine)
{
    setWindowTitle("PktSwerve | Settings");
    setMinimumSize(920, 860);
    buildUi();
    loadToUi(m_engine->config());

    connect(m_engine, &BypassEngine::configChanged, this, [this]() {
        LOG_DEBUG0("SettingsWindow: config changed externally - reloading UI");
        loadToUi(m_engine->config());
    });

    LOG_INFO0("SettingsWindow created");
}

void SettingsWindow::buildUi()
{
    setStyleSheet(R"(
        QMainWindow, QWidget {
            background: #0d0d0d;
            color: #e0e0e0;
            font-family: 'Segoe UI', sans-serif;
            font-size: 13px;
        }
        QTabWidget::pane   { border: 1px solid rgba(255,255,255,0.08); border-radius: 6px; background: #0a0a0a; }
        QTabBar::tab       { padding: 10px 20px; background: transparent; color: #666; border-bottom: 2px solid transparent; }
        QTabBar::tab:selected { color: #ffffff; border-bottom: 2px solid #ffffff; }
        QTabBar::tab:hover { color: #aaa; }

        QGroupBox {
            border: 1px solid rgba(255,255,255,0.08);
            border-radius: 6px;
            margin-top: 18px;
            padding-top: 16px;
            color: #777;
            font-size: 11px;
            font-weight: 600;
            letter-spacing: 0.8px;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            subcontrol-position: top left;
            left: 12px;
            top: 0px;
            padding: 0 6px;
            background: #0a0a0a;
        }

        QCheckBox { spacing: 8px; }
        QCheckBox::indicator {
            width: 34px; height: 18px;
            border-radius: 9px;
            border: none;
            background: #2a2a2a;
        }
        QCheckBox::indicator:checked { background: #ffffff; }

        QLineEdit, QSpinBox, QTextEdit, QComboBox {
            background: #141414;
            border: 1px solid rgba(255,255,255,0.1);
            border-radius: 6px;
            padding: 8px 10px;
            color: #e0e0e0;
        }
        QLineEdit:focus, QSpinBox:focus, QTextEdit:focus, QComboBox:focus { border-color: #555; }

        QPushButton {
            background: #141414;
            border: 1px solid rgba(255,255,255,0.1);
            border-radius: 6px;
            padding: 8px 18px;
            color: #aaa;
        }
        QPushButton:hover { background: #222; color: #ffffff; }

        QPushButton#applyBtn { background: #ffffff; color: #000000; border: none; font-weight: 600; }
        QPushButton#applyBtn:hover { background: #d0d0d0; }

        QPushButton#cancelBtn { background: transparent; color: #777; border: 1px solid rgba(255,255,255,0.1); }
        QPushButton#cancelBtn:hover { color: #fff; }

        QPushButton#resetBtn { background: transparent; color: #ff4d4d; border: 1px solid rgba(255,77,77,0.3); }
        QPushButton#resetBtn:hover { background: rgba(255,77,77,0.1); border-color: rgba(255,77,77,0.6); }

        QScrollArea { border: none; }
    )");

    QWidget* central = new QWidget(this);
    setCentralWidget(central);

    setWindowFlags(windowFlags() | Qt::Window);
    setAttribute(Qt::WA_ShowWithoutActivating, false);

    QVBoxLayout* mainLay = new QVBoxLayout(central);
    mainLay->setContentsMargins(20, 20, 20, 16);
    mainLay->setSpacing(12);

    m_tabs = new QTabWidget();
    auto* genTab  = new QWidget(); buildGeneralTab(genTab);
    auto* methTab = new QWidget(); buildMethodsTab(methTab);
    auto* dnsTab  = new QWidget(); buildDnsTab(dnsTab);
    auto* hostTab = new QWidget(); buildHostlistTab(hostTab);
    auto* aboutTab= new QWidget(); buildAboutTab(aboutTab);

    m_tabs->addTab(genTab,  "General");
    m_tabs->addTab(methTab, "Methods");
    m_tabs->addTab(dnsTab,  "DNS");
    m_tabs->addTab(hostTab, "Domain List");
    m_tabs->addTab(aboutTab,"About");

    mainLay->addWidget(m_tabs, 1);

    QHBoxLayout* botRow = new QHBoxLayout();

    QPushButton* resetBtn = new QPushButton("Reset to Defaults");
    resetBtn->setObjectName("resetBtn");
    botRow->addWidget(resetBtn);

    botRow->addStretch();

    QPushButton* cancelBtn = new QPushButton("Cancel");
    cancelBtn->setObjectName("cancelBtn");
    QPushButton* applyBtn  = new QPushButton("Apply & Save");
    applyBtn->setObjectName("applyBtn");
    botRow->addWidget(cancelBtn);
    botRow->addWidget(applyBtn);
    mainLay->addLayout(botRow);

    connect(resetBtn,  &QPushButton::clicked, this, &SettingsWindow::onResetDefaults);
    connect(cancelBtn, &QPushButton::clicked, this, &QWidget::close);
    connect(applyBtn,  &QPushButton::clicked, this, &SettingsWindow::onApply);
}


void SettingsWindow::buildGeneralTab(QWidget* tab)
{
    QVBoxLayout* lay = new QVBoxLayout(tab);
    lay->setContentsMargins(20, 20, 20, 0);
    lay->setSpacing(16);

    QGroupBox* modeGroup = new QGroupBox("OPERATING MODE");
    QVBoxLayout* mg = new QVBoxLayout(modeGroup);
    mg->setSpacing(8);

    m_modeCombo = new QComboBox();
    m_modeCombo->addItem("Disabled",           static_cast<int>(BypassMode::Disabled));
    m_modeCombo->addItem("Selected Sites Only", static_cast<int>(BypassMode::BlacklistMode));
    m_modeCombo->addItem("Global",             static_cast<int>(BypassMode::GlobalMode));
    mg->addWidget(m_modeCombo);
    mg->addWidget(makeNote(
        "Selected Sites Only: bypass only the sites you add to the Site List tab.\n"
        "Global: affects all HTTP/HTTPS traffic."));

    lay->addWidget(modeGroup);

    QGroupBox* sysGroup = new QGroupBox("SYSTEM");
    QVBoxLayout* sg = new QVBoxLayout(sysGroup);
    m_autoStartCheck = new QCheckBox("Launch at Windows startup");
    sg->addWidget(m_autoStartCheck);
    lay->addWidget(sysGroup);

    connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsWindow::onModeChanged);

    lay->addStretch();
}


void SettingsWindow::buildMethodsTab(QWidget* tab)
{
    QScrollArea* scroll = new QScrollArea(tab);
    scroll->setWidgetResizable(true);
    QWidget* content = new QWidget();
    scroll->setWidget(content);
    QVBoxLayout* outer = new QVBoxLayout(tab);
    outer->setContentsMargins(0,0,0,0);
    outer->addWidget(scroll);

    QVBoxLayout* lay = new QVBoxLayout(content);
    lay->setContentsMargins(20, 20, 20, 20);
    lay->setSpacing(16);

    QGroupBox* fragGroup = new QGroupBox("FRAGMENTATION");
    QVBoxLayout* fg = new QVBoxLayout(fragGroup);
    fg->setSpacing(10);

    m_fragHttps = new QCheckBox("HTTPS / TLS ClientHello fragmentation");
    fg->addWidget(m_fragHttps);
    fg->addWidget(makeNote(
        "Splits the TLS handshake packet so the SNI (target domain) "
        "doesn't appear in a single packet. Primary bypass method."));

    m_fragHttp = new QCheckBox("HTTP fragmentation");
    fg->addWidget(m_fragHttp);
    fg->addWidget(makeNote(
        "Fragments plaintext HTTP on port 80. "
        "Rarely needed since most sites use HTTPS."));

    m_reverseFrag = new QCheckBox("Send fragments in reverse order");
    fg->addWidget(m_reverseFrag);
    fg->addWidget(makeNote(
        "Sends fragment 2 before fragment 1. "
        "May confuse additional DPI systems; disable if connections break."));

    QFormLayout* fragParams = new QFormLayout();
    m_httpFragSize  = new QSpinBox(); m_httpFragSize->setRange(1, 512);
    m_httpsFragSize = new QSpinBox(); m_httpsFragSize->setRange(1, 512);
    fragParams->addRow("HTTP fragment size (bytes):",  m_httpFragSize);
    fragParams->addRow("HTTPS fragment size (bytes):", m_httpsFragSize);
    fg->addLayout(fragParams);
    fg->addWidget(makeNote("Default of 2 bytes is optimal. Larger values may bypass certain DPI types."));

    lay->addWidget(fragGroup);

    // --- Deception packets ---
    QGroupBox* fakeGroup = new QGroupBox("DECEPTION PACKET TECHNIQUES");
    QVBoxLayout* fkl = new QVBoxLayout(fakeGroup);
    fkl->setSpacing(10);

    m_fakePackets = new QCheckBox("Low-TTL fake packet");
    fkl->addWidget(m_fakePackets);
    fkl->addWidget(makeNote(
        "Sends a decoy packet the DPI sees but drops before reaching the real server "
        "due to low TTL. Works best combined with fragmentation."));

    m_wrongChecksum = new QCheckBox("Wrong TCP checksum fake packet");
    fkl->addWidget(m_wrongChecksum);
    fkl->addWidget(makeNote(
        "Confuses DPI systems that skip checksum validation. "
        "Real server rejects the packet, but the DPI context table is corrupted."));

    m_wrongSeq = new QCheckBox("Wrong sequence number fake packet");
    fkl->addWidget(m_wrongSeq);
    fkl->addWidget(makeNote(
        "Disrupts DPI TCP stream reassembly. "
        "Aggressive technique; try disabling if other methods cause issues."));

    QFormLayout* ttlForm = new QFormLayout();
    m_fakeTTL = new QSpinBox();
    m_fakeTTL->setRange(1, 128);
    m_fakeTTL->setValue(8);
    ttlForm->addRow("Fake packet TTL:", m_fakeTTL);
    fkl->addLayout(ttlForm);
    fkl->addWidget(makeNote(
        "Fake packet lifetime in hops. Too high and the packet may reach the destination."));

    lay->addWidget(fakeGroup);

    lay->addStretch();
}


void SettingsWindow::buildDnsTab(QWidget* tab)
{
    QVBoxLayout* lay = new QVBoxLayout(tab);
    lay->setContentsMargins(20, 20, 20, 20);
    lay->setSpacing(16);

    QGroupBox* enableGroup = new QGroupBox("DNS REDIRECT");
    QVBoxLayout* eg = new QVBoxLayout(enableGroup);
    m_dnsRedirect = new QCheckBox("Enable DNS redirect");
    eg->addWidget(m_dnsRedirect);
    eg->addWidget(makeNote("Routes DNS queries to the server below instead of your ISP's DNS."));
    lay->addWidget(enableGroup);

    QHBoxLayout* splitLay = new QHBoxLayout();
    splitLay->setSpacing(16);

    QVBoxLayout* leftCol = new QVBoxLayout();

    QGroupBox* presetGroup = new QGroupBox("PRESET SERVERS");
    QVBoxLayout* pg = new QVBoxLayout(presetGroup);

    m_dnsPresetCombo = new QComboBox();
    m_dnsPresetCombo->addItem("— Manual entry —", "||");

    m_dnsPresetCombo->addItem("Quad9 Unfiltered (Alt Port)     9.9.9.10 : 9953",        "9.9.9.10|9953");
    m_dnsPresetCombo->addItem("Quad9 Secure (Alt Port 1)       9.9.9.9 : 9953",         "9.9.9.9|9953");
    m_dnsPresetCombo->addItem("Quad9 Secure (Alt Port 2)       149.112.112.112 : 9953", "149.112.112.112|9953");
    m_dnsPresetCombo->addItem("OpenDNS Primary (Alt Port 1)    208.67.222.222 : 5353",  "208.67.222.222|5353");
    m_dnsPresetCombo->addItem("OpenDNS Secondary (Alt Port 1)  208.67.220.220 : 5353",  "208.67.220.220|5353");
    m_dnsPresetCombo->addItem("OpenDNS Primary (Alt Port 2)    208.67.222.222 : 443",   "208.67.222.222|443");
    m_dnsPresetCombo->addItem("OpenDNS Secondary (Alt Port 2)  208.67.220.220 : 443",   "208.67.220.220|443");
    m_dnsPresetCombo->addItem("AdGuard Default (Alt Port 1)    94.140.14.14 : 5353",    "94.140.14.14|5353");
    m_dnsPresetCombo->addItem("AdGuard Default (Alt Port 2)    94.140.15.15 : 5353",    "94.140.15.15|5353");
    m_dnsPresetCombo->addItem("AdGuard Family (Alt Port)       94.140.14.15 : 5353",    "94.140.14.15|5353");
    m_dnsPresetCombo->addItem("Yandex Basic (Alt Port)         77.88.8.8 : 1253",       "77.88.8.8|1253");
    m_dnsPresetCombo->addItem("Yandex Safe (Alt Port)          77.88.8.88 : 1253",      "77.88.8.88|1253");
    m_dnsPresetCombo->addItem("CleanBrowsing Security (Alt)    185.228.168.9 : 5353",   "185.228.168.9|5353");

    m_dnsPresetCombo->addItem("Cloudflare Primary              1.1.1.1 : 53",           "1.1.1.1|53");
    m_dnsPresetCombo->addItem("Cloudflare Secondary            1.0.0.1 : 53",           "1.0.0.1|53");
    m_dnsPresetCombo->addItem("Cloudflare Malware Block        1.1.1.2 : 53",           "1.1.1.2|53");
    m_dnsPresetCombo->addItem("Cloudflare Family Block         1.1.1.3 : 53",           "1.1.1.3|53");
    m_dnsPresetCombo->addItem("Google Primary                  8.8.8.8 : 53",           "8.8.8.8|53");
    m_dnsPresetCombo->addItem("Google Secondary                8.8.4.4 : 53",           "8.8.4.4|53");
    m_dnsPresetCombo->addItem("Quad9 Secure Primary            9.9.9.9 : 53",           "9.9.9.9|53");
    m_dnsPresetCombo->addItem("OpenDNS Primary                 208.67.222.222 : 53",    "208.67.222.222|53");
    m_dnsPresetCombo->addItem("AdGuard Default                 94.140.14.14 : 53",      "94.140.14.14|53");
    m_dnsPresetCombo->addItem("Yandex Basic                    77.88.8.8 : 53",         "77.88.8.8|53");
    m_dnsPresetCombo->addItem("ControlD Unfiltered             76.76.2.0 : 53",         "76.76.2.0|53");

    pg->addWidget(m_dnsPresetCombo);
    leftCol->addWidget(presetGroup);

    connect(m_dnsPresetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsWindow::onDnsPresetSelected);

    QGroupBox* manualGroup = new QGroupBox("SERVER ADDRESS");
    QFormLayout* form = new QFormLayout(manualGroup);
    m_dnsServer = new QLineEdit();
    m_dnsServer->setPlaceholderText("e.g. 9.9.9.10");
    m_dnsPort = new QSpinBox();
    m_dnsPort->setRange(1, 65535);
    form->addRow("IP Address:", m_dnsServer);
    form->addRow("Port:",       m_dnsPort);
    leftCol->addWidget(manualGroup);

    leftCol->addStretch();

    QGroupBox* checkGroup = new QGroupBox("CONNECTIVITY TEST");
    QVBoxLayout* cl = new QVBoxLayout(checkGroup);

    m_checkBtn = new QPushButton("Test Connection");
    m_checkBtn->setObjectName("applyBtn");
    cl->addWidget(m_checkBtn);

    connect(m_checkBtn, &QPushButton::clicked, this, &SettingsWindow::onCheckConnectivity);

    m_dnsStatusLabel = new QTextEdit();
    m_dnsStatusLabel->setReadOnly(true);
    m_dnsStatusLabel->setStyleSheet(
        "background: transparent; border: 1px solid rgba(255,255,255,0.05); border-radius: 8px;"
        " color: #d0d0d0; font-size: 13px; font-family: 'Segoe UI', sans-serif; padding: 12px; line-height: 1.5;");
    cl->addWidget(m_dnsStatusLabel);

    splitLay->addLayout(leftCol, 1);
    splitLay->addWidget(checkGroup, 1);

    lay->addLayout(splitLay, 1);
}


void SettingsWindow::buildHostlistTab(QWidget* tab)
{
    QVBoxLayout* outer = new QVBoxLayout(tab);
    outer->setContentsMargins(0, 0, 0, 0);

    m_hostListWidget = new QWidget();
    QVBoxLayout* lay = new QVBoxLayout(m_hostListWidget);
    lay->setContentsMargins(20, 20, 20, 20);
    lay->setSpacing(12);

    lay->addWidget(makeNote(
        "One domain per line. Subdomains match automatically "
        "(e.g. 'example.com' also covers 'api.example.com')."));

    QLabel* listTitle = new QLabel("Bypass List");
    listTitle->setStyleSheet("font-weight: 600; font-size: 13px; color: #e0e0e0;");
    lay->addWidget(listTitle);
    lay->addWidget(makeNote("Traffic to these domains will be bypassed."));

    m_blacklistEdit = new QTextEdit();
    m_blacklistEdit->setPlaceholderText("discord.com\ntwitter.com\nreddit.com");
    lay->addWidget(m_blacklistEdit);

    outer->addWidget(m_hostListWidget);
}


void SettingsWindow::buildAboutTab(QWidget* tab)
{
    QVBoxLayout* mainLay = new QVBoxLayout(tab);
    mainLay->setContentsMargins(32, 32, 32, 32);
    mainLay->setSpacing(0);

    QLabel* titleLabel = new QLabel("PktSwerve");
    titleLabel->setStyleSheet("font-size: 24px; font-weight: 700; color: #ffffff; letter-spacing: 0.5px;");
    mainLay->addWidget(titleLabel);

    QLabel* versionLabel = new QLabel("Version 1.0.0");
    versionLabel->setStyleSheet("font-size: 12px; color: #555; margin-top: 2px; margin-bottom: 24px;");
    mainLay->addWidget(versionLabel);

    QLabel* descLabel = new QLabel(
        "PktSwerve is a custom packet manipulation tool designed to bypass network "
        "restrictions and secure personal traffic. "
        );
    descLabel->setWordWrap(true);
    descLabel->setMaximumWidth(520);
    descLabel->setStyleSheet("font-size: 13px; color: #b0b0b0; line-height: 1.6; margin-bottom: 20px;");
    mainLay->addWidget(descLabel);

    QLabel* noteLabel = new QLabel(
        "This is a private utility. The developer assumes no responsibility "
        "for network issues, connection drops, or any misuse of this software."
        );
    noteLabel->setWordWrap(true);
    noteLabel->setMaximumWidth(520);
    noteLabel->setStyleSheet("font-size: 11px; color: #666; font-style: italic; line-height: 1.4; margin-bottom: 28px;");
    mainLay->addWidget(noteLabel);

    QPushButton* devBtn = new QPushButton("by Lurid Lane");
    devBtn->setCursor(Qt::PointingHandCursor);
    devBtn->setFixedWidth(120);
    devBtn->setStyleSheet(
        "QPushButton {"
        "    background: #ffffff;"
        "    color: #000000;"
        "    border: none;"
        "    border-radius: 6px;"
        "    padding: 8px 0px;"
        "    font-size: 12px;"
        "    font-weight: 600;"
        "}"
        "QPushButton:hover {"
        "    background: #d0d0d0;"
        "}"
        );

    QObject::connect(devBtn, &QPushButton::clicked, []() {
        QDesktopServices::openUrl(QUrl("https://luridlane.com"));
    });

    mainLay->addWidget(devBtn, 0, Qt::AlignLeft);

    mainLay->addStretch();
}

void SettingsWindow::onModeChanged(int /*index*/)
{
    BypassMode mode = static_cast<BypassMode>(m_modeCombo->currentData().toInt());
    bool hostListNeeded = (mode == BypassMode::BlacklistMode);
    if (m_hostListWidget)
        m_hostListWidget->setEnabled(hostListNeeded);
    LOG_DEBUG("SettingsWindow: mode changed to %1", static_cast<int>(mode));
}

void SettingsWindow::loadToUi(const BypassConfig& cfg)
{
    for (int i = 0; i < m_modeCombo->count(); ++i) {
        if (m_modeCombo->itemData(i).toInt() == static_cast<int>(cfg.mode)) {
            m_modeCombo->setCurrentIndex(i);
            break;
        }
    }
    onModeChanged(0);

    m_autoStartCheck->setChecked(m_engine->autoStart());

    m_fragHttps->setChecked(cfg.fragmentHttpsPersist);
    m_fragHttp->setChecked(cfg.fragmentHttpRequests);
    m_reverseFrag->setChecked(cfg.reverseFrag);
    m_httpFragSize->setValue(cfg.httpFragmentSize);
    m_httpsFragSize->setValue(cfg.httpsFragmentSize);
    m_fakePackets->setChecked(cfg.fakePackets);
    m_wrongChecksum->setChecked(cfg.wrongChecksum);
    m_wrongSeq->setChecked(cfg.wrongSeq);
    m_fakeTTL->setValue(cfg.fakePacketTTL);

    m_dnsRedirect->setChecked(cfg.dnsRedirect);
    m_dnsServer->setText(cfg.dnsServer);
    m_dnsPort->setValue(cfg.dnsPort);
    m_dnsPresetCombo->setCurrentIndex(0);

    m_blacklistEdit->setPlainText(cfg.blacklist.join("\n"));

    LOG_DEBUG("SettingsWindow: UI loaded from config");
}

BypassConfig SettingsWindow::uiToConfig()
{
    BypassConfig cfg;
    cfg.mode = static_cast<BypassMode>(m_modeCombo->currentData().toInt());

    cfg.fragmentHttpsPersist = m_fragHttps->isChecked();
    cfg.fragmentHttpRequests = m_fragHttp->isChecked();
    cfg.reverseFrag          = m_reverseFrag->isChecked();
    cfg.httpFragmentSize     = m_httpFragSize->value();
    cfg.httpsFragmentSize    = m_httpsFragSize->value();
    cfg.fakePackets          = m_fakePackets->isChecked();
    cfg.wrongChecksum        = m_wrongChecksum->isChecked();
    cfg.wrongSeq             = m_wrongSeq->isChecked();
    cfg.fakePacketTTL        = m_fakeTTL->value();

    cfg.dnsRedirect = m_dnsRedirect->isChecked();
    cfg.dnsServer   = m_dnsServer->text().trimmed();
    cfg.dnsPort     = m_dnsPort->value();

    auto splitLines = [](QTextEdit* t) {
        QStringList result;
        for (const auto& line : t->toPlainText().split('\n'))
            if (!line.trimmed().isEmpty()) result << line.trimmed().toLower();
        return result;
    };
    cfg.blacklist = splitLines(m_blacklistEdit);
    return cfg;
}

void SettingsWindow::onApply()
{
    LOG_INFO0("SettingsWindow::onApply called");
    BypassConfig cfg = uiToConfig();

    if (cfg.dnsServer.isEmpty()) {
        LOG_WARN0("Apply rejected: DNS server address is empty");
        QMessageBox::warning(this, "Missing Field", "DNS server address cannot be empty.");
        m_tabs->setCurrentIndex(2);
        return;
    }

    m_engine->setAutoStart(m_autoStartCheck->isChecked());
    m_engine->setConfig(cfg);

#ifdef PLATFORM_WINDOWS
    LOG_INFO0("Flushing DNS cache (ipconfig /flushdns)");
    system("ipconfig /flushdns >nul 2>&1");
#endif

    if (m_engine->isRunning()) {
        LOG_INFO0("Engine was running - restarting to apply new config");
        m_engine->stop();
        m_engine->start();
    }
    close();
}

void SettingsWindow::onDnsPresetSelected(int index)
{
    QString data = m_dnsPresetCombo->itemData(index).toString();
    if (data.isEmpty() || data == "||") return;

    QStringList parts = data.split('|');
    if (parts.size() < 2) return;
    m_dnsServer->setText(parts[0]);
    m_dnsPort->setValue(parts[1].toInt());
    LOG_INFO("DNS preset selected: %1:%2", parts[0], parts[1]);
}


void SettingsWindow::onCheckConnectivity()
{
    LOG_INFO0("DNS connectivity test started");
    m_checkBtn->setEnabled(false);
    m_dnsStatusLabel->setPlainText("Testing...");

    QString ip   = m_dnsServer->text().trimmed();
    int     port = m_dnsPort->value();
    bool wasRunning = m_engine->isRunning();

    LOG_INFO("Test target: %1:%2 | engineWasRunning=%3", ip, port, wasRunning ? "yes" : "no");

    QPointer<SettingsWindow> safeThis(this);

    auto startTest = [safeThis, ip, port, wasRunning]() {
        if (!safeThis) return;

        LOG_INFO0("DNS test: launching async worker");
        auto* watcher = new QFutureWatcher<QString>(safeThis.data());

        connect(watcher, &QFutureWatcher<QString>::finished,
                safeThis.data(), [safeThis, watcher, wasRunning]()
                {
                    if (!safeThis) { watcher->deleteLater(); return; }

                    QString result = watcher->result();

                    QString logText = result;
                    logText.replace(QRegularExpression("<[^>]*>"), "");
                    LOG_INFO("DNS test completed:\n%1", logText);


                    safeThis->m_dnsStatusLabel->setHtml(result);
                    safeThis->m_checkBtn->setEnabled(true);
                    watcher->deleteLater();

                    if (wasRunning && safeThis->m_engine) {
                        LOG_INFO0("DNS test done - restarting engine");
                        safeThis->m_engine->start();
                    }
                });

        watcher->setFuture(QtConcurrent::run([ip, port]() -> QString {
            QString result = "<div style='line-height: 1.2;'>";

            LOG_INFO0("DNS test worker: checking internet connectivity");
            {
                QTcpSocket s;
                s.connectToHost("8.8.8.8", 53);
                if (!s.waitForConnected(2000)) {
                    LOG_WARN0("DNS test: internet connectivity check FAILED");
                    return result + "<span style='color:#ff4d4d; font-size: 14px;'><b>No Internet Connection</b><br><span style='font-size:12px; color:#888;'>Please check your network settings.</span></span></div>";
                }
            }
            LOG_INFO0("DNS test: internet connectivity OK");

            struct Target { QString label; QString host; };
            const QList<Target> targets = {
                { "Discord",      "discord.com"         },
                { "Discord GW",   "gateway.discord.gg"  },
                { "Roblox",       "roblox.com"          },
                { "Twitter",      "twitter.com"         },
                { "Reddit",       "reddit.com"          },
                { "YouTube",      "youtube.com"         },
                { "Instagram",    "instagram.com"       },
                { "Telegram",     "telegram.org"        },
                { "GitHub",       "github.com"          },
                { "Spotify",      "spotify.com"         },
                { "Steam",        "steamcommunity.com"  },
                { "EA",           "ea.com"              }
            };

            qint64 totalLatency = 0;
            int successCount = 0;
            int failCount = 0;

            QString details = "";

            for (const auto& t : targets) {
                QByteArray query;
                query.append("\xAB\xCD\x01\x00\x00\x01\x00\x00\x00\x00\x00\x00", 12);
                for (const QString& part : t.host.split('.')) {
                    QByteArray p = part.toLatin1();
                    query.append(static_cast<char>(p.size()));
                    query.append(p);
                }
                query.append('\x00');
                query.append("\x00\x01\x00\x01", 4);

                QUdpSocket udp;
                udp.bind(0);

                QElapsedTimer timer;
                timer.start();
                udp.writeDatagram(query, QHostAddress(ip), static_cast<quint16>(port));

                QString resolved;
                qint64 pingMs = -1;

                if (udp.waitForReadyRead(3000)) {
                    QByteArray resp(512, 0);
                    qint64 n = udp.readDatagram(resp.data(), resp.size());
                    pingMs = timer.elapsed();

                    if (n > 12) {
                        int i = 12;
                        while (i < n && resp[i] != '\x00')
                            i += (uint8_t)resp[i] + 1;
                        i += 5;

                        int answers = ((uint8_t)resp[6] << 8) | (uint8_t)resp[7];
                        for (int a = 0; a < answers && i + 12 <= n; ++a) {
                            if (i < n && (resp[i] & 0xC0) == 0xC0) i += 2;
                            else { while (i < n && resp[i]) i++; i++; }

                            if (i + 10 > n) break;
                            uint16_t type  = ((uint8_t)resp[i] << 8) | (uint8_t)resp[i+1];
                            i += 8;
                            uint16_t rdlen = ((uint8_t)resp[i] << 8) | (uint8_t)resp[i+1];
                            i += 2;

                            if (type == 1 && rdlen == 4 && i + 4 <= n) {
                                resolved = QString("%1.%2.%3.%4")
                                .arg((uint8_t)resp[i])  .arg((uint8_t)resp[i+1])
                                    .arg((uint8_t)resp[i+2]).arg((uint8_t)resp[i+3]);
                                break;
                            }
                            i += rdlen;
                        }
                    }
                }

                if (resolved.isEmpty()) {
                    details += QString("<tr>"
                                       "<td width='16'><span style='color:#ff4d4d; font-size: 14px;'>●</span></td>"
                                       "<td width='90'><span style='color:#ccc;'>%1:</span></td>"
                                       "<td><span style='color:#666; font-size: 11px;'>- Timeout</span></td>"
                                       "</tr>").arg(t.label);
                    failCount++;
                } else {
                    QString color = "#4ade80";
                    if (pingMs > 250) color = "#ff4d4d";
                    else if (pingMs > 80) color = "#fbbf24";

                    details += QString("<tr>"
                                       "<td width='16'><span style='color:%1; font-size: 14px;'>●</span></td>"
                                       "<td width='90'><span style='color:#ccc;'>%2:</span></td>"
                                       "<td><span style='color:#777; font-size: 11px;'>- %3ms</span></td>"
                                       "</tr>").arg(color, t.label).arg(pingMs);
                    totalLatency += pingMs;
                    successCount++;
                }
            }

            result += QString("<span style='font-size: 14px; font-weight: 600; color: #ffffff;'>Target Server: %1:%2</span><br>").arg(ip, QString::number(port));

            if (failCount > 0) {
                result += QString("<span style='color:#ff4d4d; font-size: 12px;'><b>Warning:</b> %1 domain(s) failed. ISP may be blocking.</span><br>").arg(failCount);
            } else {
                result += "<span style='color:#4ade80; font-size: 12px;'><b>Status:</b> All tests passed.</span><br>";
            }

            if (successCount > 0) {
                qint64 avgLatency = totalLatency / successCount;
                QString avgColor = (avgLatency > 150) ? "#fbbf24" : "#4ade80";
                result += QString("<span style='color:#888; font-size: 12px;'>Avg Latency:</span> <span style='color:%1; font-size: 12px; font-weight:600;'>%2 ms</span>").arg(avgColor).arg(avgLatency);
            }

            result += "<hr style='margin: 8px 0 4px 0; border: none; border-top: 1px solid rgba(255,255,255,0.08);'>";

            result += "<table width='100%' cellspacing='0' cellpadding='1'>";
            result += details;
            result += "</table>";

            result += "</div>";
            return result;
        }));
    };

    if (wasRunning) {
        LOG_INFO0("DNS test: stopping engine before test to prevent WinDivert from intercepting test packets");
        auto* conn = new QMetaObject::Connection();
        *conn = connect(m_engine, &BypassEngine::stopped,
                        this, [safeThis, conn, startTest]() {
                            QObject::disconnect(*conn);
                            delete conn;
                            LOG_INFO0("DNS test: engine stopped - starting test");
                            if (safeThis) startTest();
                        }, Qt::SingleShotConnection);
        m_engine->stop();
    } else {
        startTest();
    }
}

void SettingsWindow::onResetDefaults()
{
    LOG_INFO0("SettingsWindow: Reset to defaults requested");

    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, "Reset to Defaults",
                                  "Are you sure you want to reset all settings to their default values?\n\n"
                                  "(Changes will not take effect until you click 'Apply & Save').",
                                  QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        BypassConfig defaultConfig;

        loadToUi(defaultConfig);
        LOG_INFO0("SettingsWindow: UI reset to default configuration");
    }
}