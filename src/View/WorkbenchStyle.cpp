#include "WorkbenchStyle.h"

#include <QPainter>
#include <QStyleOption>
#include "View/Theme.h"



WorkbenchStyle::WorkbenchStyle() : QProxyStyle() {}


int WorkbenchStyle::pixelMetric(PixelMetric metric, const QStyleOption* option, const QWidget* widget) const {
    if (metric == PM_DockWidgetSeparatorExtent) return 3;
    return QProxyStyle::pixelMetric(metric, option, widget);
}


void WorkbenchStyle::drawPrimitive(PrimitiveElement element, const QStyleOption* option,
                                   QPainter* painter, const QWidget* widget) const {
    if (element != PE_IndicatorDockWidgetResizeHandle) {
        QProxyStyle::drawPrimitive(element, option, painter, widget);
        return;
    }

    const QRect r = option->rect;
    painter->fillRect(r, Theme::window);

    // Three dots centred along the bar
    const bool verticalBar = r.width() < r.height();
    const QPointF c = QRectF(r).center();
    const qreal spacing = 6.0, radius = 1.2;
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(Qt::NoPen);
    painter->setBrush(Theme::grip);
    for (int i = -1; i <= 1; ++i) {
        QPointF p = verticalBar ? QPointF(c.x(), c.y() + i * spacing) : QPointF(c.x() + i * spacing, c.y());
        painter->drawEllipse(p, radius, radius);
    }
    painter->restore();
}
