#include "BottomBar.h"
#include "Style.h"
#include "IconUtil.h"
#include <QHBoxLayout>
#include <QColor>
#include <QEasingCurve>

BottomBar::BottomBar(QWidget *parent) : SolidPanel(parent) {
    setFillColor(QColor(Style::WHITE));
    setTopBorder(QColor(Style::INK), 2);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // ── Drawer de cola (colapsable) ──────────────────────
    m_drawer = new SolidPanel(this);
    m_drawer->setFillColor(QColor(Style::VIOLET_DARK));
    m_drawer->setBottomBorder(QColor(0, 0, 0, 40), 2);
    m_drawerLayout = new QVBoxLayout(m_drawer);
    m_drawerLayout->setContentsMargins(20, 10, 20, 10);
    m_drawerLayout->setSpacing(6);
    m_drawer->setMaximumHeight(0);
    m_drawer->setMinimumHeight(0);
    outer->addWidget(m_drawer);

    m_drawerAnim = new QPropertyAnimation(m_drawer, "maximumHeight", this);
    m_drawerAnim->setDuration(220);
    m_drawerAnim->setEasingCurve(QEasingCurve::OutCubic);

    // ── Barra principal (3 columnas) ─────────────────────
    auto *bar = new QWidget(this);
    bar->setFixedHeight(Style::BOTTOMBAR_HEIGHT);
    auto *barLayout = new QHBoxLayout(bar);
    barLayout->setContentsMargins(20, 0, 20, 0);
    barLayout->setSpacing(20);

    // Columna izquierda
    auto *leftCol = new QWidget();
    auto *leftLayout = new QHBoxLayout(leftCol);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(12);

    m_statusIconLabel = new QLabel();
    m_statusIconLabel->setFixedSize(36, 36);
    m_statusIconLabel->setAlignment(Qt::AlignCenter);
    m_statusIconLabel->setStyleSheet(QString(
        "background: %1; border: 2px solid %2; border-radius: 10px;"
    ).arg(Style::VIOLET, Style::INK));
    leftLayout->addWidget(m_statusIconLabel);

    auto *textCol = new QWidget();
    auto *textLayout = new QVBoxLayout(textCol);
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(2);
    m_taskTitleLabel = new QLabel("Listo para trabajar");
    m_taskTitleLabel->setStyleSheet(QString("color: %1; font-weight: 800; font-size: 13px; border: none;").arg(Style::INK));
    m_taskSubLabel = new QLabel();
    m_taskSubLabel->setStyleSheet(QString("color: %1; font-weight: 700; font-size: 11px; border: none;").arg(Style::VIOLET_DARK));
    textLayout->addWidget(m_taskTitleLabel);
    textLayout->addWidget(m_taskSubLabel);
    leftLayout->addWidget(textCol, 1);

    barLayout->addWidget(leftCol, 2);

    // Columna central
    auto *centerCol = new QWidget();
    auto *centerLayout = new QVBoxLayout(centerCol);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    centerLayout->setSpacing(4);
    centerLayout->setAlignment(Qt::AlignCenter);

    m_modelLabel = new QLabel(this);
    m_modelLabel->setVisible(false);

    auto *progressRow = new QWidget();
    auto *progressRowLayout = new QHBoxLayout(progressRow);
    progressRowLayout->setContentsMargins(0, 0, 0, 0);
    progressRowLayout->setSpacing(8);

    m_progressBar = new QProgressBar();
    m_progressBar->setFixedSize(120, 6);
    m_progressBar->setTextVisible(false);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setStyleSheet(
        "QProgressBar { background: rgba(17,17,17,0.10); border: none; border-radius: 3px; }"
        "QProgressBar::chunk { background: #7c3aed; border-radius: 3px; }"
    );
    m_progressAnim = new QPropertyAnimation(m_progressBar, "value", this);
    m_progressAnim->setDuration(220);
    m_progressAnim->setEasingCurve(QEasingCurve::OutCubic);
    progressRowLayout->addWidget(m_progressBar);

    m_progressText = new QLabel("0/0");
    m_progressText->setStyleSheet(QString("color: %1; font-weight: 800; font-size: 11px; border: none;").arg(Style::VIOLET_DARK));
    progressRowLayout->addWidget(m_progressText);

    centerLayout->addWidget(progressRow, 0, Qt::AlignHCenter);
    barLayout->addWidget(centerCol, 1);

    // Columna derecha
    auto *rightCol = new QWidget();
    auto *rightLayout = new QHBoxLayout(rightCol);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(8);
    rightLayout->setAlignment(Qt::AlignRight);

    m_pauseBtn = makeGhostButton(":/icons/icons/pause.svg", "Pausar");
    m_cancelBtn = makeGhostButton(":/icons/icons/x.svg", "Cancelar");
    auto *diffBtn = makeGhostButton(":/icons/icons/git-compare.svg", "Diff");
    auto *logsBtn = makeGhostButton(":/icons/icons/eye.svg", "Logs");

    connect(m_pauseBtn, &QPushButton::clicked, this, &BottomBar::pauseClicked);
    connect(m_cancelBtn, &QPushButton::clicked, this, &BottomBar::cancelClicked);
    connect(diffBtn, &QPushButton::clicked, this, &BottomBar::diffClicked);
    connect(logsBtn, &QPushButton::clicked, this, &BottomBar::logsClicked);

    rightLayout->addWidget(m_pauseBtn);
    rightLayout->addWidget(m_cancelBtn);
    rightLayout->addWidget(diffBtn);
    rightLayout->addWidget(logsBtn);

    const QString toolBtnStyle = QString(
        "QToolButton { background: %1; border: 2px solid %2; border-radius: 8px; }"
        "QToolButton:hover { background: %3; }"
    ).arg(Style::WHITE, Style::INK, Style::VIOLET_LIGHT);

    m_muteBtn = new QToolButton();
    m_muteBtn->setFixedSize(32, 32);
    m_muteBtn->setCursor(Qt::PointingHandCursor);
    m_muteBtn->setIcon(IconUtil::coloredIcon(":/icons/icons/volume.svg", QColor(Style::INK), QSize(14, 14)));
    m_muteBtn->setIconSize(QSize(14, 14));
    m_muteBtn->setStyleSheet(toolBtnStyle);
    connect(m_muteBtn, &QToolButton::clicked, this, [this]() { m_muted = !m_muted; });
    rightLayout->addWidget(m_muteBtn);

    m_queueToggleBtn = new QToolButton();
    m_queueToggleBtn->setFixedSize(32, 32);
    m_queueToggleBtn->setCursor(Qt::PointingHandCursor);
    m_queueToggleBtn->setIcon(IconUtil::coloredIcon(":/icons/icons/list.svg", QColor(Style::INK), QSize(14, 14)));
    m_queueToggleBtn->setIconSize(QSize(14, 14));
    m_queueToggleBtn->setStyleSheet(toolBtnStyle);
    connect(m_queueToggleBtn, &QToolButton::clicked, this, &BottomBar::toggleQueue);
    rightLayout->addWidget(m_queueToggleBtn);

    barLayout->addWidget(rightCol, 2);

    outer->addWidget(bar);

    setQueueStatus(QueueStatus::Idle);
}

