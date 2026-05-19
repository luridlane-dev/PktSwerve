// PopupWindow.h

#pragma once
#include <QWidget>
#include <QLabel>

#include <QPainter>
#include <QPropertyAnimation>
#include <QMouseEvent>
#include "ToggleSwitch.h"

class BypassEngine;

class ToggleSwitch;
class QLabel;

class PopupWindow : public QWidget {
    Q_OBJECT
public:
    explicit PopupWindow(BypassEngine* engine, QWidget* parent = nullptr);
    void showAtTray(const QRect& trayIconGeo);

signals:
    void settingsRequested();

protected:
    void paintEvent(QPaintEvent*) override;
    void focusOutEvent(QFocusEvent*) override;
    bool eventFilter(QObject*, QEvent*) override;

private slots:
    void onToggleClicked();
    void onEngineStarted();
    void onEngineStopped();
    void onEngineError(const QString&);
    void onStatsUpdated(quint64, quint64);
    void onSettingsClicked();
    void onQuitClicked();

private:
    void buildUi();
    void updateState();

    BypassEngine*  m_engine;
    ToggleSwitch*  m_toggleBtn   = nullptr;
    QLabel*        m_statusLabel = nullptr;
    QLabel*        m_dnsLabel    = nullptr;
    QLabel*        m_logoLabel   = nullptr;
    int            m_elapsedSec  = 0;

    void applyVisualUpdates(bool running);

    bool m_isAnimating = false;
    bool m_hasPendingState = false;
    bool m_pendingState = false;
};