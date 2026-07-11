#pragma once
#include <QObject>
#include <QString>

enum class CoreState {
    Idle,
    Thinking,
    Planning,
    AwaitingApproval,
    Executing,
    RollingBack
};

class VoryelCore : public QObject {
    Q_OBJECT
public:
    explicit VoryelCore(QObject *parent = nullptr);

    CoreState state() const { return m_state; }
    QString currentTask() const { return m_currentTask; }

public slots:
    void setState(CoreState state);
    void setCurrentTask(const QString &task);

signals:
    void stateChanged(CoreState state, CoreState previousState);
    void currentTaskChanged(const QString &task);

private:
    CoreState m_state = CoreState::Idle;
    QString m_currentTask;
};
