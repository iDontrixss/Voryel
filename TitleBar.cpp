#include "TitleBar.h"
#include "SolidPanel.h"
#include "Style.h"
#include "IconUtil.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QIcon>
#include <QPainter>
#include <QEvent>
#include <QMouseEvent>
#include <QFontMetrics>
#include <QSizePolicy>

TitleBar::TitleBar(QWidget *parent) : QWidget(parent) {
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedHeight(Style::TOPBAR_HEIGHT);
    setStyleSheet(QString(
        "background: %1; border-bottom: 2px solid %2;"
    ).arg(Style::WHITE, Style::INK));

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(16, 0, 10, 0);
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

    // Neo-brutalist card for active model — identical style to dashboard cards
    m_modelBadge = new SolidPanel();
    m_modelBadge->setFillColor(QColor(Style::WHITE));
    m_modelBadge->setFullBorder(QColor(Style::INK), 2);
    m_modelBadge->setCornerRadius(10);
    m_modelBadge->setHardShadow(QColor(Style::INK), 2, 2);
    m_modelBadge->setHoverEffect(true);
    m_modelBadge->setCursor(Qt::PointingHandCursor);
    m_modelBadge->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    m_modelBadge->setFixedHeight(32);
    m_modelBadge->installEventFilter(this);

    auto *badgeLayout = new QHBoxLayout(m_modelBadge);
    badgeLayout->setContentsMargins(6, 2, 10, 2);
    badgeLayout->setSpacing(6);

    auto *modelIcon = makeModelIcon("Claude 3.5 Sonnet");
    badgeLayout->addWidget(modelIcon);

    m_modelLabel = new QLabel("Claude 3.5 Sonnet");
    m_modelLabel->setMinimumWidth(60);
    m_modelLabel->setMaximumWidth(150);
    m_modelLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    m_modelLabel->setStyleSheet(QString("font-size: 11px; font-weight: 800; color: %1; border: none; background: transparent;").arg(Style::INK));
    badgeLayout->addWidget(m_modelLabel);

    layout->addWidget(m_modelBadge);

    layout->addSpacing(12);

    auto makeDot = [](const QString &color, const QString &hoverColor) -> QPushButton* {
        auto *btn = new QPushButton();
        btn->setFixedSize(14, 14);
        btn->setCursor(Qt::ArrowCursor);
        btn->setStyleSheet(QString(
            "QPushButton { background: %1; border: none; border-radius: 7px; padding: 0; }"
            "QPushButton:hover { background: %2; }"
        ).arg(color, hoverColor));
        return btn;
    };

    m_minBtn  = makeDot("#ffbd2e", "#d4a020");
    m_maxBtn  = makeDot("#28c840", "#20aa36");
    m_closeBtn = makeDot("#ff5f57", "#d94d46");

    connect(m_minBtn, &QPushButton::clicked, this, [this]() {
        if (auto *w = window()) w->showMinimized();
    });
    connect(m_maxBtn, &QPushButton::clicked, this, [this]() {
        if (auto *w = window()) {
            if (w->isMaximized()) {
                w->showNormal();
            } else {
                w->showMaximized();
            }
            syncMaximizeButton();
        }
    });
    connect(m_closeBtn, &QPushButton::clicked, this, [this]() {
        if (auto *w = window()) w->close();
    });

    layout->addWidget(m_minBtn);
    layout->addSpacing(6);
    layout->addWidget(m_maxBtn);
    layout->addSpacing(6);
    layout->addWidget(m_closeBtn);
}

void TitleBar::setProjectName(const QString &name) {
    const bool has = !name.isEmpty();
    m_breadcrumbSlash->setVisible(has);
    m_projectLabel->setVisible(has);
    m_projectLabel->setText(name);
}

void TitleBar::setActiveModel(const QString &modelName) {
    const QString full = modelName.isEmpty() ? "Sin modelo" : modelName;
    m_modelLabel->setToolTip(full);
    m_modelLabel->setText(QFontMetrics(m_modelLabel->font()).elidedText(full, Qt::ElideRight, 145));
    // Rebuild icon
    auto *layout = qobject_cast<QHBoxLayout*>(m_modelBadge->layout());
    if (!layout) return;
    auto *oldIcon = layout->itemAt(0) ? qobject_cast<QLabel*>(layout->itemAt(0)->widget()) : nullptr;
    if (oldIcon) { layout->removeWidget(oldIcon); oldIcon->deleteLater(); }
    layout->insertWidget(0, makeModelIcon(full));
}

void TitleBar::syncMaximizeButton() {
    // no text to toggle; green dot stays as-is
}

bool TitleBar::isDragRegion(const QPoint &localPos) const {
    for (auto *btn : {m_minBtn, m_maxBtn, m_closeBtn}) {
        if (btn->geometry().contains(localPos)) return false;
    }
    if (m_modelBadge->geometry().contains(localPos)) return false;
    return true;
}

void TitleBar::mouseDoubleClickEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        if (auto *w = window()) {
            if (w->isMaximized()) {
                w->showNormal();
            } else {
                w->showMaximized();
            }
            syncMaximizeButton();
        }
    }
    QWidget::mouseDoubleClickEvent(event);
}

bool TitleBar::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_modelBadge && event->type() == QEvent::MouseButtonRelease) {
        auto *mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton) {
            const QPoint anchor = m_modelBadge->mapToGlobal(QPoint(m_modelBadge->width(), m_modelBadge->height()));
            emit modelBadgeClicked(anchor);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

static QString modelSvgPath(const QString &modelName) {
    const QString lower = modelName.toLower();
    if (lower.contains("claude")) return ":/icons/icons/claude.svg";
    if (lower.contains("gpt") || lower.contains("openai")) return ":/icons/icons/openai.svg";
    if (lower.contains("gemini")) return ":/icons/icons/gemini.svg";
    if (lower.contains("groq") || lower.contains("llama")) return ":/icons/icons/groq.svg";
    if (lower.contains("ollama") || lower.contains("qwen")) return ":/icons/icons/ollama.svg";
    if (lower.contains("deepseek")) return ":/icons/icons/deepseek.svg";
    return {};
}

QLabel* TitleBar::makeModelIcon(const QString &modelName) {
    const int size = 18;
    auto *label = new QLabel();
    label->setFixedSize(size, size);
    QString svgPath = modelSvgPath(modelName);
    if (svgPath.isEmpty()) {
        label->setStyleSheet(QString("background: %1; border: 2px solid %2; border-radius: 9px;").arg(Style::VIOLET, Style::INK));
    } else {
        label->setPixmap(QIcon(svgPath).pixmap(size, size));
    }
    return label;
}
