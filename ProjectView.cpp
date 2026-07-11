#include "ProjectView.h"
#include "Style.h"
#include "SolidPanel.h"
#include "IconUtil.h"

#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QLayoutItem>
#include <QtGlobal>
#include <QSizePolicy>

namespace {

QLabel* iconBox(const QString &iconRes, const QColor &tint, int boxSize, int iconSize,
                const QString &bgColor = Style::VIOLET) {
    auto *box = new QLabel();
    box->setAttribute(Qt::WA_StyledBackground, true);
    box->setFixedSize(boxSize, boxSize);
    box->setAlignment(Qt::AlignCenter);
    box->setStyleSheet(QString(
        "background: %1; border: 2px solid %2; border-radius: %3px;"
    ).arg(bgColor, Style::INK, QString::number(qMax(6, boxSize / 4))));
    box->setPixmap(IconUtil::coloredIcon(iconRes, tint, QSize(iconSize, iconSize)).pixmap(iconSize, iconSize));
    return box;
}

QPushButton* primaryButton(const QString &text) {
    auto *btn = new QPushButton(text);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setMinimumHeight(38);
    btn->setStyleSheet(QString(
        "QPushButton {"
        "  background: %1; color: white; border: 2px solid %2; border-radius: 10px;"
        "  padding: 0 16px; font-size: 13px; font-weight: 900;"
        "}"
        "QPushButton:hover { background: %3; }"
        "QPushButton:pressed { padding-top: 2px; }"
    ).arg(Style::VIOLET, Style::INK, Style::VIOLET_DARK));
    return btn;
}

QPushButton* ghostButton(const QString &text) {
    auto *btn = new QPushButton(text);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setMinimumHeight(34);
    btn->setStyleSheet(QString(
        "QPushButton {"
        "  background: %1; color: %2; border: 2px solid %2; border-radius: 9px;"
        "  padding: 0 12px; font-size: 12px; font-weight: 800;"
        "}"
        "QPushButton:hover { background: %3; color: white; border-color: %3; }"
    ).arg(Style::BG_LILAC, Style::INK, Style::VIOLET));
    return btn;
}

QLabel* muted(const QString &text, int px = 12) {
    auto *lbl = new QLabel(text);
    lbl->setWordWrap(true);
    lbl->setStyleSheet(QString("font-size: %1px; color: %2; font-weight: 600; border: none;")
                           .arg(px).arg(Style::TEXT_MUTED));
    return lbl;
}

} // namespace

ProjectView::ProjectView(QWidget *parent) : QWidget(parent) {
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QString("background: %1;").arg(Style::BG_LILAC));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

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
    m_contentLayout = new QVBoxLayout(content);
    m_contentLayout->setContentsMargins(Style::PAGE_MARGIN_X, Style::PAGE_MARGIN_Y, Style::PAGE_MARGIN_X, 38);
    m_contentLayout->setSpacing(18);

    scroll->setWidget(content);
    root->addWidget(scroll);

    rebuild();
}

void ProjectView::setProjects(const QVector<ProjectEntry> &projects) {
    m_projects = projects;
    rebuild();
}

void ProjectView::setActiveProject(const QString &projectId) {
    m_activeProject = projectId;
    rebuild();
}

void ProjectView::clearLayout(QLayout *layout) {
    QLayoutItem *item;
    while ((item = layout->takeAt(0)) != nullptr) {
        if (item->widget()) item->widget()->deleteLater();
        if (item->layout()) clearLayout(item->layout());
        delete item;
    }
}

void ProjectView::rebuild() {
    if (!m_contentLayout) return;
    clearLayout(m_contentLayout);

    auto *header = new QHBoxLayout();
    auto *titleCol = new QVBoxLayout();
    titleCol->setSpacing(2);

    auto *title = new QLabel("Proyectos");
    title->setStyleSheet(QString("font-size: 24px; font-weight: 900; color: %1; border: none;").arg(Style::INK));
    auto *subtitle = muted("Abrí una carpeta, creá un workspace y dejá que Voryel entienda el contexto.", 12);
    titleCol->addWidget(title);
    titleCol->addWidget(subtitle);
    header->addLayout(titleCol, 1);

    auto *newBtn = primaryButton("＋  Crear proyecto");
    connect(newBtn, &QPushButton::clicked, this, &ProjectView::createProjectRequested);
    header->addWidget(newBtn, 0, Qt::AlignTop);
    m_contentLayout->addLayout(header);

    if (m_projects.isEmpty()) {
        m_contentLayout->addStretch(1);
        auto *emptyRow = new QHBoxLayout();
        emptyRow->addStretch(1);
        emptyRow->addWidget(makeEmptyState());
        emptyRow->addStretch(1);
        m_contentLayout->addLayout(emptyRow);
        m_contentLayout->addStretch(2);
        return;
    }

    auto *hint = new SolidPanel();
    hint->setFillColor(QColor(Style::WHITE));
    hint->setCornerRadius(12);
    hint->setFullBorder(QColor(Style::INK), 2);
    hint->setHardShadow(QColor(Style::INK), 3, 3);
    hint->setHoverEffect(true);
    auto *hintLayout = new QHBoxLayout(hint);
    hintLayout->setContentsMargins(14, 12, 17, 15);
    hintLayout->setSpacing(12);
    hintLayout->addWidget(iconBox(":/icons/icons/folder.svg", QColor(Style::WHITE), 36, 17));
    auto *hintText = new QVBoxLayout();
    auto *hintTitle = new QLabel("Workspaces guardados");
    hintTitle->setStyleSheet(QString("font-size: 13px; font-weight: 900; color: %1; border: none;").arg(Style::INK));
    auto *hintSub = muted("Estos son proyectos mock por ahora. Después se conectan a carpetas reales y proyectos recientes.", 11);
    hintText->addWidget(hintTitle);
    hintText->addWidget(hintSub);
    hintLayout->addLayout(hintText, 1);
    m_contentLayout->addWidget(hint);

    auto *grid = new QGridLayout();
    grid->setSpacing(16);
    for (int i = 0; i < m_projects.size(); ++i) {
        grid->addWidget(makeProjectCard(m_projects[i]), i / 2, i % 2);
    }
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    m_contentLayout->addLayout(grid);
    m_contentLayout->addStretch();
}

