#include "VoryelCore.h"

VoryelCore::VoryelCore(QObject *parent) : QObject(parent) {}

void VoryelCore::setState(CoreState state) {
    if (m_state == state) return;
    CoreState prev = m_state;
    m_state = state;
    emit stateChanged(m_state, prev);
}

void VoryelCore::setCurrentTask(const QString &task) {
    if (m_currentTask == task) return;
    m_currentTask = task;
    emit currentTaskChanged(m_currentTask);
}
