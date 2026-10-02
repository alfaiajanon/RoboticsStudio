// src/UI/Docking/PlotPanel.h
#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QTabWidget>
#include "Canvas.h"
#include "MotionPanel.h"
#include <QJsonObject>

class PlotPanel : public QWidget {
    Q_OBJECT
private:
    QTabWidget* tabWidget;
    QVBoxLayout* canvasListLayout;
    MotionPanel* motionPanel;

    CanvasDockItem* addCanvas(DataType type, const QString& name);

public:
    explicit PlotPanel(QWidget* parent = nullptr);

    // Project persistence: the "plotting" object of the .rsproj.
    QJsonObject toJson() const;
    void loadJson(const QJsonObject& obj);   // replaces every canvas / motion target

private slots:
    void showAddCanvasDialog();
};