QWidget* ProjectView::makeEmptyState() {
    auto *card = new SolidPanel();
    card->setFillColor(QColor(Style::WHITE));
    card->setCornerRadius(18);
    card->setFullBorder(QColor(Style::INK), 2);
    card->setHardShadow(QColor(Style::INK), 4, 4);
    card->setHoverEffect(true);
    card->setMinimumWidth(460);
    card->setMaximumWidth(560);

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(28, 26, 32, 32);
    layout->setSpacing(12);
    layout->setAlignment(Qt::AlignCenter);

    layout->addWidget(iconBox(":/icons/icons/folder.svg", QColor(Style::WHITE), 64, 28), 0, Qt::AlignHCenter);

    auto *title = new QLabel("Todavía no hay proyectos");
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet(QString("font-size: 20px; font-weight: 900; color: %1; border: none;").arg(Style::INK));
    layout->addWidget(title);

    auto *sub = muted("Creá o abrí un proyecto para que Voryel pueda leer su estructura, reglas y contexto.", 13);
    sub->setAlignment(Qt::AlignCenter);
    layout->addWidget(sub);

    auto *btn = primaryButton("Crear primer proyecto");
    btn->setMinimumWidth(210);
    connect(btn, &QPushButton::clicked, this, &ProjectView::createProjectRequested);
    layout->addWidget(btn, 0, Qt::AlignHCenter);

    auto *note = muted("Luego aparecerá acá como una card de acceso rápido.", 11);
    note->setAlignment(Qt::AlignCenter);
    layout->addWidget(note);
    return card;
}

QWidget* ProjectView::makeProjectCard(const ProjectEntry &project) {
    auto *card = new SolidPanel();
    card->setFillColor(QColor(Style::WHITE));
    card->setCornerRadius(14);
    card->setFullBorder(QColor(project.id == m_activeProject ? Style::VIOLET : Style::INK), 2);
    card->setHardShadow(QColor(project.id == m_activeProject ? Style::VIOLET : Style::INK), 3, 3);
    card->setHoverEffect(true);
    card->setMinimumHeight(150);

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(16, 14, 19, 18);
    layout->setSpacing(10);

    auto *top = new QHBoxLayout();
    auto *avatar = new QLabel(project.name.left(2).toUpper());
    avatar->setAttribute(Qt::WA_StyledBackground, true);
    avatar->setFixedSize(44, 44);
    avatar->setAlignment(Qt::AlignCenter);
    avatar->setStyleSheet(QString(
        "background: %1; border: 2px solid %2; border-radius: 12px; color: white; font-weight: 900; font-size: 13px;"
    ).arg(project.color.isEmpty() ? Style::VIOLET : project.color, Style::INK));
    top->addWidget(avatar);

    auto *nameCol = new QVBoxLayout();
    nameCol->setSpacing(2);
    auto *name = new QLabel(project.name);
    name->setStyleSheet(QString("font-size: 14px; font-weight: 900; color: %1; border: none;").arg(Style::INK));
    auto *path = new QLabel(project.path);
    path->setStyleSheet(QString("font-size: 11px; color: %1; font-family: 'Consolas','Courier New',monospace; border: none;").arg(Style::TEXT_MUTED));
    nameCol->addWidget(name);
    nameCol->addWidget(path);
    top->addLayout(nameCol, 1);

    auto *badge = new QLabel(project.stack);
    badge->setAttribute(Qt::WA_StyledBackground, true);
    badge->setStyleSheet(QString(
        "font-size: 10px; font-weight: 800; color: %1; background: %2; border: 1px solid %1; border-radius: 8px; padding: 2px 8px;"
    ).arg(Style::VIOLET, Style::BG_LILAC));
    top->addWidget(badge, 0, Qt::AlignTop);
    layout->addLayout(top);

    auto *meta = muted(QString("Última actividad: %1").arg(project.lastActive), 11);
    layout->addWidget(meta);
    layout->addStretch();

    auto *actions = new QHBoxLayout();
    auto *openBtn = primaryButton("Abrir");
    connect(openBtn, &QPushButton::clicked, this, [this, id = project.id]() { emit projectOpened(id); });
    auto *rulesBtn = ghostButton("Ver reglas");
    actions->addWidget(openBtn);
    actions->addWidget(rulesBtn);
    layout->addLayout(actions);

    return card;
}
