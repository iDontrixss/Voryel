#pragma once
#include <QWidget>
#include <QLabel>
#include <QPoint>

class TopBar : public QWidget {
    Q_OBJECT
public:
    explicit TopBar(QWidget *parent = nullptr);
    void setProjectName(const QString &name); // vacío = sin proyecto activo
    void setActiveModel(const QString &modelName);

signals:
    void modelBadgeClicked(const QPoint &globalPos);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QLabel *m_breadcrumbSlash;
    QLabel *m_projectLabel;
    QLabel *m_modelLabel;
    QWidget *m_modelBadge;
};
