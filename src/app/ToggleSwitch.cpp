#include "ToggleSwitch.h"
#include "Theme.h"

#include <QMouseEvent>
#include <QPainter>
#include <QVariantAnimation>

namespace {

constexpr QSize kBoxSwitchSize{51, 31};

QColor mix(const QColor& a, const QColor& b, qreal t)
{
    return QColor::fromRgbF(a.redF()   + (b.redF()   - a.redF())   * t,
                            a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF()  + (b.blueF()  - a.blueF())  * t);
}

}

SwitchPainter::SwitchPainter(QWidget* owner)
    : QObject(owner), m_owner(owner), m_anim(new QVariantAnimation(this))
{
    m_anim->setDuration(220);
    m_anim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        m_progress = v.toReal();
        m_owner->update();
    });
}

void SwitchPainter::setOn(bool on, bool animate)
{
    const qreal target = on ? 1.0 : 0.0;
    m_anim->stop();
    if (!animate) {
        m_progress = target;
        m_owner->update();
        return;
    }
    m_anim->setStartValue(m_progress);
    m_anim->setEndValue(target);
    m_anim->start();
}

void SwitchPainter::paint(QPainter& p, const QRectF& r, bool enabled) const
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setOpacity(enabled ? 1.0 : 0.4);

    p.setBrush(mix(Theme::switchOff, Theme::green, m_progress));
    p.drawRoundedRect(r, r.height() / 2, r.height() / 2);

    const qreal pad = 2;
    const qreal d   = r.height() - 2 * pad;
    const qreal x   = r.left() + pad + (r.width() - d - 2 * pad) * m_progress;
    const QRectF thumb(x, r.top() + pad, d, d);

    p.setBrush(QColor(0, 0, 0, 60));
    p.drawEllipse(thumb.translated(0, 1.5));
    p.setBrush(Qt::white);
    p.drawEllipse(thumb);
    p.restore();
}


ToggleSwitch::ToggleSwitch(QWidget* parent)
    : QWidget(parent), m_switch(new SwitchPainter(this))
{
    setFixedSize(64, 39);
    setCursor(Qt::PointingHandCursor);
}

void ToggleSwitch::setActive(bool active)
{
    if (m_active == active) return;
    m_active = active;
    m_switch->setOn(active, isVisible());
}

void ToggleSwitch::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && rect().contains(e->position().toPoint()))
        emit clicked();
}

void ToggleSwitch::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    m_switch->paint(p, rect(), isEnabled());
}


SwitchBox::SwitchBox(const QString& text, QWidget* parent)
    : QCheckBox(text, parent), m_switch(new SwitchPainter(this))
{
    setCursor(Qt::PointingHandCursor);
    connect(this, &QCheckBox::toggled, this, [this](bool on) { m_switch->setOn(on, isVisible()); });
}

QSize SwitchBox::sizeHint() const
{
    const int textWidth = fontMetrics().horizontalAdvance(text());
    return {textWidth + 16 + kBoxSwitchSize.width(), kBoxSwitchSize.height() + 6};
}

void SwitchBox::paintEvent(QPaintEvent*)
{
    QPainter p(this);

    const QRectF box(QPointF(width() - kBoxSwitchSize.width(),
                             (height() - kBoxSwitchSize.height()) / 2.0),
                     QSizeF(kBoxSwitchSize));
    m_switch->paint(p, box, isEnabled());

    p.setPen(isEnabled() ? Theme::text : Theme::hint);
    p.drawText(QRectF(0, 0, box.left() - 12, height()), Qt::AlignVCenter | Qt::AlignLeft, text());
}
