#include "SettingsWindow.h"
#include "app/DomainListEditor.h"
#include "app/Logger.h"
#include "app/Theme.h"
#include "app/ToggleSwitch.h"
#include "core/BypassEngine.h"
#include "core/DnsProbe.h"

#include <QComboBox>
#include <QFormLayout>
#include <QHostInfo>
#include <QFuture>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QSpinBox>
#include <QTabWidget>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

#include <cstdlib>

namespace {

struct DnsPreset {
    const char* name;
    const char* ip;
    int         port;
};

const DnsPreset kDnsPresets[] = {
    {"Quad9 Unfiltered (Alt Port)",    "9.9.9.10",        9953},
    {"Quad9 Secure (Alt Port 1)",      "9.9.9.9",         9953},
    {"Quad9 Secure (Alt Port 2)",      "149.112.112.112", 9953},
    {"OpenDNS Primary (Alt Port 1)",   "208.67.222.222",  5353},
    {"OpenDNS Secondary (Alt Port 1)", "208.67.220.220",  5353},
    {"OpenDNS Primary (Alt Port 2)",   "208.67.222.222",  443},
    {"OpenDNS Secondary (Alt Port 2)", "208.67.220.220",  443},
    {"AdGuard Default (Alt Port 1)",   "94.140.14.14",    5353},
    {"AdGuard Default (Alt Port 2)",   "94.140.15.15",    5353},
    {"AdGuard Family (Alt Port)",      "94.140.14.15",    5353},
    {"Yandex Basic (Alt Port)",        "77.88.8.8",       1253},
    {"Yandex Safe (Alt Port)",         "77.88.8.88",      1253},
    {"CleanBrowsing Security (Alt)",   "185.228.168.9",   5353},
    {"Cloudflare Primary",             "1.1.1.1",         53},
    {"Cloudflare Secondary",           "1.0.0.1",         53},
    {"Cloudflare Malware Block",       "1.1.1.2",         53},
    {"Cloudflare Family Block",        "1.1.1.3",         53},
    {"Google Primary",                 "8.8.8.8",         53},
    {"Google Secondary",               "8.8.4.4",         53},
    {"Quad9 Secure Primary",           "9.9.9.9",         53},
    {"OpenDNS Primary",                "208.67.222.222",  53},
    {"AdGuard Default",                "94.140.14.14",    53},
    {"Yandex Basic",                   "77.88.8.8",       53},
    {"ControlD Unfiltered",            "76.76.2.0",       53},
};

#ifdef PLATFORM_MACOS
// The test runs as root, which the pf rules skip, so the engine can stay up.
constexpr bool kProbeNeedsEngineOff = false;
#else
constexpr bool kProbeNeedsEngineOff = true;
#endif

QLabel* makeLabel(const QString& text, const char* name)
{
    auto* label = new QLabel(text);
    label->setObjectName(name);
    label->setWordWrap(true);
    return label;
}

QGroupBox* makeGroup(const QString& title)
{
    auto* group = new QGroupBox(title);
    auto* lay = new QVBoxLayout(group);
    lay->setSpacing(10);
    return group;
}

void addForm(QGroupBox* group, QFormLayout* form)
{
    static_cast<QVBoxLayout*>(group->layout())->addLayout(form);
}

QCheckBox* addSwitch(QGroupBox* group, const QString& text, const QString& note = {})
{
    auto* box = new SwitchBox(text);
    group->layout()->addWidget(box);
    if (!note.isEmpty())
        group->layout()->addWidget(makeLabel(note, "note"));
    return box;
}

QSpinBox* makeSpin(int min, int max)
{
    auto* spin = new QSpinBox;
    spin->setRange(min, max);
    return spin;
}

QVBoxLayout* makePageLayout(QWidget* page, int margin = 4)
{
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(margin, 8, margin, margin);
    lay->setSpacing(16);
    return lay;
}

void flushDnsCache()
{
#ifdef PLATFORM_WINDOWS
    LOG_INFO0("Flushing DNS cache (ipconfig /flushdns)");
    system("ipconfig /flushdns >nul 2>&1");
#elif defined(PLATFORM_MACOS)
    LOG_INFO0("Flushing DNS cache (dscacheutil + mDNSResponder)");
    QProcess::execute("/usr/bin/dscacheutil", {"-flushcache"});
    QProcess::execute("/usr/bin/killall", {"-HUP", "mDNSResponder"});
#endif
}

// Runs on a worker thread; returns the HTML shown in the test box.
QString probeHtml(const QString& ip, int port)
{
    const QString good = Theme::green.name();
    const QString warn = Theme::orange.name();
    const QString bad  = Theme::red.name();
    const QString dim  = Theme::subtext.name();

    QHostAddress server;
    if (!server.setAddress(ip))
        return QString("<span style='color:%1;'><b>Invalid DNS server address.</b></span>").arg(bad);

    const DnsProbeReport report = DnsProbe::probe(
        server, static_cast<quint16>(port), DnsProbe::defaultTargets());

    QString html = QString("<span style='font-size:14px; font-weight:600;'>Target Server: %1:%2</span><br>")
                       .arg(ip).arg(port);
    if (!report.serverError.isEmpty())
        return html + QString("<span style='color:%1;'><b>Test error:</b> %2</span>")
                          .arg(bad, report.serverError.toHtmlEscaped());

    qint64 totalMs = 0;
    int succeeded = 0;
    QString rows;
    for (const DnsProbeTargetResult& r : report.targets) {
        const QString color = !r.succeeded ? bad : r.latencyMs > 250 ? bad : r.latencyMs > 80 ? warn : good;
        const QString detail = r.succeeded ? QString("%1 ms").arg(r.latencyMs) : r.error.toHtmlEscaped();
        rows += QString("<tr><td width='16'><span style='color:%1;'>&#9679;</span></td>"
                        "<td width='105'>%2</td><td style='color:%3;'>%4</td></tr>")
                    .arg(color, r.target.label.toHtmlEscaped(), dim, detail);
        if (r.succeeded) {
            totalMs += r.latencyMs;
            ++succeeded;
        }
    }

    for (const DnsProbeTargetResult& r : report.targets)
        LOG_INFO("DNS test | %1:%2 | %3 | %4", ip, port, r.target.host,
                 r.succeeded ? QString("%1 ms").arg(r.latencyMs) : r.error);

    const int failures = report.targets.size() - succeeded;
    html += failures == 0
        ? QString("<span style='color:%1;'><b>Status:</b> Resolver returned usable answers.</span><br>").arg(good)
        : QString("<span style='color:%1;'><b>Resolver warning:</b> %2 domain(s) had no usable answer.</span><br>")
              .arg(warn).arg(failures);
    if (succeeded > 0)
        html += QString("<span style='color:%1;'>Average latency:</span> %2 ms<br>").arg(dim).arg(totalMs / succeeded);

    html += "<table width='100%' cellspacing='0' cellpadding='2'>" + rows + "</table>";

    if (succeeded == 0 && !report.targets.isEmpty()) {
        // Tell "this resolver/port is filtered" apart from "no network at all".
        const bool online = QHostInfo::fromName("example.com").error() == QHostInfo::NoError;
        LOG_WARN("DNS test | no answers from %1:%2 | system resolver %3", ip, port, online ? "works" : "fails");
        html += QString("<br><span style='color:%1;'>%2</span>").arg(warn, online
            ? "Your internet works, but this server did not answer on this port. Your network is "
              "probably blocking it. Pick another preset (a port 53 one such as Cloudflare or "
              "Google is the most likely to pass), then test again."
            : "The system resolver failed as well, so there may be no internet connection, or a "
              "VPN or firewall is blocking DNS.");
    }
    return html;
}

}


