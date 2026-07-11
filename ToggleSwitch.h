#pragma once
#include <QAbstractButton>
#include <QPropertyAnimation>

class ToggleSwitch : public QAbstractButton {
    Q_OBJECT
    Q_PROPERTY(qreal knobPos READ knobPos WRITE setKnobPos)
public:
    explicit ToggleSwitch(QWidget *parent = nullptr);
    bool isOn() const { return m_on; }
    void setOn(bool on, bool animated = true);

signals:
    void toggled(bool on);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    QSize sizeHint() const override { return QSize(44, 24); }

private:
    bool m_on = false;
    qreal m_knobPos = 0.0;
    QPropertyAnimation *m_anim = nullptr;
    qreal knobPos() const { return m_knobPos; }
    void setKnobPos(qreal pos);
};
