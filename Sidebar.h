#pragma once
#include <QWidget>
#include <QToolButton>
#include <QVector>
#include <QLabel>
#include <QString>
#include <QColor>
#include "Views.h"

class Sidebar : public QWidget {
    Q_OBJECT
public:
    explicit Sidebar(QWidget *parent = nullptr);
    void setActiveView(View view);
    void setChatBadge(const QString &symbol, const QColor &bgColor);
    void clearChatBadge();

protected:
    void resizeEvent(QResizeEvent *event) override;

signals:
    void navigate(View view);

private:
    struct NavItem {
        View view;
        QToolButton *button;
    };
    QVector<NavItem> m_navItems;
    View m_active = View::Dashboard;
    QLabel *m_chatBadge = nullptr;
    QToolButton *m_chatBtn = nullptr;

    QToolButton* makeNavButton(const QString &iconRes, const QString &tooltip);
    void applyButtonStyle(QToolButton *btn, bool active);
    void repositionBadge();
};
