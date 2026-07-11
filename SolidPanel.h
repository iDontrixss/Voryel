#pragma once
#include <QFrame>
#include <QColor>

class QPropertyAnimation;

// Panel que pinta su propio fondo/bordes con QPainter en vez de QSS.
// Motivo: en algunos entornos Windows + Fusion, un QWidget/QFrame con
// stylesheet de background no siempre pinta de forma confiable (el color
// queda "lavado" o directamente no se ve). Pintando a mano con fillRect
// no depende del motor de estilos y siempre se ve exactamente el color
// que pedimos.
class SolidPanel : public QFrame {
    Q_OBJECT
    Q_PROPERTY(qreal hoverProgress READ hoverProgress WRITE setHoverProgress)
public:
    explicit SolidPanel(QWidget *parent = nullptr);

    void setFillColor(const QColor &color);
    void setTopBorder(const QColor &color, int width);
    void setBottomBorder(const QColor &color, int width);
    void setFullBorder(const QColor &color, int width);
    void setCornerRadius(int radius);

    // Sombra dura offset pintada a mano (NO usa QGraphicsDropShadowEffect).
    // El efecto de Qt captura el widget como pixmap y en varios casos de
    // Windows lo trata como bounding-box rectangular aunque el fondo sea
    // redondeado, dejando una "tira" recta que no sigue la curva del borde,
    // y a veces se recorta directamente si el widget queda pegado al borde
    // de su contenedor (por eso algunas cards se veían sin sombra). Pintar
    // la sombra nosotros mismos, con el mismo QPainterPath del borde,
    // garantiza que siempre siga la forma exacta y nunca se recorte.
    // dx/dy reservan espacio real dentro del widget (hay que dejarles lugar
    // en el layout, ver DashboardView para el patrón de uso).
    void setHardShadow(const QColor &color, int dx, int dy);
    void setHoverEffect(bool enabled);

    qreal hoverProgress() const { return m_hoverProgress; }
    void setHoverProgress(qreal progress);

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QColor m_fill = Qt::transparent;
    QColor m_topBorderColor = Qt::transparent;
    int m_topBorderWidth = 0;
    QColor m_bottomBorderColor = Qt::transparent;
    int m_bottomBorderWidth = 0;
    QColor m_fullBorderColor = Qt::transparent;
    int m_fullBorderWidth = 0;
    int m_radius = 0;
    QColor m_shadowColor = Qt::transparent;
    int m_shadowDx = 0;
    int m_shadowDy = 0;
    bool m_hoverEffect = false;
    qreal m_hoverProgress = 0.0;
    int m_shadowDxBase = 0;
    int m_shadowDyBase = 0;
    QPropertyAnimation *m_hoverAnim = nullptr;
};
