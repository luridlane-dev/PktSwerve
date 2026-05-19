#pragma once
#include <QWidget>
#include <QPropertyAnimation>
#include <QPainter>

class ToggleSwitch : public QWidget {
    Q_OBJECT
    Q_PROPERTY(float offset READ offset WRITE setOffset)

public:
    explicit ToggleSwitch(QWidget* parent = nullptr)
        : QWidget(parent), m_offset(m_offPos)
    {
        setFixedSize(72, 40);
        setCursor(Qt::PointingHandCursor);

        m_anim = new QPropertyAnimation(this, "offset", this);
        m_anim->setDuration(180);
        m_anim->setEasingCurve(QEasingCurve::InOutQuad);
    }

    void setActive(bool active) {
        if (m_active == active) return;
        m_active = active;
        m_anim->stop();
        m_anim->setStartValue(m_offset);
        m_anim->setEndValue(active ? m_onPos : m_offPos);
        m_anim->start();
    }

    bool isActive() const { return m_active; }

    float offset() const { return m_offset; }
    void setOffset(float v) { m_offset = v; update(); }

signals:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent*) override { emit clicked(); }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        // Track
        QColor trackColor = m_active ? QColor(0x3a, 0x7a, 0x52) : QColor(0x2e, 0x2e, 0x2e);
        p.setBrush(trackColor);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(0, 0, 72, 40, 20, 20);

        // Thumb
        p.setBrush(Qt::white);
        p.drawEllipse(QRectF(m_offset, 4, 32, 32));
    }

private:
    bool  m_active  = false;
    float m_offset  = 4.0f;
    static constexpr float m_offPos = 4.0f;
    static constexpr float m_onPos  = 36.0f;
    QPropertyAnimation* m_anim;
};

