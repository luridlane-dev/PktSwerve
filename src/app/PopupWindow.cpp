#include "PopupWindow.h"
#include "app/Logger.h"
#include "app/Theme.h"
#include "app/ToggleSwitch.h"
#include "core/BypassEngine.h"

#include <QApplication>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScreen>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <cstdlib>
#endif

namespace {

constexpr int kWidth      = 300;
constexpr int kLogoSize   = 72;
constexpr int kRowHeight  = 44;
constexpr int kEdge       = 16;
constexpr int kFadeMs     = 300;

const char* kStyle = R"(
    QLabel#statusLabel { font-size: 15px; font-weight: 600; color: @subtext; }
    QLabel#statusLabel[state="on"]    { color: @green; }
    QLabel#statusLabel[state="error"] { color: @red; }

    QFrame#card { background: @card; border-radius: 12px; }
    QFrame#separator { background: @line; }

    QLabel#rowTitle { font-size: 14px; }
    QLabel#rowTitle[danger="true"] { color: @red; }
    QLabel#rowValue { font-size: 14px; color: @subtext; }

    QPushButton#rowBtn {
        background: transparent;
        border: none;
        border-radius: 0;
        padding: 0;
    }
    QPushButton#rowBtn:hover   { background: @fill; }
    QPushButton#rowBtn:pressed { background: @fillHigh; }
    QPushButton#rowBtn[edge="first"] { border-top-left-radius: 12px; border-top-right-radius: 12px; }
    QPushButton#rowBtn[edge="last"]  { border-bottom-left-radius: 12px; border-bottom-right-radius: 12px; }
)";

QPixmap loadLogo(const QString& path, qreal dpr)
{
    QPixmap px(path);
    if (px.isNull()) {
        LOG_ERROR("Resource loading failed! Path: %1", path);
        return px;
    }
    px = px.scaled(QSize(kLogoSize, kLogoSize) * dpr, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    px.setDevicePixelRatio(dpr);
    return px;
}

QString modeName(BypassMode mode)
{
    switch (mode) {
    case BypassMode::Disabled:      return "Disabled";
    case BypassMode::BlacklistMode: return "Selected Sites";
    case BypassMode::WhitelistMode: return "Whitelist";
    case BypassMode::GlobalMode:    return "Global";
    }
    return {};
}

QLabel* makeLabel(const QString& text, const char* name, bool transparentToMouse = false)
{
    auto* label = new QLabel(text);
    label->setObjectName(name);
    label->setAttribute(Qt::WA_TransparentForMouseEvents, transparentToMouse);
    return label;
}

QWidget* makeValueRow(const QString& title, QLabel* value)
{
    auto* row = new QWidget;
    row->setFixedHeight(kRowHeight);
    auto* lay = new QHBoxLayout(row);
    lay->setContentsMargins(kEdge, 0, kEdge, 0);
    lay->addWidget(makeLabel(title, "rowTitle"));
    lay->addStretch();
    lay->addWidget(value);
    return row;
}

QPushButton* makeButtonRow(const QString& title, const char* edge, bool danger, bool chevron)
{
    auto* btn = new QPushButton;
    btn->setObjectName("rowBtn");
    btn->setProperty("edge", edge);
    btn->setFixedHeight(kRowHeight);
    btn->setCursor(Qt::PointingHandCursor);

    auto* lay = new QHBoxLayout(btn);
    lay->setContentsMargins(kEdge, 0, kEdge, 0);
    QLabel* label = makeLabel(title, "rowTitle", true);
    label->setProperty("danger", danger);
    lay->addWidget(label);
    lay->addStretch();
    if (chevron)
        lay->addWidget(makeLabel("›", "rowValue", true));
    return btn;
}

QFrame* makeCard(const QList<QWidget*>& rows)
{
    auto* card = new QFrame;
    card->setObjectName("card");
    auto* lay = new QVBoxLayout(card);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    for (QWidget* row : rows) {
        if (row != rows.first()) {
            auto* sep = new QFrame;
            sep->setObjectName("separator");
            sep->setFixedHeight(1);
            lay->addWidget(sep);
        }
        lay->addWidget(row);
    }
    return card;
}

void repolish(QWidget* w)
{
    w->style()->unpolish(w);
    w->style()->polish(w);
}

}


PopupWindow::PopupWindow(BypassEngine* engine, QWidget* parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint),
      m_engine(engine)
{
    setAttribute(Qt::WA_TranslucentBackground);

    const qreal dpr = devicePixelRatioF();
    m_activeLogo = loadLogo(":/icons/active.png", dpr);
    m_idleLogo   = loadLogo(":/icons/inactive.png", dpr);

    buildUi();
    qApp->installEventFilter(this);

    connect(m_engine, &BypassEngine::started,       this, &PopupWindow::updateState);
    connect(m_engine, &BypassEngine::stopped,       this, &PopupWindow::updateState);
    connect(m_engine, &BypassEngine::configChanged, this, &PopupWindow::updateState);
    connect(m_engine, &BypassEngine::errorOccurred, this, &PopupWindow::onEngineError);

    connect(qApp, &QApplication::focusWindowChanged, this, [this](QWindow* win) {
        if (isVisible() && !win)
            hide();
    });

    applyState();
}

bool PopupWindow::eventFilter(QObject*, QEvent* e)
{
    if (!isVisible())
        return false;

    switch (e->type()) {
    case QEvent::MouseButtonPress:
        if (!geometry().contains(static_cast<QMouseEvent*>(e)->globalPosition().toPoint()))
            hide();
        break;
    case QEvent::ApplicationDeactivate:
    case QEvent::WindowDeactivate:
        hide();
        break;
    default:
        break;
    }
    return false;
}

