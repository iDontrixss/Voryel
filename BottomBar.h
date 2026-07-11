#pragma once
#include "SolidPanel.h"
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>
#include <QPropertyAnimation>
#include <QProgressBar>
#include <QVector>
#include "Task.h"
#include "core/VoryelCore.h"

class BottomBar : public SolidPanel {
    Q_OBJECT
public:
    explicit BottomBar(QWidget *parent = nullptr);

    void setQueueStatus(QueueStatus status);
    void setCoreState(CoreState state);
    void setCurrentTask(const QString &title);
    void setActiveModel(const QString &model);
    void setTaskQueue(const QVector<Task> &tasks);

signals:
    void pauseClicked();
    void cancelClicked();
    void diffClicked();
    void logsClicked();
    void taskCompleted();

private:
    QueueStatus m_status = QueueStatus::Idle;
    QVector<Task> m_tasks;
    bool m_queueOpen = false;
    bool m_muted = false;

    // Drawer (cola de tareas)
    SolidPanel *m_drawer;
    QVBoxLayout *m_drawerLayout;
    QPropertyAnimation *m_drawerAnim;

    // Columna izquierda
    QLabel *m_statusIconLabel;
    QLabel *m_taskTitleLabel;
    QLabel *m_taskSubLabel;

    // Columna central
    QLabel *m_modelLabel;
    QProgressBar *m_progressBar;
    QLabel *m_progressText;
    QPropertyAnimation *m_progressAnim;

    // Columna derecha
    QPushButton *m_pauseBtn;
    QPushButton *m_cancelBtn;
    QToolButton *m_muteBtn;
    QToolButton *m_queueToggleBtn;

    void rebuildDrawer();
    void refreshLeftColumn();
    void refreshCenterColumn();
    void refreshRightColumn();
    void toggleQueue();
    QPushButton* makeGhostButton(const QString &iconRes, const QString &text);
};
