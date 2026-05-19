#include "PopupWindow.h"
#include <windows.h>
#include "core/BypassEngine.h"
#include "app/ToggleSwitch.h"
#include "app/Logger.h"
#include <QApplication>
#include <QStyle>
#include <QScreen>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QPainter>
#include <QPainterPath>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <QGraphicsDropShadowEffect>
#include <QSequentialAnimationGroup>
#include <QFocusEvent>
#include <QMouseEvent>

static QString fmtBytes(quint64 b)
{
    if (b < 1024)        return QString("%1 B").arg(b);
    if (b < 1024 * 1024) return QString("%1 KB").arg(b / 1024);
    return QString("%1 MB").arg(b / (1024 * 1024));
}


PopupWindow::PopupWindow(BypassEngine* engine, QWidget* parent)
    : QWidget(parent,
              Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint),
    m_engine(engine)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setFixedSize(300, 380);

    buildUi();

    qApp->installEventFilter(this);

    connect(m_engine, &BypassEngine::started,       this, &PopupWindow::onEngineStarted);
    connect(m_engine, &BypassEngine::stopped,       this, &PopupWindow::onEngineStopped);
    connect(m_engine, &BypassEngine::errorOccurred, this, &PopupWindow::onEngineError);
    connect(m_engine, &BypassEngine::statsUpdated,  this, &PopupWindow::onStatsUpdated);
    connect(m_engine, &BypassEngine::configChanged, this, &PopupWindow::updateState);

    connect(qApp, &QApplication::focusWindowChanged, this, [this](QWindow* win) {
        if (isVisible() && win == nullptr) {
            hide();
        }
    });

    LOG_INFO0("PopupWindow created");
    updateState();
}


bool PopupWindow::eventFilter(QObject* obj, QEvent* e)
{
    if (isVisible() && e->type() == QEvent::MouseButtonPress) {
        QPoint globalPos = static_cast<QMouseEvent*>(e)->globalPosition().toPoint();
        if (!geometry().contains(globalPos)) {
            hide();
        }
    }
    if (e->type() == QEvent::ApplicationDeactivate) {
        hide();
    }
    return false;
}


void PopupWindow::showAtTray(const QRect& trayGeo)
{

    setFixedSize(320, 420);
    show();
    raise();
    activateWindow();

    QScreen* screen = QGuiApplication::screenAt(trayGeo.center());
    if (!screen) screen = QGuiApplication::primaryScreen();
    QRect avail = screen->availableGeometry();

    int x = avail.right() - width() - 12;
    int y = avail.bottom() - height() - 12;

    if (x < avail.left()) x = avail.left() + 8;
    if (x + width() > avail.right()) x = avail.right() - width() - 8;

    move(x, y);
    setFocus();
}


void PopupWindow::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(rect().adjusted(1, 1, -1, -1), 14, 14);

    p.fillPath(path, Qt::black);
    p.setPen(QPen(QColor(255, 255, 255, 20), 1));
    p.drawPath(path);
}

void PopupWindow::focusOutEvent(QFocusEvent* e)
{
    LOG_DEBUG("PopupWindow focusOutEvent | reason=%1", static_cast<int>(e->reason()));
    QWidget::focusOutEvent(e);
}

