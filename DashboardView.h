#pragma once
#include <QWidget>
#include <QVector>
#include <QString>

class QLabel;
class QPushButton;
class QVBoxLayout;
class QHBoxLayout;
class QStackedWidget;

// ── Datos mostrados en el Dashboard ──────────────────────────
// Equivalentes a los tipos Project/Session/Workflow del mockup TSX
// (src/types.ts + src/data.ts), simplificados a lo que usa esta vista.

struct DashProject {
    QString id;
    QString name;
    QString typeLabel;   // "Electron", "Web", "Rust", etc.
    QString color;       // hex, ej "#7c3aed"
    QString lastTask;
    QString lastActive;
};

struct DashSession {
    QString id;
    QString title;
    QString status;       // "completed" | "active" | "error"
    QString model;
    int filesChanged = 0;
    QString timestamp;
};

struct DashWorkflow {
    QString id;
    QString iconRes;      // ruta del ícono en :/icons/...
    QString label;
};

// Vista principal: header + stats + workflows rápidos + tabs
// (Proyectos/Sesiones/Historial) + panel lateral derecho con modelo
// activo, permisos y checkpoint. Port fiel de DashboardView.tsx.
class DashboardView : public QWidget {
    Q_OBJECT
public:
    explicit DashboardView(QWidget *parent = nullptr);

    void setActiveModel(const QString &model);
    void setProjects(const QVector<DashProject> &projects);
    void setSessions(const QVector<DashSession> &sessions);
    void setWorkflows(const QVector<DashWorkflow> &workflows);

signals:
    void projectOpened(const QString &projectId);
    void chatRequested(const QString &prompt, const QString &projectId);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QString m_activeModel;
    QVector<DashProject> m_projects;
    QVector<DashSession> m_sessions;
    QVector<DashWorkflow> m_workflows;

    QLabel *m_modelLine = nullptr;
    QHBoxLayout *m_statsRow = nullptr;
    QHBoxLayout *m_workflowsQuickRow = nullptr;
    QStackedWidget *m_tabStack = nullptr;
    QPushButton *m_tabProjects = nullptr;
    QPushButton *m_tabSessions = nullptr;
    QPushButton *m_tabHistory = nullptr;
    QVBoxLayout *m_projectsListLayout = nullptr;
    QVBoxLayout *m_sessionsListLayout = nullptr;
    QVBoxLayout *m_workflowsFullLayout = nullptr;

    QVBoxLayout *m_rightFallbacksLayout = nullptr;

    void rebuildStats();
    void rebuildWorkflowsQuick();
    void rebuildProjectsTab();
    void rebuildSessionsTab();
    void rebuildWorkflowsTab();
    void selectTab(int index);
    QWidget* makeStatCard(const QString &iconRes, const QString &color, const QString &value, const QString &label);
    QWidget* makeProjectRow(const DashProject &p);
    QWidget* makeSessionRow(const DashSession &s);
    QWidget* makeWorkflowQuickCard(const DashWorkflow &wf);
    QWidget* makeWorkflowFullCard(const DashWorkflow &wf);
    void clearLayout(QLayout *layout);
    void triggerInitialAnimations();
    bool m_initialAnimated = false;
};
