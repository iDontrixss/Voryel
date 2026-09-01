#pragma once

#include <QDialog>
#include <QPoint>

class QLabel;
class QVBoxLayout;
class QMouseEvent;
class QPaintEvent;

class VoryelDialog : public QDialog {
    Q_OBJECT
public:
    explicit VoryelDialog(const QString &title, QWidget *parent = nullptr);

    QVBoxLayout *bodyLayout() const { return m_bodyLayout; }
    void setDialogTitle(const QString &title);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    QLabel *m_titleLabel = nullptr;
    QWidget *m_titleBar = nullptr;
    QVBoxLayout *m_bodyLayout = nullptr;
    QPoint m_dragOffset;
    bool m_dragging = false;
};
