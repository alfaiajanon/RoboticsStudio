#include "PanelCorners.h"

#include <QEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include "View/Theme.h"



PanelCorners* PanelCorners::install(QWidget* target) {
    return new PanelCorners(target);
}


PanelCorners::PanelCorners(QWidget* target) : QWidget(target) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    // the global dark.qss paints every QWidget; this one must stay see-through
    setObjectName("panelCorners");
    setStyleSheet("#panelCorners { background: transparent; }");
    setFocusPolicy(Qt::NoFocus);

    setGeometry(target->rect());
    target->installEventFilter(this);
    raise();
    show();
}


void PanelCorners::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath card;
    card.addRoundedRect(r, Theme::panelRadius, Theme::panelRadius);

    // Corner wedges: everything outside the rounded card
    QPainterPath outside;
    outside.addRect(rect());
    p.fillPath(outside.subtracted(card), Theme::window);

    p.setPen(QPen(Theme::seam, 1));
    p.setBrush(Qt::NoBrush);
    p.drawPath(card);
}


bool PanelCorners::eventFilter(QObject* watched, QEvent* event) {
    if (watched == parentWidget()) {
        switch (event->type()) {
            case QEvent::Resize:
                setGeometry(parentWidget()->rect());
                break;
            case QEvent::ChildAdded:
                // panels rebuild their content; new children stack above us
                scheduleRaise();
                break;
            default:
                break;
        }
    }
    return QWidget::eventFilter(watched, event);
}


void PanelCorners::scheduleRaise() {
    if (raisePending) return;
    raisePending = true;
    QTimer::singleShot(0, this, [this]() {
        raisePending = false;
        raise();
    });
}