void PopupWindow::buildUi()
{
    setStyleSheet(R"(
        QWidget { color: #e0e0e0; font-family: 'Segoe UI', sans-serif; }

        QLabel#brandLabel { font-size: 16px; font-weight: 700; color: #ffffff; letter-spacing: 0.3px; }
        QLabel#devLabel   { font-size: 10px; color: #444444; }

        QLabel#statusLabel              { font-size: 13px; font-weight: 500; color: #555555; }
        QLabel#statusLabel[active="true"] { color: #4ade80; font-weight: 700; }

        QLabel#dnsLabel              { font-size: 17px; font-weight: 600; color: #444444; }
        QLabel#dnsLabel[active="true"] { color: #ffffff; }

        QPushButton#settingsBtn {
            background: transparent;
            border: none;
            color: #444444;
            font-size: 18px;
            padding: 0;
        }
        QPushButton#settingsBtn:hover { color: #ffffff; }

        QPushButton#quitBtn {
            background: #0a0a0a;
            border: 1px solid rgba(255, 255, 255, 0.08);
            border-radius: 10px;
            padding: 10px;
            font-size: 12px;
            color: #666666;
        }
        QPushButton#quitBtn:hover { background: #141414; color: #ffffff; border-color: #444444; }
    )");

    QWidget* inner = new QWidget(this);
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->addWidget(inner);

    QVBoxLayout* lay = new QVBoxLayout(inner);
    lay->setContentsMargins(24, 18, 24, 20);
    lay->setSpacing(0);

    QHBoxLayout* header = new QHBoxLayout();

    QLabel* brand = new QLabel("PktSwerve");
    brand->setObjectName("brandLabel");
    brand->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    QPushButton* settingsBtn = new QPushButton("⚙");
    settingsBtn->setObjectName("settingsBtn");
    settingsBtn->setFixedSize(28, 28);
    settingsBtn->setCursor(Qt::PointingHandCursor);

    header->addWidget(brand, 0, Qt::AlignVCenter);
    header->addStretch();
    header->addWidget(settingsBtn, 0, Qt::AlignVCenter);

    lay->addLayout(header);
    lay->addSpacing(16);

    m_logoLabel = new QLabel();
    m_logoLabel->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    m_logoLabel->setFixedHeight(110);

    lay->addWidget(m_logoLabel, 0, Qt::AlignHCenter);
    lay->addSpacing(20);

    m_toggleBtn = new ToggleSwitch(this);
    connect(m_toggleBtn, &ToggleSwitch::clicked, this, &PopupWindow::onToggleClicked);
    lay->addWidget(m_toggleBtn, 0, Qt::AlignHCenter);
    lay->addSpacing(12);

    m_statusLabel = new QLabel("Disconnected");
    m_statusLabel->setObjectName("statusLabel");
    m_statusLabel->setProperty("active", false);
    m_statusLabel->setAlignment(Qt::AlignHCenter);
    lay->addWidget(m_statusLabel);
    lay->addSpacing(24);

    m_dnsLabel = new QLabel();
    m_dnsLabel->setObjectName("dnsLabel");
    m_dnsLabel->setAlignment(Qt::AlignHCenter);
    lay->addWidget(m_dnsLabel);
    lay->addSpacing(20);

    QPushButton* quitBtn = new QPushButton("✕  Quit");
    quitBtn->setObjectName("quitBtn");
    quitBtn->setCursor(Qt::PointingHandCursor);
    lay->addWidget(quitBtn);
    lay->addSpacing(14);

    QLabel* dev = new QLabel("<a href=\"https://luridlane.com\" style=\"text-decoration: none; color: #888;\">by Lurid Lane</a>");
    dev->setObjectName("devLabel");
    dev->setAlignment(Qt::AlignHCenter);
    dev->setOpenExternalLinks(true);
    lay->addWidget(dev);

    connect(settingsBtn, &QPushButton::clicked, this, &PopupWindow::onSettingsClicked);
    connect(quitBtn,     &QPushButton::clicked, this, &PopupWindow::onQuitClicked);
}