QPushButton* BottomBar::makeGhostButton(const QString &iconRes, const QString &text) {
    auto *btn = new QPushButton();
    btn->setText("  " + text);
    btn->setIcon(IconUtil::coloredIcon(iconRes, QColor(Style::INK), QSize(12, 12)));
    btn->setIconSize(QSize(12, 12));
    btn->setCursor(Qt::PointingHandCursor);
    btn->setStyleSheet(QString(
        "QPushButton {"
        "  background: %1; border: 2px solid %2;"
        "  border-radius: 8px; color: %2; font-weight: 800; font-size: 11px; padding: 5px 11px;"
        "}"
        "QPushButton:hover { background: %3; color: white; border-color: %3; }"
    ).arg(Style::WHITE, Style::INK, Style::VIOLET));
    return btn;
}

void BottomBar::setQueueStatus(QueueStatus status) {
    const QueueStatus prev = m_status;
    m_status = status;
    refreshLeftColumn();
    refreshRightColumn();
    if (prev == QueueStatus::Working && status == QueueStatus::Done) {
        emit taskCompleted();
    }
}

void BottomBar::setCoreState(CoreState state) {
    switch (state) {
    case CoreState::Idle:
        setQueueStatus(QueueStatus::Idle);
        break;
    case CoreState::Thinking:
    case CoreState::Planning:
    case CoreState::Executing:
    case CoreState::RollingBack:
        setQueueStatus(QueueStatus::Working);
        break;
    case CoreState::AwaitingApproval:
        setQueueStatus(QueueStatus::Paused);
        break;
    }
}

void BottomBar::setCurrentTask(const QString &title) {
    if (m_status == QueueStatus::Working) {
        m_taskTitleLabel->setText(title.isEmpty() ? "Procesando..." : title);
    }
}

void BottomBar::setActiveModel(const QString &model) {
    // Guardado para tooltip/debug futuro, pero no visible: el modelo ya está en TopBar.
    if (m_modelLabel) m_modelLabel->setText(model.isEmpty() ? "SIN MODELO" : model.toUpper());
}

void BottomBar::setTaskQueue(const QVector<Task> &tasks) {
    m_tasks = tasks;
    refreshCenterColumn();
    rebuildDrawer();
}

