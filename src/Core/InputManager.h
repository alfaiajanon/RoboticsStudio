#pragma once

#include <QObject>
#include <QWidget>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <functional>
#include <vector>

class InputManager : public QObject {
    Q_OBJECT

private:
    explicit InputManager() : QObject(nullptr), targetWidget(nullptr) {}

    QWidget* targetWidget;

    using KeyCallbackFn         = std::function<void(int key, int mods)>;
    using MouseButtonCallbackFn = std::function<void(int button, int mods)>;
    using CursorPosCallbackFn   = std::function<void(double x, double y)>;
    using ScrollCallbackFn      = std::function<void(double xoffset, double yoffset)>;

    std::vector<KeyCallbackFn> keyPressHandlers;
    std::vector<KeyCallbackFn> keyReleaseHandlers;
    std::vector<MouseButtonCallbackFn> mouseButtonPressHandlers;
    std::vector<MouseButtonCallbackFn> mouseButtonReleaseHandlers;
    std::vector<CursorPosCallbackFn> cursorPosHandlers;
    std::vector<ScrollCallbackFn> scrollHandlers;

public:

    static InputManager& getInstance();

    // Attach the InputManager to a SINGLE Qt widget (your viewport)
    void bind(QWidget* widget);

    // Registration API
    void onKeyPress(KeyCallbackFn fn);
    void onKeyRelease(KeyCallbackFn fn);

    void onMouseButtonPress(MouseButtonCallbackFn fn);
    void onMouseButtonRelease(MouseButtonCallbackFn fn);
    void onCursorPos(CursorPosCallbackFn fn);
    void onScroll(ScrollCallbackFn fn);

    // Get cursor position relative to the bound widget
    void getCursorPosition(double& x, double& y);

protected:

    // Unified event dispatcher
    bool eventFilter(QObject* obj, QEvent* event) override;
};
