#pragma once
#include <QCheckBox>
#include <QWidget>

class QPainter;
class QVariantAnimation;

// Animated switch drawing shared by the widgets below.
class SwitchPainter : public QObject {
public:
    explicit SwitchPainter(QWidget* owner);

    void setOn(bool on, bool animate);
    void paint(QPainter& p, const QRectF& rect, bool enabled) const;

private:
    QWidget*           m_owner;
    QVariantAnimation* m_anim;
    qreal              m_progress = 0;
};

// Large switch that only reports clicks; the owner decides the state.
class ToggleSwitch : public QWidget {
    Q_OBJECT
public:
    explicit ToggleSwitch(QWidget* parent = nullptr);

    void setActive(bool active);

signals:
    void clicked();

protected:
    void mouseReleaseEvent(QMouseEvent* e) override;
    void paintEvent(QPaintEvent*) override;

private:
    SwitchPainter* m_switch;
    bool           m_active = false;
};

// Checkbox drawn as a label with a switch on the right.
class SwitchBox : public QCheckBox {
    Q_OBJECT
public:
    explicit SwitchBox(const QString& text, QWidget* parent = nullptr);

    QSize sizeHint() const override;

protected:
    bool hitButton(const QPoint& pos) const override { return rect().contains(pos); }
    void paintEvent(QPaintEvent*) override;

private:
    SwitchPainter* m_switch;
};
