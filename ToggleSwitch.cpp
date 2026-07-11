#include "ToggleSwitch.h"
#include "Style.h"
#include <QPainter>
#include <QMouseEvent>
#include <QGraphicsDropShadowEffect>

ToggleSwitch::ToggleSwitch(QWidget *parent) : QAbstractButton(parent) {
    setFixedSize(44, 24);
    setCursor(Qt::PointingHandCursor);
    // Neo-brutalist hard shadow
    auto *shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(0);
    shadow->setOffset(2, 2);
    shadow->setColor(QColor(Style::INK));
    setGraphicsEffect(shadow);
}

void ToggleSwitch::setOn(bool on, bool animated) {
    if (on == m_on) return;
    m_on = on;
    if (animated) {
        if (m_anim) m_anim->stop();
        m_anim = new QPropertyAnimation(this, "knobPos", this);
        m_anim->setDuration(150);
        m_anim->setStartValue(m_knobPos);
        m_anim->setEndValue(m_on ? 1.0 : 0.0);
        m_anim->setEasingCurve(QEasingCurve::OutCubic);
        connect(m_anim, &QPropertyAnimation::finished, this, [this]() {
            m_anim->deleteLater();
            m_anim = nullptr;
        });
        m_anim->start();
    } else {
        m_knobPos = m_on ? 1.0 : 0.0;
    }
    update();
    emit toggled(m_on);
}

void ToggleSwitch::setKnobPos(qreal pos) {
    m_knobPos = pos;
    update();
}

void ToggleSwitch::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const int trackW = 44, trackH = 24;
    const int knobSize = 18;
    const int margin = (trackH - knobSize) / 2;

    // Track pill
    QColor trackColor = m_on ? QColor(Style::VIOLET) : QColor(Style::WHITE);
    p.setBrush(trackColor);
    p.setPen(QPen(QColor(Style::INK), 2));
    p.drawRoundedRect(1, 1, trackW - 2, trackH - 2, trackH / 2, trackH / 2);

    // Knob
    qreal knobX = margin + m_knobPos * (trackW - margin * 2 - knobSize);
    p.setBrush(QColor(Style::WHITE));
    p.setPen(QPen(QColor(Style::INK), 2));
    p.drawEllipse(QRectF(knobX, margin, knobSize, knobSize));
}

void ToggleSwitch::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && rect().contains(event->pos())) {
        setOn(!m_on);
    }
    QAbstractButton::mouseReleaseEvent(event);
}
