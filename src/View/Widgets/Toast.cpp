#include "Toast.h"
#include <QLabel>
#include <QTimer>
#include <QPropertyAnimation>
#include <QVBoxLayout>

// Static helper to instantiate and display a transient toast notification.
// Automatically handles memory management and positioning relative to the parent.
void Toast::showMessage(QWidget* parent, const QString& message, int durationMs) {
    Toast* toast = new Toast(parent, message, durationMs);
    toast->fadeIn();
}




// Constructs the frameless, floating toast widget.
// Sets up the dark theme styling, layout, and automatic deletion on close.
Toast::Toast(QWidget* parent, const QString& message, int durationMs)
    : QWidget(parent) {
    
    setWindowFlags(Qt::FramelessWindowHint | Qt::ToolTip | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);

    setWindowOpacity(defaultOpacity);

    label = new QLabel(message, this);
    label->setStyleSheet(R"(
        QLabel {
            background-color: #1c1c1d;
            color: #dcdcdc;
            border: 1px solid #073859;
            border-radius: 4px;
            padding: 8px 16px;
            font-weight: bold;
        }
    )");

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addWidget(label);
    layout->setContentsMargins(0, 0, 0, 0);

    adjustSize();

    if (parent) {
        QWidget* mainWindow = parent->window(); 
        QPoint globalTopLeft = mainWindow->mapToGlobal(QPoint(0, 0));
        int centerX = globalTopLeft.x() + (mainWindow->width() / 2);
        int bottomY = globalTopLeft.y() + mainWindow->height();
        move(centerX - (width() / 2), bottomY - height() - 60);
    }

    timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, &Toast::fadeOut);
    timer->start(durationMs);
}




// Initiates the opacity animation to smoothly reveal the widget.
// Uses QPropertyAnimation bound to the windowOpacity property.
void Toast::fadeIn() {
    show();
    animation = new QPropertyAnimation(this, "windowOpacity", this);
    animation->setDuration(250);
    animation->setStartValue(0.0);
    animation->setEndValue(defaultOpacity);
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}




// Initiates the opacity animation to smoothly hide the widget.
// Triggers the deletion of the widget from memory once the animation completes.
void Toast::fadeOut() {
    animation = new QPropertyAnimation(this, "windowOpacity", this);
    animation->setDuration(350);
    animation->setStartValue(defaultOpacity);
    animation->setEndValue(0.0);
    connect(animation, &QPropertyAnimation::finished, this, &QWidget::close);
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}