void BottomBar::refreshLeftColumn() {
    QIcon icon;
    QString title, sub;
    QString iconBg, iconBorder;
    switch (m_status) {
    case QueueStatus::Working:
        icon = IconUtil::coloredIcon(":/icons/icons/cpu.svg", QColor(Style::WHITE), QSize(16, 16));
        title = m_taskTitleLabel->text().isEmpty() ? "Procesando..." : m_taskTitleLabel->text();
        sub = "Voryel está trabajando";
        iconBg = Style::VIOLET;
        break;
    case QueueStatus::Done:
        icon = IconUtil::coloredIcon(":/icons/icons/check-circle.svg", QColor(Style::WHITE), QSize(16, 16));
        title = "Tarea completada";
        sub = "3 archivos · 1 comando";
        iconBg = "#059669";
        break;
    case QueueStatus::Paused:
        icon = IconUtil::coloredIcon(":/icons/icons/pause.svg", QColor(Style::WHITE), QSize(16, 16));
        title = "Pausado";
        sub = "";
        iconBg = "#d97706";
        break;
    case QueueStatus::Idle:
    default:
        icon = IconUtil::coloredIcon(":/icons/icons/play.svg", QColor(Style::VIOLET), QSize(16, 16));
        title = "Listo para trabajar";
        sub = "";
        iconBg = Style::VIOLET_LIGHT;
        break;
    }
    m_statusIconLabel->setPixmap(icon.pixmap(16, 16));
    m_statusIconLabel->setStyleSheet(QString(
        "background: %1; border: 2px solid %2; border-radius: 10px;"
    ).arg(iconBg, Style::INK));
    m_taskTitleLabel->setText(title);
    m_taskTitleLabel->setStyleSheet(QString(
        "color: %1; font-weight: 800; font-size: 13px; border: none;"
    ).arg(m_status == QueueStatus::Done ? "#065f46" :
          m_status == QueueStatus::Paused ? "#92400e" : Style::INK));
    m_taskSubLabel->setText(sub);
    m_taskSubLabel->setVisible(!sub.isEmpty());
}

void BottomBar::refreshCenterColumn() {
    const int total = m_tasks.size();
    int done = 0;
    for (const auto &t : m_tasks) if (t.status == TaskStatus::Done) done++;

    m_progressBar->setVisible(total > 0);
    m_progressText->setVisible(total > 0);
    m_progressBar->setRange(0, total > 0 ? total : 1);
    if (total > 0) {
        m_progressAnim->stop();
        m_progressAnim->setStartValue(m_progressBar->value());
        m_progressAnim->setEndValue(done);
        m_progressAnim->start();
    } else {
        m_progressBar->setValue(0);
    }
    m_progressText->setText(QString("%1/%2").arg(done).arg(total));
}

void BottomBar::refreshRightColumn() {
    const bool working = (m_status == QueueStatus::Working);
    const bool paused = (m_status == QueueStatus::Paused);
    m_pauseBtn->setVisible(working);
    m_cancelBtn->setVisible(working);
    if (paused) {
        m_pauseBtn->setVisible(true);
        m_pauseBtn->setText("  Reanudar");
        m_pauseBtn->setIcon(IconUtil::coloredIcon(":/icons/icons/play.svg", QColor(Style::INK), QSize(12, 12)));
    } else {
        m_pauseBtn->setText("  Pausar");
        m_pauseBtn->setIcon(IconUtil::coloredIcon(":/icons/icons/pause.svg", QColor(Style::INK), QSize(12, 12)));
    }
}

void BottomBar::rebuildDrawer() {
    QLayoutItem *item;
    while ((item = m_drawerLayout->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }

    if (m_tasks.isEmpty()) return;

    auto *header = new QLabel("COLA DE TAREAS");
    header->setStyleSheet(QString("color: %1; font-weight: 900; font-size: 10px; letter-spacing: 1px; border: none;").arg(Style::VIOLET_LIGHT));
    m_drawerLayout->addWidget(header);

    int i = 1;
    for (const auto &task : m_tasks) {
        auto *row = new SolidPanel();
        row->setFillColor(QColor(Style::WHITE));
        row->setCornerRadius(8);
        row->setFullBorder(QColor(Style::INK), 2);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(10, 6, 10, 6);
        rowLayout->setSpacing(10);

        auto *num = new QLabel(QString::number(i) + ".");
        num->setStyleSheet(QString("color: %1; font-weight: 900; font-size: 11px; border: none;").arg(Style::VIOLET_DARK));
        num->setFixedWidth(18);
        rowLayout->addWidget(num);

        auto *title = new QLabel(task.title);
        QString color = task.status == TaskStatus::Running ? Style::INK :
                         task.status == TaskStatus::Done ? "#065f46" : Style::VIOLET_DARK;
        title->setStyleSheet(QString("color: %1; font-weight: 700; font-size: 12px; border: none;").arg(color));
        rowLayout->addWidget(title, 1);

        if (task.status == TaskStatus::Done) {
            auto *check = new QLabel();
            check->setPixmap(IconUtil::coloredIcon(":/icons/icons/check-circle.svg", QColor("#065f46"), QSize(12, 12)).pixmap(12, 12));
            rowLayout->addWidget(check);
        }

        m_drawerLayout->addWidget(row);
        i++;
    }
}

void BottomBar::toggleQueue() {
    m_queueOpen = !m_queueOpen;
    const int targetHeight = m_queueOpen ? m_drawer->sizeHint().height() : 0;
    m_drawerAnim->stop();
    m_drawerAnim->setStartValue(m_drawer->maximumHeight());
    m_drawerAnim->setEndValue(targetHeight);
    m_drawerAnim->start();

    m_queueToggleBtn->setIcon(IconUtil::coloredIcon(
        m_queueOpen ? ":/icons/icons/chevron-down.svg" : ":/icons/icons/list.svg",
        QColor(Style::INK), QSize(14, 14)));
}