SettingsWindow::SettingsWindow(BypassEngine* engine, QWidget* parent)
    : QMainWindow(parent), m_engine(engine)
{
    setWindowTitle("PktSwerve Settings");
    setMinimumSize(680, 720);
    buildUi();
    loadToUi(m_engine->config());

    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_W), this, this, &QWidget::close);
    connect(m_engine, &BypassEngine::configChanged, this, [this] { loadToUi(m_engine->config()); });
}

void SettingsWindow::buildUi()
{
    auto* central = new QWidget;
    setCentralWidget(central);

    auto* mainLay = new QVBoxLayout(central);
    mainLay->setContentsMargins(20, 16, 20, 18);
    mainLay->setSpacing(12);

    using Builder = void (SettingsWindow::*)(QWidget*);
    const std::pair<const char*, Builder> tabs[] = {
        {"General",     &SettingsWindow::buildGeneralTab},
        {"Methods",     &SettingsWindow::buildMethodsTab},
        {"DNS",         &SettingsWindow::buildDnsTab},
        {"Domain List", &SettingsWindow::buildHostlistTab},
        {"About",       &SettingsWindow::buildAboutTab},
    };
    m_tabs = new QTabWidget;
    for (const auto& [title, build] : tabs) {
        auto* page = new QWidget;
        (this->*build)(page);
        m_tabs->addTab(page, title);
    }
    mainLay->addWidget(m_tabs, 1);

    auto* resetBtn  = new QPushButton("Reset to Defaults");
    auto* cancelBtn = new QPushButton("Cancel");
    auto* applyBtn  = new QPushButton("Apply");
    resetBtn->setObjectName("destructiveBtn");
    applyBtn->setObjectName("primaryBtn");

    auto* buttons = new QHBoxLayout;
    buttons->addWidget(resetBtn);
    buttons->addStretch();
    buttons->addWidget(cancelBtn);
    buttons->addWidget(applyBtn);
    mainLay->addLayout(buttons);

    connect(resetBtn,  &QPushButton::clicked, this, &SettingsWindow::onResetDefaults);
    connect(cancelBtn, &QPushButton::clicked, this, &QWidget::close);
    connect(applyBtn,  &QPushButton::clicked, this, &SettingsWindow::onApply);
}


