#include "TopBar.h"
#include "Style.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QEvent>
#include <QMouseEvent>
#include <QCursor>
#include <QFontMetrics>
#include <QSizePolicy>

TopBar::TopBar(QWidget *parent) : QWidget(parent) {
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedHeight(Style::TOPBAR_HEIGHT);
    setStyleSheet(QString(
        "background: %1; border-bottom: 2px solid %2;"
    ).arg(Style::WHITE, Style::INK));

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(16, 0, 18, 0);
    layout->setSpacing(8);

    auto *brand = new QLabel("Voryel");
    brand->setStyleSheet(QString("font-weight: 900; font-size: 13px; color: %1; border: none;").arg(Style::INK));
    layout->addWidget(brand);

    m_breadcrumbSlash = new QLabel("/");
    m_breadcrumbSlash->setStyleSheet("color: #ccc; border: none;");
    m_breadcrumbSlash->setVisible(false);
    layout->addWidget(m_breadcrumbSlash);

    m_projectLabel = new QLabel();
    m_projectLabel->setStyleSheet(QString("font-size: 13px; font-weight: 600; color: %1; border: none;").arg(Style::TEXT_MUTED));
    m_projectLabel->setVisible(false);
    layout->addWidget(m_projectLabel);

    layout->addStretch();

    // Badge de modelo activo: punto verde + texto, píldora con borde
    m_modelBadge = new QWidget();
    m_modelBadge->setAttribute(Qt::WA_StyledBackground, true);
    m_modelBadge->setStyleSheet(QString(
        "background: %1; border: 2px solid %2; border-radius: 12px;"
    ).arg(Style::BG_LILAC, Style::INK));
    m_modelBadge->setCursor(Qt::PointingHandCursor);
    m_modelBadge->installEventFilter(this);

    auto *badgeLayout = new QHBoxLayout(m_modelBadge);
    badgeLayout->setContentsMargins(9, 3, 9, 3);
    badgeLayout->setSpacing(6);

    auto *dot = new QLabel();
    dot->setFixedSize(7, 7);
    dot->setStyleSheet(QString("background: %1; border-radius: 3px; border: none;").arg(Style::GREEN));
    badgeLayout->addWidget(dot);

    m_modelLabel = new QLabel("Sin modelo");
    m_modelLabel->setMinimumWidth(60);
    m_modelLabel->setMaximumWidth(150);
    m_modelLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    m_modelLabel->setStyleSheet(QString("font-size: 11px; font-weight: 800; color: %1; border: none;").arg(Style::INK));
    badgeLayout->addWidget(m_modelLabel);

    layout->addWidget(m_modelBadge);
}

void TopBar::setProjectName(const QString &name) {
    const bool has = !name.isEmpty();
    m_breadcrumbSlash->setVisible(has);
    m_projectLabel->setVisible(has);
    m_projectLabel->setText(name);
}

void TopBar::setActiveModel(const QString &modelName) {
    const QString full = modelName.isEmpty() ? "Sin modelo" : modelName;
    m_modelLabel->setToolTip(full);
    m_modelLabel->setText(QFontMetrics(m_modelLabel->font()).elidedText(full, Qt::ElideRight, 145));
}


bool TopBar::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_modelBadge) {
        if (event->type() == QEvent::Enter) {
            m_modelBadge->setStyleSheet(QString(
                "background: %1; border: 2px solid %2; border-radius: 12px;"
            ).arg(Style::WHITE, Style::VIOLET));
            return false;
        }
        if (event->type() == QEvent::Leave) {
            m_modelBadge->setStyleSheet(QString(
                "background: %1; border: 2px solid %2; border-radius: 12px;"
            ).arg(Style::BG_LILAC, Style::INK));
            return false;
        }
        if (event->type() == QEvent::MouseButtonRelease) {
            auto *mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::LeftButton) {
                const QPoint anchor = m_modelBadge->mapToGlobal(QPoint(m_modelBadge->width(), m_modelBadge->height()));
                emit modelBadgeClicked(anchor);
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}
