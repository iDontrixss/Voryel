#include "SolidPanel.h"
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QEvent>
#include <QEnterEvent>
#include <QPropertyAnimation>
#include <QEasingCurve>

SolidPanel::SolidPanel(QWidget *parent) : QFrame(parent) {
    setFrameShape(QFrame::NoFrame);
    setAttribute(Qt::WA_StyledBackground, false);
    setAttribute(Qt::WA_Hover, true);
}

void SolidPanel::setFillColor(const QColor &color) {
    m_fill = color;
    update();
}

void SolidPanel::setTopBorder(const QColor &color, int width) {
    m_topBorderColor = color;
    m_topBorderWidth = width;
    update();
}

void SolidPanel::setBottomBorder(const QColor &color, int width) {
    m_bottomBorderColor = color;
    m_bottomBorderWidth = width;
    update();
}

void SolidPanel::setFullBorder(const QColor &color, int width) {
    m_fullBorderColor = color;
    m_fullBorderWidth = width;
    update();
}

void SolidPanel::setCornerRadius(int radius) {
    m_radius = radius;
    update();
}

void SolidPanel::setHardShadow(const QColor &color, int dx, int dy) {
    m_shadowColor = color;
    m_shadowDx = dx;
    m_shadowDy = dy;
    m_shadowDxBase = dx;
    m_shadowDyBase = dy;
    update();
}

void SolidPanel::setHoverEffect(bool enabled) {
    m_hoverEffect = enabled;
}

void SolidPanel::setHoverProgress(qreal progress) {
    m_hoverProgress = progress;
    const int extraDx = qRound(progress * 2);
    const int extraDy = qRound(progress * 2);
    m_shadowDx = m_shadowDxBase + extraDx;
    m_shadowDy = m_shadowDyBase + extraDy;
    update();
}

void SolidPanel::enterEvent(QEnterEvent *event) {
    if (m_hoverEffect) {
        delete m_hoverAnim;
        m_hoverAnim = new QPropertyAnimation(this, "hoverProgress", this);
        m_hoverAnim->setDuration(180);
        m_hoverAnim->setStartValue(m_hoverProgress);
        m_hoverAnim->setEndValue(1.0);
        m_hoverAnim->setEasingCurve(QEasingCurve::OutCubic);
        m_hoverAnim->start();
    }
    QFrame::enterEvent(event);
}

void SolidPanel::leaveEvent(QEvent *event) {
    if (m_hoverEffect) {
        delete m_hoverAnim;
        m_hoverAnim = new QPropertyAnimation(this, "hoverProgress", this);
        m_hoverAnim->setDuration(200);
        m_hoverAnim->setStartValue(m_hoverProgress);
        m_hoverAnim->setEndValue(0.0);
        m_hoverAnim->setEasingCurve(QEasingCurve::OutCubic);
        m_hoverAnim->start();
    }
    QFrame::leaveEvent(event);
}

void SolidPanel::paintEvent(QPaintEvent *) {
    QPainter painter(this);

    QRect bodyRect = rect();
    if (m_shadowDx > 0 || m_shadowDy > 0) {
        bodyRect.adjust(0, 0, -m_shadowDx, -m_shadowDy);
    }

    if (m_shadowColor.alpha() > 0 && (m_shadowDx > 0 || m_shadowDy > 0)) {
        painter.setRenderHint(QPainter::Antialiasing, true);
        QRect shadowRect = bodyRect.translated(m_shadowDx, m_shadowDy);
        if (m_radius > 0) {
            QPainterPath shadowPath;
            shadowPath.addRoundedRect(shadowRect, m_radius, m_radius);
            painter.fillPath(shadowPath, m_shadowColor);
        } else {
            painter.fillRect(shadowRect, m_shadowColor);
        }
    }

    painter.setRenderHint(QPainter::Antialiasing, true);
    if (m_fullBorderWidth > 0) {
        QPainterPath outerPath;
        outerPath.addRoundedRect(QRectF(bodyRect), m_radius, m_radius);
        painter.fillPath(outerPath, m_fullBorderColor);

        const qreal inset = static_cast<qreal>(m_fullBorderWidth);
        QRectF innerRect = QRectF(bodyRect).adjusted(inset, inset, -inset, -inset);
        if (innerRect.isValid()) {
            QPainterPath innerPath;
            qreal innerRadius = qMax(0.0, static_cast<qreal>(m_radius) - inset);
            innerPath.addRoundedRect(innerRect, innerRadius, innerRadius);
            painter.fillPath(innerPath, m_fill);
        }
    } else {
        if (m_fill.alpha() > 0) {
            QPainterPath path;
            path.addRoundedRect(QRectF(bodyRect), m_radius, m_radius);
            painter.fillPath(path, m_fill);
        }
    }

    if (m_topBorderWidth > 0) {
        painter.fillRect(QRect(bodyRect.left(), bodyRect.top(), bodyRect.width(), m_topBorderWidth), m_topBorderColor);
    }
    if (m_bottomBorderWidth > 0) {
        painter.fillRect(QRect(bodyRect.left(), bodyRect.bottom() - m_bottomBorderWidth + 1, bodyRect.width(), m_bottomBorderWidth), m_bottomBorderColor);
    }
}
