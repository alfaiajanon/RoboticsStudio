#pragma once

#include <QWidget>
#include <QString>

class QLabel;
class QTimer;
class QPropertyAnimation;

class Toast : public QWidget {
    Q_OBJECT

public:
    static void showMessage(QWidget* parent, const QString& message, int durationMs = 2000);

private:
    explicit Toast(QWidget* parent, const QString& message, int durationMs);

    void fadeIn();
    void fadeOut();

    float defaultOpacity = 0.8f;
    QLabel* label;
    QTimer* timer;
    QPropertyAnimation* animation;
};