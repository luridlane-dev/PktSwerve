#pragma once
#include <QPixmap>
#include <QWidget>

class BypassEngine;
class QGraphicsOpacityEffect;
class QLabel;
class QPropertyAnimation;
class ToggleSwitch;

class PopupWindow : public QWidget {
    Q_OBJECT
public:
    explicit PopupWindow(BypassEngine* engine, QWidget* parent = nullptr);
    void showAtTray(const QRect& trayIconGeo);

signals:
    void settingsRequested();

protected:
    void paintEvent(QPaintEvent*) override;
    bool eventFilter(QObject*, QEvent*) override;

private:
    void buildUi();
    void updateState();
    void applyState();
    void setStatus(const QString& state, const QString& text);
    void onToggleClicked();
    void onEngineError(const QString& message);
    void onQuitClicked();

    BypassEngine*           m_engine;
    ToggleSwitch*           m_toggle      = nullptr;
    QWidget*                m_hero        = nullptr;
    QLabel*                 m_logoLabel   = nullptr;
    QLabel*                 m_statusLabel = nullptr;
    QLabel*                 m_dnsValue    = nullptr;
    QLabel*                 m_modeValue   = nullptr;
    QGraphicsOpacityEffect* m_heroFade    = nullptr;
    QPropertyAnimation*     m_fade        = nullptr;
    QPixmap                 m_activeLogo;
    QPixmap                 m_idleLogo;
};
