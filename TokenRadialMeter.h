#pragma once
#include <QWidget>
#include <QString>

class TokenRadialMeter : public QWidget {
    Q_OBJECT
public:
    explicit TokenRadialMeter(QWidget *parent = nullptr);

    void setValue(int currentTokens, int maxTokens,
                  const QString &provider, const QString &model,
                  const QString &accuracy,
                  bool compactionPending = false);

    int currentTokens() const { return m_current; }
    int maxTokens() const { return m_max; }
    double percent() const;

protected:
    void paintEvent(QPaintEvent *event) override;
    bool event(QEvent *event) override;

private:
    int m_current = 0;
    int m_max = 32768;
    QString m_provider;
    QString m_model;
    QString m_accuracy;
    bool m_compactionPending = false;

    static constexpr int SEGMENTS = 32;
    static constexpr int METER_SIZE = 28;
    static constexpr int PEN_WIDTH = 3;
};
