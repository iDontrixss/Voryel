#include "DashboardView.h"
#include "Style.h"
#include "SolidPanel.h"
#include "IconUtil.h"
#include "VoryelDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QButtonGroup>
#include <QStackedWidget>
#include <QFrame>
#include <QMouseEvent>
#include <QSizePolicy>
#include <QTimer>
#include <QDialogButtonBox>
#include <QPlainTextEdit>

namespace {

QLabel* makeIconLabel(const QString &iconRes, const QColor &tint, int boxSize, int iconSize,
                       const QString &bgColor, int radius, bool border = true) {
    auto *box = new QLabel();
    box->setAttribute(Qt::WA_StyledBackground, true);
    box->setFixedSize(boxSize, boxSize);
    box->setAlignment(Qt::AlignCenter);
    box->setStyleSheet(QString(
        "background: %1; border: %2 solid %3; border-radius: %4px;"
    ).arg(bgColor, border ? "2px" : "0px", Style::INK, QString::number(radius)));
    box->setPixmap(IconUtil::coloredIcon(iconRes, tint, QSize(iconSize, iconSize)).pixmap(iconSize, iconSize));
    return box;
}

QPushButton* makeGhostButton(const QString &text) {
    auto *btn = new QPushButton(text);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setStyleSheet(QString(
        "QPushButton {"
        "  background: %1; color: %2; border: 2px solid %2; border-radius: 8px;"
        "  padding: 5px 12px; font-size: 11px; font-weight: 800;"
        "}"
        "QPushButton:hover { background: %3; color: white; border-color: %3; }"
    ).arg(Style::BG_LILAC, Style::INK, Style::VIOLET));
    return btn;
}

QLabel* sectionHeader(const QString &text) {
    auto *lbl = new QLabel(text);
    lbl->setStyleSheet(QString(
        "font-size: 11px; font-weight: 900; color: %1; letter-spacing: 0.8px; border: none;"
    ).arg(Style::TEXT_MUTED));
    return lbl;
}

} // namespace

