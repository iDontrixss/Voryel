#pragma once
#include <QWidget>
#include <QVector>
#include <QString>

class QVBoxLayout;
class QGridLayout;
class QLabel;

struct ProjectEntry {
    QString id;
    QString name;
    QString path;
    QString stack;
    QString color;
    QString lastActive;
};

// Sección simple de proyectos: si no hay proyectos muestra una llamada
// central para crear/abrir uno; cuando existan, aparecen como cards.
class ProjectView : public QWidget {
    Q_OBJECT
public:
    explicit ProjectView(QWidget *parent = nullptr);

    void setProjects(const QVector<ProjectEntry> &projects);
    void setActiveProject(const QString &projectId);

signals:
    void createProjectRequested();
    void projectOpened(const QString &projectId);

private:
    QVector<ProjectEntry> m_projects;
    QString m_activeProject;
    QVBoxLayout *m_contentLayout = nullptr;

    void rebuild();
    QWidget* makeEmptyState();
    QWidget* makeProjectCard(const ProjectEntry &project);
    void clearLayout(QLayout *layout);
};
