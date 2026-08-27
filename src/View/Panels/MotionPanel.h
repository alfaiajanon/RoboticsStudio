// src/View/Panels/MotionPanel.h
#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QMap>
#include <memory>
#include "View/Panels/Canvas.h"

class BodyMotionSource;

/*
 * Body Motion tab of the PlotPanel.
 * Lets the user pick a component + one of its physical bodies + a motion
 * quantity (linear/angular velocity/acceleration) and plots it through a
 * BodyMotionSource feeding a shared VectorCanvasWindow.
 */
class MotionPanel : public QWidget {
    Q_OBJECT
private:
    QVBoxLayout* targetsLayout;
    VectorCanvasWindow* plotWindow;
    QMap<int, QWidget*> targetRows; // channelId -> row widget

public:
    explicit MotionPanel(QWidget* parent = nullptr);
    ~MotionPanel() override;

private slots:
    void showAddTargetDialog();
};