void PopupWindow::updateState()
{
    if (!m_engine) {
        LOG_ERROR("PopupWindow: m_engine is null in updateState()");
        return;
    }

    bool running = m_engine->isRunning();
    m_toggleBtn->setActive(running);

    if (!isVisible()) {
        applyVisualUpdates(running);
        return;
    }

    if (m_isAnimating) {
        m_hasPendingState = true;
        m_pendingState = running;
        return;
    }

    m_isAnimating = true;

    QList<QWidget*> widgetsToAnimate = { m_logoLabel, m_statusLabel, m_dnsLabel };

    auto* fadeOutGroup = new QParallelAnimationGroup(this);
    auto* fadeInGroup = new QParallelAnimationGroup(this);

    int delayMs = 0;
    const int staggerStepMs = 40;

    for (QWidget* widget : widgetsToAnimate) {
        if (!widget) continue;

        auto* effect = new QGraphicsOpacityEffect(widget);
        widget->setGraphicsEffect(effect);

        auto* fadeOut = new QPropertyAnimation(effect, "opacity");
        fadeOut->setDuration(120);
        fadeOut->setStartValue(1.0);
        fadeOut->setEndValue(0.0);
        fadeOut->setEasingCurve(QEasingCurve::InSine);
        fadeOutGroup->addAnimation(fadeOut);

        auto* seq = new QSequentialAnimationGroup(fadeInGroup);
        if (delayMs > 0) {
            seq->addPause(delayMs);
        }

        auto* fadeIn = new QPropertyAnimation(effect, "opacity");
        fadeIn->setDuration(200);
        fadeIn->setStartValue(0.0);
        fadeIn->setEndValue(1.0);
        fadeIn->setEasingCurve(QEasingCurve::OutBack);
        seq->addAnimation(fadeIn);

        fadeInGroup->addAnimation(seq);

        delayMs += staggerStepMs;
    }

    connect(fadeOutGroup, &QParallelAnimationGroup::finished, this, [this, running, fadeInGroup]() {
        applyVisualUpdates(running);
        fadeInGroup->start(QAbstractAnimation::DeleteWhenStopped);
    });

    connect(fadeInGroup, &QParallelAnimationGroup::finished, this, [this, widgetsToAnimate]() {
        for (QWidget* widget : widgetsToAnimate) {
            if (!widget) continue;
            widget->setGraphicsEffect(nullptr);
        }

        m_isAnimating = false;

        if (m_hasPendingState) {
            m_hasPendingState = false;
            if (m_engine && m_engine->isRunning() == m_pendingState) {
                updateState();
            }
        }
    });

    fadeOutGroup->start(QAbstractAnimation::DeleteWhenStopped);
}

void PopupWindow::applyVisualUpdates(bool running)
{
    if (m_statusLabel) {
        m_statusLabel->setProperty("active", running);
        m_statusLabel->style()->unpolish(m_statusLabel);
        m_statusLabel->style()->polish(m_statusLabel);
        m_statusLabel->setText(running ? "Connected" : "Disconnected");
    }

    if (m_dnsLabel) {
        m_dnsLabel->setProperty("active", running);
        m_dnsLabel->style()->unpolish(m_dnsLabel);
        m_dnsLabel->style()->polish(m_dnsLabel);

        const auto& cfg = m_engine->config();
        m_dnsLabel->setText(cfg.dnsRedirect ? QString("%1").arg(cfg.dnsServer) : "DNS redirect disabled");
    }

    if (m_logoLabel) {
        QString resPath = running ? ":/icons/active.png" : ":/icons/inactive.png";
        QPixmap logoPixmap(resPath);

        if (logoPixmap.isNull()) {
            LOG_ERROR("Resource loading failed! Path: %1", resPath);
        } else {
            m_logoLabel->setPixmap(logoPixmap.scaled(100, 100, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
    }
}


void PopupWindow::onToggleClicked()
{
    if (m_engine->isRunning()) {
        LOG_INFO0("PopupWindow: toggle clicked - stopping engine");
        m_engine->stop();
    } else {
        LOG_INFO0("PopupWindow: toggle clicked - starting engine");
        m_engine->start();
    }
}

void PopupWindow::onEngineStarted()
{
    LOG_INFO0("PopupWindow: engine started signal received");
    updateState();
}

void PopupWindow::onEngineStopped()
{
    LOG_INFO0("PopupWindow: engine stopped signal received");
    updateState();
}

void PopupWindow::onEngineError(const QString& msg)
{
    LOG_ERROR("PopupWindow: engine error received: %1", msg);
    m_statusLabel->setText("Error");
    m_statusLabel->setStyleSheet("font-size:12px; color:#e05555;");
}

void PopupWindow::onStatsUpdated(quint64 packets, quint64 bytes)
{
    Q_UNUSED(packets)
    Q_UNUSED(bytes)
}

void PopupWindow::onSettingsClicked()
{
    LOG_INFO0("PopupWindow: settings clicked");
    hide();
    emit settingsRequested();
}

void PopupWindow::onQuitClicked()
{
    LOG_INFO0("PopupWindow: quit clicked");
    hide();
    if (m_engine && m_engine->isRunning()) {
        m_engine->blockSignals(true);
        m_engine->stop();
    }

    ::TerminateProcess(::GetCurrentProcess(), 0);
}

