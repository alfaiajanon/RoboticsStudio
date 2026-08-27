#include "InputManager.h"

InputManager& InputManager::getInstance() {
    static InputManager instance;
    return instance;
}



void InputManager::bind(QWidget* widget) {
    if (targetWidget)
        targetWidget->removeEventFilter(this);

    targetWidget = widget;
    widget->installEventFilter(this);
    widget->setFocusPolicy(Qt::StrongFocus);
}

void InputManager::onKeyPress(KeyCallbackFn fn) {
    keyPressHandlers.push_back(fn);
}

void InputManager::onKeyRelease(KeyCallbackFn fn) {
    keyReleaseHandlers.push_back(fn);
}

void InputManager::onMouseButtonPress(MouseButtonCallbackFn fn) {
    mouseButtonPressHandlers.push_back(fn);
}

void InputManager::onMouseButtonRelease(MouseButtonCallbackFn fn) {
    mouseButtonReleaseHandlers.push_back(fn);
}

void InputManager::onCursorPos(CursorPosCallbackFn fn) {
    cursorPosHandlers.push_back(fn);
}

void InputManager::onScroll(ScrollCallbackFn fn) {
    scrollHandlers.push_back(fn);
}

void InputManager::getCursorPosition(double& x, double& y) {
    if (!targetWidget) { x = y = 0; return; }
    QPoint p = targetWidget->mapFromGlobal(QCursor::pos());
    x = p.x();
    y = p.y();
}


bool InputManager::eventFilter(QObject* obj, QEvent* event) {
    if (obj != targetWidget)
        return false;

    switch (event->type()){
        
        case QEvent::KeyPress:{
            auto *e = static_cast<QKeyEvent *>(event);
            int key = e->key();
            int mods = e->modifiers();

            for (auto &fn : keyPressHandlers)
                fn(key, mods);

            return true;
        }

        case QEvent::KeyRelease:{
            auto *e = static_cast<QKeyEvent *>(event);
            int key = e->key();
            int mods = e->modifiers();

            for (auto &fn : keyReleaseHandlers)
                fn(key, mods);

            return true;
        }

        case QEvent::MouseButtonPress:{
            auto *e = static_cast<QMouseEvent *>(event);
            int button = static_cast<int>(e->button());
            int mods = e->modifiers();

            for (auto &fn : mouseButtonPressHandlers)
                fn(button, mods);

            return true;
        }

        case QEvent::MouseButtonRelease:{
            auto *e = static_cast<QMouseEvent *>(event);
            int button = static_cast<int>(e->button());
            int mods = e->modifiers();

            for (auto &fn : mouseButtonReleaseHandlers)
                fn(button, mods);

            return true;
        }

        case QEvent::MouseMove:{
            auto *e = static_cast<QMouseEvent *>(event);
            QPointF pos = e->position();

            for (auto &fn : cursorPosHandlers)
                fn(pos.x(), pos.y());

            return true;
        }

        case QEvent::Wheel:{
            auto *e = static_cast<QWheelEvent *>(event);

            double dx = e->angleDelta().x() / 120.0;
            double dy = e->angleDelta().y() / 120.0;

            for (auto &fn : scrollHandlers)
                fn(dx, dy);

            return true;
        }

        default:
            break;
    }

    return QObject::eventFilter(obj, event);
}