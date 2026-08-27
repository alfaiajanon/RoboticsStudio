// src/UI/Docking/PlotPanel.h
#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QTabWidget>
#include "Canvas.h"
#include "MotionPanel.h"

class PlotPanel : public QWidget {
    Q_OBJECT
private:
    QTabWidget* tabWidget;
    QVBoxLayout* canvasListLayout;

public:
    explicit PlotPanel(QWidget* parent = nullptr);

private slots:
    void showAddCanvasDialog();
};
