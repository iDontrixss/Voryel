#include "VoryelDialog.h"
#include "SolidPanel.h"
#include "Style.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>

VoryelDialog::VoryelDialog(const QString &title, QWidget *parent)
    : QDialog(parent) {
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAutoFillBackground(false);
    QPalette transparentPalette = palette();
    transparentPalette.setColor(QPalette::Window, Qt::transparent);
    setPalette(transparentPalette);
    setModal(true);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(5, 5, 9, 9);
    outer->setSpacing(0);

    auto *card = new SolidPanel(this);
    card->setFillColor(QColor(Style::WHITE));
    card->setCornerRadius(14);
    card->setFullBorder(QColor(Style::INK), 2);
    card->setHardShadow(QColor(Style::INK), 4, 4);
    auto *cardLayout = new QVBoxLayout(card);
    // Keep every child inside the painted border and reserve the shadow area.
    // Painting children from (0,0) covered the rounded outline and made the
    // top-level window look square even with a rounded SolidPanel.
    cardLayout->setContentsMargins(2, 2, 6, 6);
    cardLayout->setSpacing(0);

    m_titleBar = new QWidget(card);
    m_titleBar->setAttribute(Qt::WA_StyledBackground, true);
    m_titleBar->setFixedHeight(42);
    m_titleBar->setStyleSheet(QString(
        "background: %1; border-bottom: 2px solid %2;"
        "border-top-left-radius: 12px; border-top-right-radius: 12px;"
    ).arg(Style::WHITE, Style::INK));
    auto *titleLayout = new QHBoxLayout(m_titleBar);
    titleLayout->setContentsMargins(14, 0, 12, 0);
    titleLayout->setSpacing(7);
    auto *brand = new QLabel("Voryel");
    brand->setStyleSheet(QString("font-size: 12px; font-weight: 900; color: %1; border: none; background: transparent;").arg(Style::INK));
    titleLayout->addWidget(brand);
    auto *slash = new QLabel("/");
    slash->setStyleSheet("color: #a79fb8; border: none; background: transparent;");
    titleLayout->addWidget(slash);
    m_titleLabel = new QLabel(title);
    m_titleLabel->setStyleSheet(QString("font-size: 12px; font-weight: 750; color: %1; border: none; background: transparent;").arg(Style::TEXT_MUTED));
    titleLayout->addWidget(m_titleLabel);
    titleLayout->addStretch();

    const auto makeDot = [](const QString &color, const QString &tip) {
        auto *button = new QPushButton();
        button->setToolTip(tip);
        button->setFixedSize(14, 14);
        button->setCursor(Qt::PointingHandCursor);
        button->setStyleSheet(QString(
            "QPushButton { background: %1; border: none; border-radius: 7px; padding: 0; }"
            "QPushButton:hover { border: 1px solid #1a1a1a; }"
        ).arg(color));
        return button;
    };
    auto *close = makeDot("#ff5f57", "Cerrar");
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    titleLayout->addWidget(close);
    cardLayout->addWidget(m_titleBar);

    auto *body = new QWidget(card);
    body->setAttribute(Qt::WA_StyledBackground, true);
    body->setStyleSheet(
        "background: #f1e7ff; border: none;"
        "border-bottom-left-radius: 12px; border-bottom-right-radius: 12px;"
    );
    m_bodyLayout = new QVBoxLayout(body);
    m_bodyLayout->setContentsMargins(20, 18, 20, 20);
    m_bodyLayout->setSpacing(11);
    cardLayout->addWidget(body);
    outer->addWidget(card);
}

void VoryelDialog::setDialogTitle(const QString &title) {
    if (m_titleLabel) m_titleLabel->setText(title);
}

void VoryelDialog::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && m_titleBar
        && m_titleBar->geometry().contains(event->position().toPoint())) {
        m_dragOffset = event->globalPosition().toPoint() - frameGeometry().topLeft();
        m_dragging = true;
        event->accept();
        return;
    }
    QDialog::mousePressEvent(event);
}

void VoryelDialog::mouseMoveEvent(QMouseEvent *event) {
    if (m_dragging && (event->buttons() & Qt::LeftButton) && !isMaximized()) {
        move(event->globalPosition().toPoint() - m_dragOffset);
        event->accept();
        return;
    }
    QDialog::mouseMoveEvent(event);
}

void VoryelDialog::mouseReleaseEvent(QMouseEvent *event) {
    m_dragging = false;
    QDialog::mouseReleaseEvent(event);
}

void VoryelDialog::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    // The top-level dialog must stay fully transparent. Otherwise Qt/the
    // platform paints its rectangular palette behind the rounded SolidPanel,
    // leaving white square corners outside the black outline.
    QPainter painter(this);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(rect(), Qt::transparent);
}