void SettingsWindow::buildGeneralTab(QWidget* tab)
{
    QVBoxLayout* lay = makePageLayout(tab);

    QGroupBox* modeGroup = makeGroup("OPERATING MODE");
    m_modeCombo = new QComboBox;
    m_modeCombo->addItem("Disabled",            static_cast<int>(BypassMode::Disabled));
    m_modeCombo->addItem("Selected Sites Only", static_cast<int>(BypassMode::BlacklistMode));
    m_modeCombo->addItem("Global",              static_cast<int>(BypassMode::GlobalMode));
    modeGroup->layout()->addWidget(m_modeCombo);
    modeGroup->layout()->addWidget(makeLabel(
        "Selected Sites Only bypasses just the domains in the Domain List tab.\n"
        "Global affects all HTTP/HTTPS traffic.", "note"));
    lay->addWidget(modeGroup);

    QGroupBox* sysGroup = makeGroup("SYSTEM");
#ifdef PLATFORM_MACOS
    m_autoStartCheck = addSwitch(sysGroup, "Launch at login");
#else
    m_autoStartCheck = addSwitch(sysGroup, "Launch at Windows startup");
#endif
    lay->addWidget(sysGroup);
    lay->addStretch();

    connect(m_modeCombo, &QComboBox::currentIndexChanged, this, &SettingsWindow::onModeChanged);
}