DashboardView::DashboardView(QWidget *parent) : QWidget(parent) {
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ══════════════════════ Columna principal (scroll) ══════════════════════
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    scroll->setStyleSheet(Style::scrollAreaStyle());
    scroll->viewport()->setStyleSheet(QString("background: %1;").arg(Style::BG_LILAC));

    auto *content = new QWidget();
    content->setMaximumWidth(Style::CONTENT_MAX_WIDTH);
    content->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    content->setStyleSheet(QString("background: %1;").arg(Style::BG_LILAC));
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(Style::PAGE_MARGIN_X, Style::PAGE_MARGIN_Y, Style::PAGE_MARGIN_X, 38);
    layout->setSpacing(18);

    // ── Header: título + descripción, botón "Nueva sesión" ───────
    auto *headerRow = new QHBoxLayout();
    auto *headerCol = new QVBoxLayout();
    headerCol->setSpacing(2);
    auto *title = new QLabel("Dashboard");
    title->setStyleSheet(QString("font-size: 24px; font-weight: 900; color: %1; border: none;").arg(Style::INK));
    m_modelLine = new QLabel("Tus proyectos, sesiones y workflows rápidos.");
    m_modelLine->setStyleSheet(QString("font-size: 12px; color: %1; border: none;").arg(Style::TEXT_MUTED));
    headerCol->addWidget(title);
    headerCol->addWidget(m_modelLine);
    headerRow->addLayout(headerCol);
    headerRow->addStretch();

    auto *newSessionBtn = new QPushButton("+  Nueva sesión");
    newSessionBtn->setCursor(Qt::PointingHandCursor);
    newSessionBtn->setFixedHeight(38);
    newSessionBtn->setStyleSheet(QString(
        "QPushButton {"
        "  background: %1; color: %2; border: 2px solid %3; border-radius: 10px;"
        "  padding: 0 16px; font-size: 13px; font-weight: 800;"
        "}"
        "QPushButton:hover { background: %4; }"
    ).arg(Style::VIOLET, Style::WHITE, Style::INK, Style::VIOLET_DARK));
    connect(newSessionBtn, &QPushButton::clicked, this, [this]() {
        emit chatRequested(QString(), QString());
    });
    // sombra dura pintada a mano sobre un wrapper SolidPanel transparente
    // no es necesaria acá: el botón es rectangular, el QSS alcanza.
    headerRow->addWidget(newSessionBtn);
    layout->addLayout(headerRow);

    // ── Stats ────────────────────────────────────────────────
    auto *statsWrap = new QHBoxLayout();
    statsWrap->setSpacing(14);
    m_statsRow = statsWrap;
    layout->addLayout(statsWrap);

    // ── Workflows rápidos ────────────────────────────────────
    layout->addWidget(sectionHeader("WORKFLOWS RÁPIDOS"));
    auto *wfQuickWrap = new QHBoxLayout();
    wfQuickWrap->setSpacing(12);
    m_workflowsQuickRow = wfQuickWrap;
    layout->addLayout(wfQuickWrap);

    // ── Tabs ─────────────────────────────────────────────────
    auto *tabsCard = new SolidPanel(content);
    tabsCard->setFillColor(QColor(Style::WHITE));
    tabsCard->setCornerRadius(12);
    tabsCard->setFullBorder(QColor(Style::INK), 2);
    tabsCard->setHardShadow(QColor(Style::INK), 2, 2);
    tabsCard->setMinimumHeight(42);
    tabsCard->setMaximumHeight(46);
    auto *tabsLayout = new QHBoxLayout(tabsCard);
    tabsLayout->setContentsMargins(4, 4, 6, 6);
    tabsLayout->setSpacing(4);

    auto makeTabButton = [&](const QString &text) {
        auto *btn = new QPushButton(text);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setCheckable(true);
        btn->setFixedHeight(30);
        return btn;
    };
    m_tabProjects = makeTabButton("Proyectos");
    m_tabSessions = makeTabButton("Sesiones");
    m_tabHistory = makeTabButton("Workflows");
    tabsLayout->addWidget(m_tabProjects);
    tabsLayout->addWidget(m_tabSessions);
    tabsLayout->addWidget(m_tabHistory);
    tabsLayout->addStretch();

    connect(m_tabProjects, &QPushButton::clicked, this, [this]() { selectTab(0); });
    connect(m_tabSessions, &QPushButton::clicked, this, [this]() { selectTab(1); });
    connect(m_tabHistory, &QPushButton::clicked, this, [this]() { selectTab(2); });

    auto *tabsRow = new QHBoxLayout();
    tabsRow->addWidget(tabsCard);
    tabsRow->addStretch();
    layout->addLayout(tabsRow);

    // ── Contenido de tabs ────────────────────────────────────
    m_tabStack = new QStackedWidget();

    auto *projectsPage = new QWidget();
    m_projectsListLayout = new QVBoxLayout(projectsPage);
    m_projectsListLayout->setContentsMargins(0, 0, 0, 0);
    m_projectsListLayout->setSpacing(8);
    m_projectsListLayout->addStretch();

    auto *sessionsPage = new QWidget();
    m_sessionsListLayout = new QVBoxLayout(sessionsPage);
    m_sessionsListLayout->setContentsMargins(0, 0, 0, 0);
    m_sessionsListLayout->setSpacing(8);
    m_sessionsListLayout->addStretch();

    auto *workflowsPage = new QWidget();
    m_workflowsFullLayout = new QVBoxLayout(workflowsPage);
    m_workflowsFullLayout->setContentsMargins(0, 0, 0, 0);

    m_tabStack->addWidget(projectsPage);
    m_tabStack->addWidget(sessionsPage);
    m_tabStack->addWidget(workflowsPage);
    layout->addWidget(m_tabStack);
    layout->addStretch();

    scroll->setWidget(content);
    root->addWidget(scroll, 1);

    // El panel derecho del Dashboard queda oculto por diseño.
    // Su información vive ahora en el popover del badge de modelo de la TopBar,
    // así el Dashboard respira mejor y no queda una columna fija robando espacio.

    // Estado inicial
    setActiveModel("Sin modelo");
    selectTab(0);
    rebuildStats();
    rebuildWorkflowsQuick();
    rebuildProjectsTab();
    rebuildSessionsTab();
    rebuildWorkflowsTab();

    QTimer::singleShot(100, this, &DashboardView::triggerInitialAnimations);
}

