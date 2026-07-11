#include "Sidebar.h"
#include "Style.h"
#include "IconUtil.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QResizeEvent>
#include <QGraphicsDropShadowEffect>
#include <QPropertyAnimation>
#include <QEasingCurve>

Sidebar::Sidebar(QWidget *parent) : QWidget(parent) {
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedWidth(Style::SIDEBAR_WIDTH);
    setStyleSheet(QString(
        "background: %1; border-right: 2px solid %2;"
    ).arg(Style::SIDEBAR_BG, Style::INK));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 12, 8, 12);
    layout->setSpacing(6);

    // Logo (bot) arriba, siempre violeta, no es un botón de navegación
    auto *logo = new QToolButton(this);
    logo->setFixedSize(32, 32);
    logo->setIcon(IconUtil::coloredIcon(":/icons/icons/bot.svg", QColor(Style::WHITE), QSize(15, 15)));
    logo->setIconSize(QSize(15, 15));
    logo->setEnabled(false);
    logo->setStyleSheet(QString(
        "QToolButton {"
        "  background: %1; border: 2px solid %2; border-radius: 8px;"
        "}"
    ).arg(Style::VIOLET, Style::INK));
    auto *logoShadow = new QGraphicsDropShadowEffect(logo);
    logoShadow->setBlurRadius(0);
    logoShadow->setOffset(2, 2);
    logoShadow->setColor(QColor(Style::INK));
    logo->setGraphicsEffect(logoShadow);

    layout->addWidget(logo, 0, Qt::AlignHCenter);
    layout->addSpacing(8);

    // ── Navegación principal: Home, Proyecto, Chat ──
    struct Def { View view; QString icon; QString tip; };
    const QVector<Def> defs = {
        { View::Dashboard, ":/icons/icons/home.svg",    "Inicio" },
        { View::Project,   ":/icons/icons/folder.svg",  "Proyecto" },
        { View::Chat,      ":/icons/icons/message.svg", "Chat" },
    };

    for (const auto &def : defs) {
        QToolButton *btn = makeNavButton(def.icon, def.tip);
        layout->addWidget(btn);
        m_navItems.push_back({ def.view, btn });
        if (def.view == View::Chat) {
            m_chatBtn = btn;
            m_chatBadge = new QLabel(this);
            m_chatBadge->setFixedSize(18, 18);
            m_chatBadge->setAlignment(Qt::AlignCenter);
            m_chatBadge->setAttribute(Qt::WA_TransparentForMouseEvents, true);
            m_chatBadge->hide();
        }
        connect(btn, &QToolButton::clicked, this, [this, btn, view = def.view]() {
            setActiveView(view);
            emit navigate(view);
            auto *pulse = new QPropertyAnimation(btn, "iconSize", btn);
            QSize orig = btn->iconSize();
            pulse->setDuration(120);
            pulse->setKeyValueAt(0, orig);
            pulse->setKeyValueAt(0.5, QSize(20, 20));
            pulse->setKeyValueAt(1, orig);
            pulse->setEasingCurve(QEasingCurve::OutBack);
            pulse->start(QAbstractAnimation::DeleteWhenStopped);
        });
    }

    // ── Plugins ──
    auto *pluginsBtn = makeNavButton(":/icons/icons/puzzle.svg", "Pr\u00F3ximamente");
    pluginsBtn->setEnabled(false);
    applyButtonStyle(pluginsBtn, false);
    layout->addWidget(pluginsBtn);

    layout->addStretch();

    // ── Configuración (abajo, con icono de engranaje) ──
    auto *settingsBtn = makeNavButton(":/icons/icons/gear.svg", "Configuraci\u00F3n");
    layout->addWidget(settingsBtn);
    m_navItems.push_back({ View::Settings, settingsBtn });
    connect(settingsBtn, &QToolButton::clicked, this, [this, settingsBtn]() {
        setActiveView(View::Settings);
        emit navigate(View::Settings);
        auto *pulse = new QPropertyAnimation(settingsBtn, "iconSize", settingsBtn);
        QSize orig = settingsBtn->iconSize();
        pulse->setDuration(120);
        pulse->setKeyValueAt(0, orig);
        pulse->setKeyValueAt(0.5, QSize(20, 20));
        pulse->setKeyValueAt(1, orig);
        pulse->setEasingCurve(QEasingCurve::OutBack);
        pulse->start(QAbstractAnimation::DeleteWhenStopped);
    });

    applyButtonStyle(m_navItems.first().button, true); // Dashboard activo por defecto
}

QToolButton* Sidebar::makeNavButton(const QString &iconRes, const QString &tooltip) {
    auto *btn = new QToolButton(this);
    btn->setFixedSize(40, 40);
    btn->setToolTip(tooltip);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setProperty("iconRes", iconRes);
    applyButtonStyle(btn, false);
    return btn;
}

void Sidebar::applyButtonStyle(QToolButton *btn, bool active) {
    const QString iconRes = btn->property("iconRes").toString();
    if (iconRes.isEmpty()) return;

    btn->setIconSize(QSize(17, 17));

    if (active) {
        btn->setIcon(IconUtil::coloredIcon(iconRes, QColor(Style::WHITE)));
        btn->setStyleSheet(QString(
            "QToolButton {"
            "  background: %1; border: 2px solid %2; border-radius: 8px;"
            "}"
        ).arg(Style::VIOLET, Style::INK));
        auto *shadow = new QGraphicsDropShadowEffect(btn);
        shadow->setBlurRadius(0);
        shadow->setOffset(2, 2);
        shadow->setColor(QColor(Style::INK));
        btn->setGraphicsEffect(shadow);
    } else {
        btn->setIcon(IconUtil::coloredIcon(iconRes, QColor(Style::TEXT_MUTED)));
        btn->setGraphicsEffect(nullptr);
        btn->setStyleSheet(QString(
            "QToolButton {"
            "  background: transparent; border: 2px solid transparent; border-radius: 8px;"
            "}"
            "QToolButton:hover {"
            "  background: %1; border: 2px solid %2;"
            "}"
        ).arg(Style::WHITE, Style::INK));
    }
}

void Sidebar::setActiveView(View view) {
    m_active = view;
    for (auto &item : m_navItems) {
        applyButtonStyle(item.button, item.view == view);
    }
}

void Sidebar::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    if (m_chatBadge && m_chatBadge->isVisible()) repositionBadge();
}

void Sidebar::setChatBadge(const QString &symbol, const QColor &bgColor) {
    if (!m_chatBadge) return;
    m_chatBadge->setText(symbol);
    m_chatBadge->setStyleSheet(QString(
        "font-size: 11px; font-weight: 900; color: #ffffff;"
        "background: %1; border: 2px solid #000000; border-radius: 9px;"
    ).arg(bgColor.name()));
    m_chatBadge->setFixedSize(18, 18);
    repositionBadge();
    m_chatBadge->show();
}

void Sidebar::clearChatBadge() {
    if (m_chatBadge) m_chatBadge->hide();
}

void Sidebar::repositionBadge() {
    if (!m_chatBadge || !m_chatBtn) return;
    QPoint topRight = m_chatBtn->geometry().topRight();
    m_chatBadge->move(topRight.x() - 9, topRight.y() - 9);
}
