#include "TokenRadialMeter.h"
#include "Style.h"
#include <QPainter>
#include <QToolTip>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QtMath>

TokenRadialMeter::TokenRadialMeter(QWidget *parent)
    : QWidget(parent)
{
    setFixedSize(METER_SIZE, METER_SIZE);
    setCursor(Qt::PointingHandCursor);
    setToolTip(QStringLiteral("Contexto: 0 / 32768 tokens\nUso: 0%"));
}

void TokenRadialMeter::setValue(int currentTokens, int maxTokens,
                                 const QString &provider, const QString &model,
                                 const QString &accuracy,
                                 bool compactionPending)
{
    m_current = qMax(0, currentTokens);
    m_max = qMax(1, maxTokens);
    m_provider = provider;
    m_model = model;
    m_accuracy = accuracy;
    m_compactionPending = compactionPending;

    double pct = percent();
    QString status;
    if (m_compactionPending)
        status = "Alto - Compactaci\u00f3n pendiente";
    else
        status = "Normal";

    setToolTip(QStringLiteral(
        "Contexto: %1 / %2 tokens\n"
        "Uso: %3%\n"
        "Modelo: %4 \u00b7 %5\n"
        "Precisi\u00f3n: %6\n"
        "Estado: %7"
    ).arg(m_current).arg(m_max)
     .arg(pct, 0, 'f', 1)
     .arg(m_provider, m_model)
     .arg(m_accuracy, status));

    update();
}

double TokenRadialMeter::percent() const {
    return (double(m_current) / m_max) * 100.0;
}

bool TokenRadialMeter::event(QEvent *event) {
    return QWidget::event(event);
}

void TokenRadialMeter::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    double pct = percent();
    int activeSegs = qMin(SEGMENTS, (int)qRound(pct / 100.0 * SEGMENTS));

    QColor inactiveColor("#d4cce6");
    QColor activeColor("#ffffff");
    QColor warnColor("#fbbf24");

    int cx = METER_SIZE / 2;
    int cy = METER_SIZE / 2;
    int radius = METER_SIZE / 2 - PEN_WIDTH;
    double startAngle = -90.0;

    for (int i = 0; i < SEGMENTS; ++i) {
        double angle = startAngle + (360.0 / SEGMENTS) * i;
        double rad = qDegreesToRadians(angle);
        double nextRad = qDegreesToRadians(angle + 360.0 / SEGMENTS);

        QPointF p1(cx + radius * qCos(rad), cy + radius * qSin(rad));
        QPointF p2(cx + radius * qCos(nextRad), cy + radius * qSin(nextRad));

        bool isActive = i < activeSegs;

        QColor color;
        if (isActive) {
            if (pct >= 90.0)
                color = warnColor;
            else
                color = activeColor;
        } else {
            color = inactiveColor;
        }

        p.setPen(QPen(color, PEN_WIDTH, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(p1, p2);
    }

    // Center percentage text
    QFont f = p.font();
    f.setPixelSize(7);
    f.setWeight(QFont::Black);
    p.setFont(f);
    p.setPen(QColor(Style::INK));
    QString pctText = QString::number((int)qRound(pct));
    QRect r(0, 0, METER_SIZE, METER_SIZE);
    p.drawText(r, Qt::AlignCenter, pctText);
}
