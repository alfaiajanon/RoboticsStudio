#include "Theme.h"

#include <QImageReader>
#include <QPainter>
#include <QPixmap>



static QPixmap tinted(const QString& svgPath, int size, const QColor& color) {
    QImageReader reader(svgPath);
    reader.setScaledSize(QSize(size, size));
    QImage img = reader.read().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (img.isNull()) return QPixmap();

    QPainter p(&img);
    p.setCompositionMode(QPainter::CompositionMode_SourceIn);
    p.fillRect(img.rect(), color);
    p.end();
    return QPixmap::fromImage(img);
}


QPixmap Theme::pixmap(const QString& svgPath, int size, const QColor& color, qreal dpr) {
    QPixmap pm = tinted(svgPath, qRound(size * dpr), color);
    pm.setDevicePixelRatio(dpr);
    return pm;
}


QIcon Theme::icon(const QString& svgPath, const QColor& onColor, const QColor& normalColor) {
    QIcon result;
    for (int size : {16, 20, 24, 32, 40, 48}) {
        result.addPixmap(tinted(svgPath, size, normalColor), QIcon::Normal, QIcon::Off);
        result.addPixmap(tinted(svgPath, size, text),      QIcon::Active, QIcon::Off);
        result.addPixmap(tinted(svgPath, size, onColor),   QIcon::Normal, QIcon::On);
        result.addPixmap(tinted(svgPath, size, onColor),   QIcon::Active, QIcon::On);
        result.addPixmap(tinted(svgPath, size, text),      QIcon::Selected, QIcon::Off);
        result.addPixmap(tinted(svgPath, size, textFaint), QIcon::Disabled, QIcon::Off);
    }
    return result;
}
