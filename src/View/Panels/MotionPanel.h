// src/View/Panels/MotionPanel.h
#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QMap>
#include <memory>
#include <QJsonObject>
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
    QMap<QString, QWidget*> targetRows; // channelKey -> row widget

    struct MotionTargetDesc { int uid; QString body; int quantity; bool local; };
    QMap<QString, MotionTargetDesc> targetDescs; // channelKey -> what to save

    void addMotionTarget(int uid, const QString& body, int quantity, bool local, const QColor& color = QColor());
    void removeMotionTarget(const QString& sourceKey);

public:
    explicit MotionPanel(QWidget* parent = nullptr);
    ~MotionPanel() override;

    // Project persistence ("plotting.motion" in the .rsproj).
    QJsonObject toJson() const;
    void loadJson(const QJsonObject& obj);   // replaces the current targets

private slots:
    void showAddTargetDialog();
};