void SettingsWindow::buildMethodsTab(QWidget* tab)
{
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* content = new QWidget;
    scroll->setWidget(content);
    makePageLayout(tab, 0)->addWidget(scroll);

    QVBoxLayout* lay = makePageLayout(content);

    QGroupBox* fragGroup = makeGroup("FRAGMENTATION");
    m_fragHttps = addSwitch(fragGroup, "HTTPS / TLS ClientHello fragmentation",
        "Splits the TLS handshake packet so the SNI (target domain) doesn't appear "
        "in a single packet. Primary bypass method.");
    m_fragHttp = addSwitch(fragGroup, "HTTP fragmentation",
        "Fragments plaintext HTTP on port 80. Rarely needed since most sites use HTTPS.");
    m_reverseFrag = addSwitch(fragGroup, "Send fragments in reverse order",
        "Sends fragment 2 before fragment 1. May confuse additional DPI systems; "
        "disable if connections break.");
#ifdef PLATFORM_MACOS
    m_tlsRecSplit = addSwitch(fragGroup, "Split TLS record at the SNI",
        "Re-frames the ClientHello as two TLS records cut in the middle of the domain name, "
        "so DPI that reassembles TCP segments still can't read it. "
        "Disable if some sites stop loading.");
#endif

    m_httpFragSize  = makeSpin(1, 512);
    m_httpsFragSize = makeSpin(1, 512);
    auto* sizes = new QFormLayout;
    sizes->addRow("HTTP fragment size (bytes):",  m_httpFragSize);
    sizes->addRow("HTTPS fragment size (bytes):", m_httpsFragSize);
    addForm(fragGroup, sizes);
    fragGroup->layout()->addWidget(makeLabel(
        "Default of 2 bytes is optimal. Larger values may bypass certain DPI types.", "note"));
    lay->addWidget(fragGroup);

    QGroupBox* fakeGroup = makeGroup("DECEPTION PACKET TECHNIQUES");
    m_fakePackets = addSwitch(fakeGroup, "Low-TTL fake packet",
        "Sends a decoy packet the DPI sees but drops before reaching the real server "
        "due to low TTL. Works best combined with fragmentation.");
    m_wrongChecksum = addSwitch(fakeGroup, "Wrong TCP checksum fake packet",
        "Confuses DPI systems that skip checksum validation. The real server rejects "
        "the packet, but the DPI context table is corrupted.");
    m_wrongSeq = addSwitch(fakeGroup, "Wrong sequence number fake packet",
        "Disrupts DPI TCP stream reassembly. Aggressive; try disabling if other methods "
        "cause issues.");

    m_voiceUdp = addSwitch(fakeGroup, "Voice call decoys (UDP)",
        "Sends a few decoy datagrams before the 74-byte discovery packet that starts a "
        "voice call, so the call connects. Applies to every app that makes such a call.");

    m_fakeTTL = makeSpin(1, 128);
    m_fakeTTL->setValue(8);
    auto* ttl = new QFormLayout;
    ttl->addRow("Fake packet TTL:", m_fakeTTL);
    addForm(fakeGroup, ttl);
    fakeGroup->layout()->addWidget(makeLabel(
        "Highest fake packet lifetime in hops. It is lowered per server once the distance is known; "
        "too high and the packet may reach the destination.", "note"));
    lay->addWidget(fakeGroup);
    lay->addStretch();

#ifdef PLATFORM_MACOS
    // The proxy backend cannot reorder segments or craft invalid packets.
    const QString unsupported = "Not available on macOS (proxy-based backend).";
    for (QWidget* w : QList<QWidget*>{m_reverseFrag, m_fakePackets, m_wrongChecksum, m_wrongSeq, m_voiceUdp, m_fakeTTL}) {
        w->setEnabled(false);
        w->setToolTip(unsupported);
    }
    fakeGroup->setToolTip(unsupported);
#endif
}