void PopupWindow::showAtTray(const QRect& trayGeo)
{
    show();
    raise();
    activateWindow();

    QScreen* screen = QGuiApplication::screenAt(trayGeo.center());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    const QRect avail = screen->availableGeometry();

    // Tray at the top (macOS menu bar): open under the icon. Otherwise bottom-right.
    const bool trayOnTop = trayGeo.isValid() && trayGeo.center().y() < avail.center().y();
    int x = trayOnTop ? trayGeo.center().x() - width() / 2 : avail.right() - width() - 12;
    const int y = trayOnTop ? avail.top() + 8 : avail.bottom() - height() - 12;

    x = qBound(avail.left() + 8, x, avail.right() - width() - 8);
    move(x, y);
    setFocus();
}

void PopupWindow::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(Theme::bg);
    p.setPen(QPen(QColor(255, 255, 255, 36), 1));
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 18, 18);
}

void PopupWindow::buildUi()
{
    setStyleSheet(Theme::style(kStyle));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(kEdge, 22, kEdge, 14);
    root->setSpacing(0);
    root->setSizeConstraint(QLayout::SetFixedSize);

    m_hero = new QWidget;
    m_hero->setFixedWidth(kWidth - 2 * kEdge);
    auto* hero = new QVBoxLayout(m_hero);
    hero->setContentsMargins(0, 0, 0, 0);
    hero->setSpacing(0);

    m_logoLabel = new QLabel;
    m_logoLabel->setFixedSize(kLogoSize, kLogoSize);
    hero->addWidget(m_logoLabel, 0, Qt::AlignHCenter);
    hero->addSpacing(14);

    m_toggle = new ToggleSwitch;
    connect(m_toggle, &ToggleSwitch::clicked, this, &PopupWindow::onToggleClicked);
    hero->addWidget(m_toggle, 0, Qt::AlignHCenter);
    hero->addSpacing(10);

    m_statusLabel = makeLabel({}, "statusLabel");
    m_statusLabel->setAlignment(Qt::AlignHCenter);
    hero->addWidget(m_statusLabel);

    m_heroFade = new QGraphicsOpacityEffect(m_hero);
    m_hero->setGraphicsEffect(m_heroFade);
    m_fade = new QPropertyAnimation(m_heroFade, "opacity", this);
    m_fade->setDuration(kFadeMs);
    m_fade->setKeyValueAt(0.0, 1.0);
    m_fade->setKeyValueAt(0.4, 0.0);
    m_fade->setKeyValueAt(1.0, 1.0);

    root->addWidget(m_hero);
    root->addSpacing(20);

    m_dnsValue  = makeLabel({}, "rowValue");
    m_modeValue = makeLabel({}, "rowValue");
    root->addWidget(makeCard({makeValueRow("DNS", m_dnsValue), makeValueRow("Mode", m_modeValue)}));
    root->addSpacing(12);

    QPushButton* settingsBtn = makeButtonRow("Settings", "first", false, true);
    QPushButton* quitBtn     = makeButtonRow("Quit", "last", true, false);
    connect(settingsBtn, &QPushButton::clicked, this, [this] { hide(); emit settingsRequested(); });
    connect(quitBtn,     &QPushButton::clicked, this, &PopupWindow::onQuitClicked);
    root->addWidget(makeCard({settingsBtn, quitBtn}));
    root->addSpacing(14);

    QLabel* credit = makeLabel("Developed by Lurid Lane", "meta");
    credit->setAlignment(Qt::AlignHCenter);
    root->addWidget(credit);
}

void PopupWindow::setStatus(const QString& state, const QString& text)
{
    m_statusLabel->setProperty("state", state);
    m_statusLabel->setText(text);
    repolish(m_statusLabel);
}

void PopupWindow::updateState()
{
    m_toggle->setActive(m_engine->isRunning());
    if (!isVisible()) {
        applyState();
        return;
    }

    // Swap the content while the hero is fully faded out.
    m_fade->stop();
    m_fade->start();
    QTimer::singleShot(kFadeMs * 4 / 10, this, &PopupWindow::applyState);
}

void PopupWindow::applyState()
{
    const bool running = m_engine->isRunning();
    const BypassConfig& cfg = m_engine->config();

    setStatus(running ? "on" : "off", running ? "Connected" : "Disconnected");
    m_logoLabel->setPixmap(running ? m_activeLogo : m_idleLogo);
    m_dnsValue->setText(cfg.dnsRedirect ? cfg.dnsServer : "Off");
    m_modeValue->setText(modeName(cfg.mode));
}

void PopupWindow::onToggleClicked()
{
    LOG_INFO("PopupWindow: toggle clicked - %1", m_engine->isRunning() ? "stopping" : "starting");
    if (m_engine->isRunning())
        m_engine->stop();
    else
        m_engine->start();
}

void PopupWindow::onEngineError(const QString& message)
{
    LOG_ERROR("PopupWindow: engine error received: %1", message);
    setStatus("error", "Error");
}

void PopupWindow::onQuitClicked()
{
    LOG_INFO0("PopupWindow: quit clicked");
    hide();
    if (m_engine->isRunning()) {
        m_engine->blockSignals(true);
        m_engine->stop();
    }

    // Skip Qt teardown; the engine is already stopped.
#ifdef Q_OS_WIN
    ::TerminateProcess(::GetCurrentProcess(), 0);
#else
    std::_Exit(0);
#endif
}