bool DashboardView::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::MouseButtonRelease) {
        auto *card = qobject_cast<QWidget*>(watched);
        const QString workflowId = card ? card->property("workflowId").toString() : QString();
        if (!workflowId.isEmpty()) {
            for (const DashWorkflow &workflow : m_workflows) {
                if (workflow.id == workflowId) {
                    prepareWorkflow(workflow);
                    return true;
                }
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void DashboardView::setActiveModel(const QString &model) {
    m_activeModel = model.isEmpty() ? "Sin modelo" : model;
    if (m_modelLine) {
        m_modelLine->setText(m_activeModel == "Sin modelo"
            ? "Conversaciones guardadas y workflows preparados con contexto."
            : "Modelo activo: " + m_activeModel);
    }
}


void DashboardView::setProjects(const QVector<DashProject> &projects) {
    m_projects = projects;
    rebuildStats();
    rebuildProjectsTab();
}

void DashboardView::setSessions(const QVector<DashSession> &sessions) {
    m_sessions = sessions;
    rebuildStats();
    rebuildSessionsTab();
}

void DashboardView::setWorkflows(const QVector<DashWorkflow> &workflows) {
    m_workflows = workflows;
    rebuildWorkflowsQuick();
    rebuildWorkflowsTab();
}

void DashboardView::clearLayout(QLayout *layout) {
    QLayoutItem *item;
    while ((item = layout->takeAt(0)) != nullptr) {
        if (item->widget()) item->widget()->deleteLater();
        if (item->layout()) clearLayout(item->layout());
        delete item;
    }
}

void DashboardView::selectTab(int index) {
    m_tabStack->setCurrentIndex(index);
    auto style = [](QPushButton *btn, bool active) {
        btn->setChecked(active);
        if (active) {
            btn->setStyleSheet(QString(
                "QPushButton {"
                "  background: %1; color: white; border: 2px solid %2; border-radius: 8px;"
                "  padding: 0 16px; font-size: 12px; font-weight: 800;"
                "}"
            ).arg(Style::VIOLET, Style::INK));
        } else {
            btn->setStyleSheet(QString(
                "QPushButton {"
                "  background: transparent; color: %1; border: 2px solid transparent; border-radius: 8px;"
                "  padding: 0 16px; font-size: 12px; font-weight: 800;"
                "}"
                "QPushButton:hover { color: %2; }"
            ).arg(Style::TEXT_MUTED, Style::VIOLET));
        }
    };
    style(m_tabProjects, index == 0);
    style(m_tabSessions, index == 1);
    style(m_tabHistory, index == 2);
}

QWidget* DashboardView::makeStatCard(const QString &iconRes, const QString &color, const QString &value, const QString &label) {
    auto *card = new SolidPanel();
    card->setFillColor(QColor(Style::WHITE));
    card->setCornerRadius(12);
    card->setFullBorder(QColor(Style::INK), 2);
    card->setHardShadow(QColor(Style::INK), 3, 3);
    card->setHoverEffect(true);

    auto *cardLayout = new QHBoxLayout(card);
    cardLayout->setContentsMargins(14, 12, 17, 15);
    cardLayout->setSpacing(10);

    cardLayout->addWidget(makeIconLabel(iconRes, QColor(Style::WHITE), 38, 17, color, 10));

    auto *textCol = new QVBoxLayout();
    textCol->setSpacing(0);
    auto *valueLbl = new QLabel(value);
    valueLbl->setStyleSheet(QString("font-size: 19px; font-weight: 900; color: %1; border: none;").arg(Style::INK));
    auto *labelLbl = new QLabel(label);
    labelLbl->setStyleSheet(QString("font-size: 11px; color: %1; font-weight: 600; border: none;").arg(Style::TEXT_MUTED));
    textCol->addWidget(valueLbl);
    textCol->addWidget(labelLbl);
    cardLayout->addLayout(textCol);
    cardLayout->addStretch();

    return card;
}

void DashboardView::rebuildStats() {
    clearLayout(m_statsRow);
    m_statsRow->addWidget(makeStatCard(":/icons/icons/folder.svg", Style::VIOLET, QString::number(m_projects.size()), "Proyectos"), 1);
    m_statsRow->addWidget(makeStatCard(":/icons/icons/message.svg", "#0ea5e9", QString::number(m_sessions.size()), "Sesiones"), 1);
    int attachments = 0;
    for (const auto &session : m_sessions) attachments += session.attachmentCount;
    m_statsRow->addWidget(makeStatCard(":/icons/icons/file-text.svg", Style::GREEN,
                                        QString::number(attachments), "Archivos adjuntos"), 1);
}

QWidget* DashboardView::makeWorkflowQuickCard(const DashWorkflow &wf) {
    auto *card = new SolidPanel();
    card->setFillColor(QColor(Style::WHITE));
    card->setCornerRadius(10);
    card->setFullBorder(QColor(Style::INK), 2);
    card->setHardShadow(QColor(Style::INK), 2, 2);
    card->setHoverEffect(true);
    card->setCursor(Qt::PointingHandCursor);
    card->setProperty("workflowId", wf.id);

    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(10, 9, 12, 11);
    cardLayout->setSpacing(6);
    cardLayout->addWidget(makeIconLabel(wf.iconRes, QColor(Style::WHITE), 26, 13, Style::VIOLET, 7));
    auto *lbl = new QLabel(wf.label);
    lbl->setWordWrap(true);
    lbl->setStyleSheet(QString("font-size: 11px; font-weight: 800; color: %1; border: none;").arg(Style::INK));
    cardLayout->addWidget(lbl);
    card->installEventFilter(this);
    return card;
}

void DashboardView::rebuildWorkflowsQuick() {
    clearLayout(m_workflowsQuickRow);
    for (const auto &wf : m_workflows) {
        m_workflowsQuickRow->addWidget(makeWorkflowQuickCard(wf), 1);
    }
}

QWidget* DashboardView::makeWorkflowFullCard(const DashWorkflow &wf) {
    auto *card = new SolidPanel();
    card->setFillColor(QColor(Style::WHITE));
    card->setCornerRadius(12);
    card->setFullBorder(QColor(Style::INK), 2);
    card->setHardShadow(QColor(Style::INK), 3, 3);
    card->setHoverEffect(true);
    card->setCursor(Qt::PointingHandCursor);
    card->setProperty("workflowId", wf.id);

    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(16, 14, 19, 17);
    cardLayout->setSpacing(8);
    cardLayout->addWidget(makeIconLabel(wf.iconRes, QColor(Style::WHITE), 36, 17, Style::VIOLET, 10));
    auto *lbl = new QLabel(wf.label);
    lbl->setStyleSheet(QString("font-size: 13px; font-weight: 800; color: %1; border: none;").arg(Style::INK));
    auto *sub = new QLabel(wf.description);
    sub->setWordWrap(true);
    sub->setStyleSheet(QString("font-size: 11px; color: %1; border: none;").arg(Style::TEXT_MUTED));
    cardLayout->addWidget(lbl);
    cardLayout->addWidget(sub);
    auto *prepareBtn = makeGhostButton("Preparar workflow");
    connect(prepareBtn, &QPushButton::clicked, this, [this, wf]() { prepareWorkflow(wf); });
    cardLayout->addWidget(prepareBtn, 0, Qt::AlignLeft);
    card->installEventFilter(this);
    return card;
}

void DashboardView::prepareWorkflow(const DashWorkflow &workflow) {
    VoryelDialog dialog(workflow.label, this);
    dialog.setMinimumWidth(500);
    auto *bodyLayout = dialog.bodyLayout();
    auto *title = new QLabel(workflow.label);
    title->setStyleSheet(QString("font-size: 18px; font-weight: 900; color: %1; border: none;").arg(Style::INK));
    bodyLayout->addWidget(title);
    auto *description = new QLabel(workflow.description);
    description->setWordWrap(true);
    description->setStyleSheet(QString("font-size: 12px; color: %1; border: none;").arg(Style::TEXT_MUTED));
    bodyLayout->addWidget(description);
    auto *question = new QLabel(workflow.question);
    question->setWordWrap(true);
    question->setStyleSheet(QString("font-size: 12px; font-weight: 800; color: %1; border: none;").arg(Style::INK));
    bodyLayout->addWidget(question);

    auto *context = new QPlainTextEdit(&dialog);
    context->setPlaceholderText("Pegá el código, el error, una descripción o el contexto necesario...");
    context->setMinimumHeight(150);
    context->setStyleSheet(QString(
        "QPlainTextEdit { background: %1; color: %2; border: 2px solid %3; border-radius: 10px;"
        " padding: 9px; font-size: 12px; } QPlainTextEdit:focus { border-color: %4; }"
    ).arg(Style::WHITE, Style::INK, Style::INK, Style::VIOLET));
    bodyLayout->addWidget(context);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    auto *cancel = buttons->button(QDialogButtonBox::Cancel);
    cancel->setText("Cancelar");
    cancel->setCursor(Qt::PointingHandCursor);
    cancel->setStyleSheet(QString(
        "QPushButton { background: white; color: %1; border: 2px solid %1; border-radius: 8px;"
        " padding: 7px 13px; font-weight: 850; } QPushButton:hover { background: #e5d3ff; }"
    ).arg(Style::INK));
    auto *openChat = buttons->addButton("Abrir en chat", QDialogButtonBox::AcceptRole);
    openChat->setEnabled(false);
    openChat->setCursor(Qt::PointingHandCursor);
    openChat->setStyleSheet(QString(
        "QPushButton { background: %1; color: white; border: 2px solid %2; border-radius: 8px;"
        " padding: 7px 13px; font-weight: 900; } QPushButton:disabled { background: #c4b5fd; color: #f5f3ff; }"
    ).arg(Style::VIOLET, Style::INK));
    connect(context, &QPlainTextEdit::textChanged, &dialog, [context, openChat]() {
        openChat->setEnabled(!context->toPlainText().trimmed().isEmpty());
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(openChat, &QPushButton::clicked, &dialog, &QDialog::accept);
    bodyLayout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted) return;
    const QString suppliedContext = context->toPlainText().trimmed();
    if (suppliedContext.isEmpty()) return;
    emit chatRequested(workflow.promptPrefix + "\n\nContexto proporcionado:\n" + suppliedContext, QString());
}

void DashboardView::triggerInitialAnimations() {
    if (m_initialAnimated) return;
    m_initialAnimated = true;
}

void DashboardView::rebuildWorkflowsTab() {
    clearLayout(m_workflowsFullLayout);
    auto *grid = new QGridLayout();
    grid->setSpacing(12);
    for (int i = 0; i < m_workflows.size(); ++i) {
        grid->addWidget(makeWorkflowFullCard(m_workflows[i]), i / 2, i % 2);
    }
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    m_workflowsFullLayout->addLayout(grid);
    m_workflowsFullLayout->addStretch();
}

QWidget* DashboardView::makeProjectRow(const DashProject &p) {
    auto *card = new SolidPanel();
    card->setFillColor(QColor(Style::WHITE));
    card->setCornerRadius(12);
    card->setFullBorder(QColor(Style::INK), 2);
    card->setHardShadow(QColor(Style::INK), 3, 3);
    card->setHoverEffect(true);

    auto *rowLayout = new QHBoxLayout(card);
    rowLayout->setContentsMargins(14, 12, 17, 15);
    rowLayout->setSpacing(12);

    auto *avatar = new QLabel(p.name.left(2).toUpper());
    avatar->setAttribute(Qt::WA_StyledBackground, true);
    avatar->setFixedSize(42, 42);
    avatar->setAlignment(Qt::AlignCenter);
    avatar->setStyleSheet(QString(
        "background: %1; border: 2px solid %2; border-radius: 11px; color: white; font-weight: 900; font-size: 13px;"
    ).arg(p.color, Style::INK));
    rowLayout->addWidget(avatar);

    auto *infoCol = new QVBoxLayout();
    infoCol->setSpacing(2);
    auto *nameRow = new QHBoxLayout();
    auto *nameLbl = new QLabel(p.name);
    nameLbl->setStyleSheet(QString("font-size: 13px; font-weight: 800; color: %1; border: none;").arg(Style::INK));
    auto *typeBadge = new QLabel(p.typeLabel);
    typeBadge->setAttribute(Qt::WA_StyledBackground, true);
    typeBadge->setStyleSheet(QString(
        "font-size: 10px; font-weight: 700; color: %1; background: %2; border: 1px solid %1; border-radius: 8px; padding: 1px 8px;"
    ).arg(Style::VIOLET, Style::BG_LILAC));
    nameRow->addWidget(nameLbl);
    nameRow->addWidget(typeBadge);
    nameRow->addStretch();
    infoCol->addLayout(nameRow);

    auto *taskLbl = new QLabel(QString("Directorio: %1").arg(p.lastTask));
    taskLbl->setStyleSheet(QString("font-size: 11px; color: %1; border: none;").arg(Style::TEXT_MUTED));
    infoCol->addWidget(taskLbl);

    auto *activeRow = new QHBoxLayout();
    activeRow->addWidget(makeIconLabel(":/icons/icons/history.svg", QColor(Style::TEXT_FAINT), 12, 10, "transparent", 0, false));
    auto *activeLbl = new QLabel(p.lastActive);
    activeLbl->setStyleSheet(QString("font-size: 10px; color: %1; border: none;").arg(Style::TEXT_FAINT));
    activeRow->addWidget(activeLbl);
    activeRow->addStretch();
    infoCol->addLayout(activeRow);

    rowLayout->addLayout(infoCol, 1);

    auto *continueBtn = makeGhostButton("▶  Continuar");
    connect(continueBtn, &QPushButton::clicked, this, [this, id = p.id]() {
        emit chatRequested(QString(), id);
    });
    rowLayout->addWidget(continueBtn);

    auto *openBtn = new QPushButton("›");
    openBtn->setCursor(Qt::PointingHandCursor);
    openBtn->setFixedSize(34, 34);
    openBtn->setStyleSheet(QString(
        "QPushButton {"
        "  background: %1; color: %2; border: 2px solid %2; border-radius: 9px; font-size: 16px; font-weight: 900;"
        "}"
        "QPushButton:hover { background: %3; color: white; border-color: %3; }"
    ).arg(Style::BG_LILAC, Style::INK, Style::VIOLET));
    connect(openBtn, &QPushButton::clicked, this, [this, id = p.id]() {
        emit projectOpened(id);
    });
    rowLayout->addWidget(openBtn);

    return card;
}

void DashboardView::rebuildProjectsTab() {
    QLayoutItem *stretchItem = m_projectsListLayout->takeAt(m_projectsListLayout->count() - 1);
    clearLayout(m_projectsListLayout);
    if (m_projects.isEmpty()) {
        auto *empty = new SolidPanel();
        empty->setFillColor(QColor(Style::WHITE));
        empty->setCornerRadius(12);
        empty->setFullBorder(QColor(Style::INK), 2);
        empty->setHardShadow(QColor(Style::INK), 2, 2);
        auto *emptyLayout = new QVBoxLayout(empty);
        emptyLayout->setContentsMargins(16, 14, 18, 16);
        emptyLayout->addWidget(new QLabel("No hay proyectos vinculados todavía."));
        auto *detail = new QLabel("Creá un proyecto desde la sección Proyectos para verlo acá.");
        detail->setWordWrap(true);
        detail->setStyleSheet(QString("font-size: 11px; color: %1; border: none;").arg(Style::TEXT_MUTED));
        emptyLayout->addWidget(detail);
        m_projectsListLayout->addWidget(empty);
    } else {
        for (const auto &p : m_projects)
            m_projectsListLayout->addWidget(makeProjectRow(p));
    }
    if (stretchItem) m_projectsListLayout->addItem(stretchItem);
    else m_projectsListLayout->addStretch();
}

QWidget* DashboardView::makeSessionRow(const DashSession &s) {
    auto *card = new SolidPanel();
    card->setFillColor(QColor(Style::WHITE));
    card->setCornerRadius(12);
    card->setFullBorder(QColor(Style::INK), 2);
    card->setHardShadow(QColor(Style::INK), 3, 3);
    card->setHoverEffect(true);

    auto *rowLayout = new QHBoxLayout(card);
    rowLayout->setContentsMargins(14, 12, 17, 15);
    rowLayout->setSpacing(12);

    auto *infoCol = new QVBoxLayout();
    infoCol->setSpacing(4);
    auto *titleRow = new QHBoxLayout();
    auto *titleLbl = new QLabel(s.title);
    titleLbl->setStyleSheet(QString("font-size: 13px; font-weight: 800; color: %1; border: none;").arg(Style::INK));
    titleRow->addWidget(titleLbl);

    QString badgeText, badgeBg, badgeFg;
    if (s.status == "active") { badgeText = "activa"; badgeBg = "#ede9fe"; badgeFg = Style::VIOLET; }
    else if (s.status == "empty") { badgeText = "vacía"; badgeBg = "#f3f4f6"; badgeFg = Style::TEXT_MUTED; }
    else { badgeText = "reciente"; badgeBg = "#d1fae5"; badgeFg = "#065f46"; }
    auto *badge = new QLabel(badgeText);
    badge->setAttribute(Qt::WA_StyledBackground, true);
    badge->setStyleSheet(QString(
        "font-size: 10px; font-weight: 800; color: %1; background: %2; border-radius: 8px; padding: 1px 8px;"
    ).arg(badgeFg, badgeBg));
    titleRow->addWidget(badge);
    titleRow->addStretch();
    infoCol->addLayout(titleRow);

    auto *metaRow = new QHBoxLayout();
    metaRow->setSpacing(12);
    metaRow->addWidget(makeIconLabel(":/icons/icons/file-text.svg", QColor(Style::TEXT_MUTED), 12, 10, "transparent", 0, false));
    auto *filesLbl = new QLabel(QString("%1 mensajes · %2 adjuntos")
                                .arg(s.messageCount).arg(s.attachmentCount));
    filesLbl->setStyleSheet(QString("font-size: 10px; color: %1; border: none;").arg(Style::TEXT_MUTED));
    auto *timeLbl = new QLabel(s.timestamp);
    timeLbl->setStyleSheet(QString("font-size: 10px; color: %1; border: none;").arg(Style::TEXT_MUTED));
    metaRow->addWidget(filesLbl);
    if (!s.model.isEmpty()) {
        auto *modelLbl = new QLabel(s.model);
        modelLbl->setToolTip(s.model);
        modelLbl->setStyleSheet(QString("font-size: 10px; color: %1; border: none;").arg(Style::TEXT_MUTED));
        metaRow->addWidget(modelLbl);
    }
    metaRow->addWidget(timeLbl);
    metaRow->addStretch();
    infoCol->addLayout(metaRow);

    rowLayout->addLayout(infoCol, 1);
    auto *open = makeGhostButton("Abrir chat");
    connect(open, &QPushButton::clicked, this, [this, id = s.id]() { emit sessionOpened(id); });
    rowLayout->addWidget(open);

    return card;
}

void DashboardView::rebuildSessionsTab() {
    QLayoutItem *stretchItem = m_sessionsListLayout->takeAt(m_sessionsListLayout->count() - 1);
    clearLayout(m_sessionsListLayout);
    if (m_sessions.isEmpty()) {
        auto *empty = new QLabel("Todavía no hay conversaciones guardadas.");
        empty->setStyleSheet(QString("font-size: 12px; color: %1; border: none;").arg(Style::TEXT_MUTED));
        m_sessionsListLayout->addWidget(empty);
    } else {
        for (const auto &s : m_sessions)
            m_sessionsListLayout->addWidget(makeSessionRow(s));
    }
    if (stretchItem) m_sessionsListLayout->addItem(stretchItem);
    else m_sessionsListLayout->addStretch();
}