void SettingsWindow::buildDnsTab(QWidget* tab)
{
    QVBoxLayout* lay = makePageLayout(tab);

    QGroupBox* enableGroup = makeGroup("DNS REDIRECT");
    m_dnsRedirect = addSwitch(enableGroup, "Enable DNS redirect",
        "Routes DNS queries to the server below instead of your ISP's DNS.");
    lay->addWidget(enableGroup);

    QGroupBox* presetGroup = makeGroup("PRESET SERVERS");
    m_dnsPresetCombo = new QComboBox;
    m_dnsPresetCombo->addItem("Manual entry");
    for (const DnsPreset& p : kDnsPresets)
        m_dnsPresetCombo->addItem(QString("%1 — %2:%3").arg(p.name, p.ip).arg(p.port));
    presetGroup->layout()->addWidget(m_dnsPresetCombo);
    connect(m_dnsPresetCombo, &QComboBox::currentIndexChanged, this, &SettingsWindow::onDnsPresetSelected);

    QGroupBox* addressGroup = new QGroupBox("SERVER ADDRESS");
    auto* form = new QFormLayout(addressGroup);
    m_dnsServer = new QLineEdit;
    m_dnsServer->setPlaceholderText("e.g. 9.9.9.10");
    m_dnsPort = makeSpin(1, 65535);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->addRow("IP Address:", m_dnsServer);
    form->addRow("Port:",       m_dnsPort);

    QGroupBox* testGroup = makeGroup("CONNECTIVITY TEST");
    m_checkBtn = new QPushButton("Test Connection");
    m_checkBtn->setObjectName("primaryBtn");
    connect(m_checkBtn, &QPushButton::clicked, this, &SettingsWindow::onCheckConnectivity);
    m_dnsStatusLabel = new QTextEdit;
    m_dnsStatusLabel->setReadOnly(true);
    testGroup->layout()->addWidget(m_checkBtn);
    testGroup->layout()->addWidget(m_dnsStatusLabel);

    auto* left = new QVBoxLayout;
    left->addWidget(presetGroup);
    left->addWidget(addressGroup);
    left->addStretch();

    auto* columns = new QHBoxLayout;
    columns->setSpacing(16);
    columns->addLayout(left, 1);
    columns->addWidget(testGroup, 1);
    lay->addLayout(columns, 1);
}


void SettingsWindow::buildHostlistTab(QWidget* tab)
{
    m_hostListWidget = tab;
    QVBoxLayout* lay = makePageLayout(tab);

    lay->addWidget(makeLabel(
        "Type a domain and press Enter. Subdomains match automatically "
        "(e.g. 'example.com' also covers 'api.example.com').", "note"));
    lay->addWidget(makeLabel("Bypass List", "heading"));

    m_blacklist = new DomainListEditor;
    lay->addWidget(m_blacklist, 1);
}


void SettingsWindow::buildAboutTab(QWidget* tab)
{
    QVBoxLayout* lay = makePageLayout(tab, 20);
    lay->setSpacing(6);

    lay->addWidget(makeLabel("PktSwerve", "title"));
    lay->addWidget(makeLabel("Version " + QCoreApplication::applicationVersion(), "note"));
    lay->addSpacing(14);
    lay->addWidget(makeLabel("Developed by Lurid Lane · Jadis0x", "note"));
    lay->addSpacing(14);
    lay->addWidget(makeLabel(
        "PktSwerve is a custom packet manipulation tool designed to bypass network "
        "restrictions and secure personal traffic.", "body"));
    lay->addSpacing(14);
    lay->addWidget(makeLabel(
        "This is a private utility. The developer assumes no responsibility for network "
        "issues, connection drops, or any misuse of this software.", "meta"));
    lay->addStretch();
}


void SettingsWindow::onModeChanged(int)
{
    const auto mode = static_cast<BypassMode>(m_modeCombo->currentData().toInt());
    m_hostListWidget->setEnabled(mode == BypassMode::BlacklistMode);
}

void SettingsWindow::loadToUi(const BypassConfig& cfg)
{
    const int modeIndex = m_modeCombo->findData(static_cast<int>(cfg.mode));
    if (modeIndex >= 0)
        m_modeCombo->setCurrentIndex(modeIndex);
    onModeChanged(0);

    m_autoStartCheck->setChecked(m_engine->autoStart());

    m_fragHttps->setChecked(cfg.fragmentHttpsPersist);
    m_fragHttp->setChecked(cfg.fragmentHttpRequests);
    m_reverseFrag->setChecked(cfg.reverseFrag);
    if (m_tlsRecSplit)
        m_tlsRecSplit->setChecked(cfg.tlsRecordSplit);
    m_httpFragSize->setValue(cfg.httpFragmentSize);
    m_httpsFragSize->setValue(cfg.httpsFragmentSize);
    m_fakePackets->setChecked(cfg.fakePackets);
    m_voiceUdp->setChecked(cfg.fakeVoiceUdp);
    m_wrongChecksum->setChecked(cfg.wrongChecksum);
    m_wrongSeq->setChecked(cfg.wrongSeq);
    m_fakeTTL->setValue(cfg.fakePacketTTL);

    m_dnsRedirect->setChecked(cfg.dnsRedirect);
    m_dnsServer->setText(cfg.dnsServer);
    m_dnsPort->setValue(cfg.dnsPort);
    m_dnsPresetCombo->setCurrentIndex(0);

    m_blacklist->setDomains(cfg.blacklist);
}

