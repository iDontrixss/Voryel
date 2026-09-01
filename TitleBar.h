#pragma once
#include <QWidget>
#include <QLabel>
#include <QPoint>
#include <QPushButton>
#include <QColor>

class SolidPanel;

class TitleBar : public QWidget {
    Q_OBJECT
public:
    explicit TitleBar(QWidget *parent = nullptr);
    void setProjectName(const QString &name);
    void setActiveModel(const QString &modelName);

    bool isDragRegion(const QPoint &localPos) const;
    void syncMaximizeButton();

signals:
    void modelBadgeClicked(const QPoint &globalPos);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    static QLabel* makeModelIcon(const QString &modelName);
    static QColor modelColor(const QString &modelName);

    QLabel *m_breadcrumbSlash;
    QLabel *m_projectLabel;
    QLabel *m_modelLabel;
    SolidPanel *m_modelBadge;
    QPushButton *m_minBtn;
    QPushButton *m_maxBtn;
    QPushButton *m_closeBtn;
};
