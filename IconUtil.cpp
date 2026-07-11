#include "IconUtil.h"
#include <QSvgRenderer>
#include <QPixmap>
#include <QPainter>

QIcon IconUtil::coloredIcon(const QString &resourcePath, const QColor &color, QSize size) {
    QPixmap source(size);
    source.fill(Qt::transparent);
    {
        QSvgRenderer renderer(resourcePath);
        QPainter painter(&source);
        painter.setRenderHint(QPainter::Antialiasing, true);
        renderer.render(&painter);
    }

    QPixmap tinted(source.size());
    tinted.fill(Qt::transparent);
    {
        QPainter painter(&tinted);
        painter.drawPixmap(0, 0, source);
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(tinted.rect(), color);
    }
    return QIcon(tinted);
}
