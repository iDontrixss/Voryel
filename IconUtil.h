#pragma once
#include <QIcon>
#include <QColor>
#include <QSize>
#include <QString>

// Renderiza un SVG (mono-color, tipo lucide-react) y lo tiñe al color pedido.
// Esto evita tener que duplicar cada ícono en gris/blanco/violeta a mano:
// un solo SVG "plantilla" + tinte en runtime.
namespace IconUtil {
QIcon coloredIcon(const QString &resourcePath, const QColor &color, QSize size = QSize(18, 18));
}
