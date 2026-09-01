#pragma once
#include <QWidget>
#include <QString>
#include "model/ModelTypes.h"

class TokenRadialMeter : public QWidget {
    Q_OBJECT
public:
    explicit TokenRadialMeter(QWidget *parent = nullptr);

    void setUsage(const TokenUsage &usage, qint64 contextLimit,
                  const QString &provider, const QString &model,
                  const QString &limitSource,
                  bool compactionPending = false);

    qint64 currentTokens() const { return m_usage.total(); }
    qint64 maxTokens() const { return m_contextLimit; }
    double percent() const;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    TokenUsage m_usage;
    qint64 m_contextLimit = 0;
    QString m_provider;
    QString m_model;
    QString m_limitSource;
    bool m_compactionPending = false;

    void showDetails();
    QString formatTokens(qint64 value) const;

    static constexpr int METER_SIZE = 24;
    static constexpr int PEN_WIDTH = 3;
};
