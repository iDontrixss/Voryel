#include "TokenRadialMeter.h"
#include "Style.h"
#include <QPainter>
#include <QMouseEvent>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QVBoxLayout>
#include <QLocale>
#include <QApplication>
#include <QScreen>
#include <QtMath>

TokenRadialMeter::TokenRadialMeter(QWidget *parent)
    : QWidget(parent)
{
    setFixedSize(METER_SIZE, METER_SIZE);
    setCursor(Qt::PointingHandCursor);
    setToolTip(QStringLiteral("Uso de contexto todavía no disponible"));
}

void TokenRadialMeter::setUsage(const TokenUsage &usage, qint64 contextLimit,
                                const QString &provider, const QString &model,
                                const QString &limitSource,
                                bool compactionPending)
{
    m_usage = usage;
    m_contextLimit = qMax<qint64>(0, contextLimit);
    m_provider = provider;
    m_model = model;
    m_limitSource = limitSource;
    m_compactionPending = compactionPending;

    if (!m_usage.valid()) {
        setToolTip(QStringLiteral("Uso de contexto todavía no disponible"));
    } else if (m_contextLimit <= 0) {
        setToolTip(QStringLiteral("Uso: %1 tokens\nLímite de contexto no informado")
            .arg(formatTokens(m_usage.total())));
    } else {
        setToolTip(QStringLiteral("Contexto: %1 / %2\nUso: %3%\n%4")
            .arg(formatTokens(m_usage.total()), formatTokens(m_contextLimit))
            .arg(percent(), 0, 'f', 1)
            .arg(m_usage.reported() ? "Reportado por el proveedor" : "Estimado"));
    }

    update();
}

double TokenRadialMeter::percent() const {
    if (m_contextLimit <= 0 || !m_usage.valid()) return 0.0;
    return qMin(100.0, (double(m_usage.total()) / double(m_contextLimit)) * 100.0);
}

QString TokenRadialMeter::formatTokens(qint64 value) const {
    if (value <= 0) return QStringLiteral("—");
    return QLocale().toString(value);
}

void TokenRadialMeter::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRectF arcRect(PEN_WIDTH, PEN_WIDTH,
                         width() - PEN_WIDTH * 2, height() - PEN_WIDTH * 2);
    p.setPen(QPen(QColor("#d4cce6"), PEN_WIDTH, Qt::SolidLine, Qt::RoundCap));
    p.drawArc(arcRect, 90 * 16, -360 * 16);

    if (!m_usage.valid() || m_contextLimit <= 0) {
        p.setPen(QPen(QColor(Style::TEXT_FAINT), 2, Qt::DashLine, Qt::RoundCap));
        p.drawArc(arcRect.adjusted(2, 2, -2, -2), 90 * 16, -360 * 16);
        return;
    }

    const double pct = percent();
    QColor progress(Style::VIOLET);
    if (pct >= 95.0) progress = QColor("#dc2626");
    else if (pct >= 85.0) progress = QColor("#f97316");
    else if (pct >= 70.0) progress = QColor("#d97706");
    p.setPen(QPen(progress, PEN_WIDTH, Qt::SolidLine, Qt::RoundCap));
    p.drawArc(arcRect, 90 * 16, -qRound(360.0 * pct / 100.0) * 16);

    p.setPen(QColor(Style::INK));
    p.setBrush(progress);
    p.drawEllipse(QPointF(width() / 2.0, height() / 2.0), 2.2, 2.2);
}

void TokenRadialMeter::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        showDetails();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void TokenRadialMeter::showDetails() {
    auto *popup = new QFrame(nullptr, Qt::Popup | Qt::FramelessWindowHint);
    popup->setAttribute(Qt::WA_DeleteOnClose);
    popup->setObjectName("contextUsagePopup");
    popup->setMinimumWidth(330);
    popup->setStyleSheet(QString(
        "QFrame#contextUsagePopup { background: %1; border: 2px solid %2; border-radius: 10px; }"
        "QLabel { border: none; background: transparent; color: %2; }"
    ).arg(Style::WHITE, Style::INK));

    auto *outer = new QVBoxLayout(popup);
    outer->setContentsMargins(16, 14, 16, 14);
    outer->setSpacing(12);

    auto *title = new QLabel("Uso del contexto");
    title->setStyleSheet("font-size: 14px; font-weight: 900;");
    outer->addWidget(title);

    auto *summary = new QLabel(m_usage.valid()
        ? (m_contextLimit > 0
              ? QString("%1 / %2 tokens · %3%")
                    .arg(formatTokens(m_usage.total()), formatTokens(m_contextLimit))
                    .arg(percent(), 0, 'f', 1)
              : QString("%1 tokens · límite no informado")
                    .arg(formatTokens(m_usage.total())))
        : QStringLiteral("Todavía no hay métricas disponibles"));
    summary->setStyleSheet(QString("font-size: 12px; color: %1;").arg(Style::TEXT_MUTED));
    outer->addWidget(summary);

    auto *grid = new QGridLayout();
    grid->setHorizontalSpacing(18);
    grid->setVerticalSpacing(7);
    int row = 0;
    auto addRow = [&](const QString &name, const QString &value) {
        auto *key = new QLabel(name);
        key->setStyleSheet(QString("font-size: 11px; color: %1;").arg(Style::TEXT_MUTED));
        auto *val = new QLabel(value);
        val->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        val->setStyleSheet("font-size: 11px; font-weight: 800;");
        grid->addWidget(key, row, 0);
        grid->addWidget(val, row, 1);
        ++row;
    };

    addRow("Entrada", formatTokens(m_usage.input));
    addRow("Salida", formatTokens(m_usage.output));
    if (m_usage.reasoning > 0) addRow("Razonamiento", formatTokens(m_usage.reasoning));
    if (m_usage.cacheRead > 0 || m_usage.cacheWrite > 0) {
        addRow("Caché leída", formatTokens(m_usage.cacheRead));
        addRow("Caché escrita", formatTokens(m_usage.cacheWrite));
    }
    addRow("Total", formatTokens(m_usage.total()));
    addRow("Límite", m_contextLimit > 0
        ? formatTokens(m_contextLimit) : QStringLiteral("No informado"));
    addRow("Modelo", m_model.isEmpty() ? QStringLiteral("—") : m_model);
    addRow("Proveedor", m_provider.isEmpty() ? QStringLiteral("—") : m_provider);
    addRow("Precisión", m_usage.reported() ? "Reportado por el proveedor" : "Estimado");
    addRow("Origen del límite", m_limitSource.isEmpty() ? QStringLiteral("—") : m_limitSource);
    if (m_compactionPending) addRow("Estado", "Compactación pendiente");
    outer->addLayout(grid);

    popup->adjustSize();
    QPoint position = mapToGlobal(QPoint(width() - popup->width(), height() + 8));
    if (QScreen *screen = QApplication::screenAt(mapToGlobal(rect().center()))) {
        const QRect available = screen->availableGeometry().adjusted(8, 8, -8, -8);
        position.setX(qBound(available.left(), position.x(),
                             available.right() - popup->width()));
        if (position.y() + popup->height() > available.bottom())
            position.setY(mapToGlobal(QPoint(0, -popup->height() - 8)).y());
    }
    popup->move(position);
    popup->show();
}