BypassConfig SettingsWindow::uiToConfig()
{
    BypassConfig cfg;
    cfg.mode = static_cast<BypassMode>(m_modeCombo->currentData().toInt());

    cfg.fragmentHttpsPersist = m_fragHttps->isChecked();
    cfg.fragmentHttpRequests = m_fragHttp->isChecked();
    cfg.reverseFrag          = m_reverseFrag->isChecked();
    cfg.tlsRecordSplit       = m_tlsRecSplit && m_tlsRecSplit->isChecked();
    cfg.httpFragmentSize     = m_httpFragSize->value();
    cfg.httpsFragmentSize    = m_httpsFragSize->value();
    cfg.fakePackets          = m_fakePackets->isChecked();
    cfg.fakeVoiceUdp         = m_voiceUdp->isChecked();
    cfg.wrongChecksum        = m_wrongChecksum->isChecked();
    cfg.wrongSeq             = m_wrongSeq->isChecked();
    cfg.fakePacketTTL        = m_fakeTTL->value();

    cfg.dnsRedirect = m_dnsRedirect->isChecked();
    cfg.dnsServer   = m_dnsServer->text().trimmed();
    cfg.dnsPort     = m_dnsPort->value();

    cfg.blacklist = m_blacklist->domains();
    return cfg;
}

void SettingsWindow::onApply()
{
    const BypassConfig cfg = uiToConfig();

    if (cfg.dnsServer.isEmpty()) {
        LOG_WARN0("Apply rejected: DNS server address is empty");
        QMessageBox::warning(this, "Missing Field", "DNS server address cannot be empty.");
        m_tabs->setCurrentIndex(2);
        return;
    }

    m_engine->setAutoStart(m_autoStartCheck->isChecked());
    m_engine->setConfig(cfg);
    flushDnsCache();

    if (m_engine->isRunning()) {
        LOG_INFO0("Restarting engine to apply new config");
        m_engine->stop();
        m_engine->start();
    }
    close();
}

void SettingsWindow::onDnsPresetSelected(int index)
{
    if (index <= 0)
        return;
    const DnsPreset& preset = kDnsPresets[index - 1];
    m_dnsServer->setText(preset.ip);
    m_dnsPort->setValue(preset.port);
    LOG_INFO("DNS preset selected: %1:%2", preset.ip, preset.port);
}

void SettingsWindow::onCheckConnectivity()
{
    m_checkBtn->setEnabled(false);
    m_dnsStatusLabel->setPlainText("Testing...");

    if (!kProbeNeedsEngineOff || !m_engine->isRunning()) {
        runDnsProbe(false);
        return;
    }

    // The engine would intercept the test queries, so stop it first.
    connect(m_engine, &BypassEngine::stopped, this, [this] { runDnsProbe(true); },
            Qt::SingleShotConnection);
    m_engine->stop();
}

void SettingsWindow::runDnsProbe(bool restartEngine)
{
    const QString ip = m_dnsServer->text().trimmed();
    const int port   = m_dnsPort->value();
    LOG_INFO("DNS test started: %1:%2", ip, port);

    auto* watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, restartEngine] {
        m_dnsStatusLabel->setHtml(watcher->result());
        m_checkBtn->setEnabled(true);
        watcher->deleteLater();
        if (restartEngine)
            m_engine->start();
    });
    watcher->setFuture(QtConcurrent::run(probeHtml, ip, port));
}

void SettingsWindow::onResetDefaults()
{
    const auto reply = QMessageBox::question(this, "Reset to Defaults",
        "Reset all settings to their default values?\n\n"
        "Changes take effect only after you click Apply.");
    if (reply == QMessageBox::Yes)
        loadToUi(BypassConfig{});
}
