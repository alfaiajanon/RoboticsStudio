#pragma once

#include <QProxyStyle>



/*
 * Thin proxy over the platform style for the parts dark.qss cannot draw.
 * Dock separators (the resize bars between docks): QSS can color them but
 * not draw an image on them, so they are painted here in the window color
 * with three grip dots. dark.qss must not style QMainWindow::separator,
 * or the stylesheet takes over and this is never called.
 */
class WorkbenchStyle : public QProxyStyle {
    Q_OBJECT

    public:
        WorkbenchStyle();

        int pixelMetric(PixelMetric metric, const QStyleOption* option = nullptr,
                        const QWidget* widget = nullptr) const override;
        void drawPrimitive(PrimitiveElement element, const QStyleOption* option,
                           QPainter* painter, const QWidget* widget = nullptr) const override;
};
