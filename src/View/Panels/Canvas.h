// src/UI/Docking/Canvas.h
#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QMap>
#include <QTimer>
#include "qcustomplot.h"
#include "Telemetry/TelemetryRegistry.h"

class CanvasWindow : public QWidget {
    Q_OBJECT
protected:
    DataType dataType;
    QString title;

public:
    explicit CanvasWindow(DataType type, const QString& title, QWidget* parent = nullptr);
    virtual ~CanvasWindow() = default;

    virtual void addTarget(int channelId, const QString& label) = 0;
    virtual void removeTarget(int channelId) = 0;
    DataType getType() const { return dataType; }
};

class ScalarCanvasWindow : public CanvasWindow {
    Q_OBJECT
private:
    QCustomPlot* customPlot;
    QTimer* updateTimer;
    QMap<int, QCPGraph*> activeGraphs;

    QVBoxLayout* controlsLayout;
    QMap<int, QWidget*> controlRows;

public:
    explicit ScalarCanvasWindow(const QString& title, QWidget* parent = nullptr);
    ~ScalarCanvasWindow() override;

    void addTarget(int channelId, const QString& label) override;
    void removeTarget(int channelId) override;

private slots:
    void onUpdateTimer();
};

struct VectorGraphs {
    QCPGraph* combinedX;
    QCPGraph* combinedY;
    QCPGraph* combinedZ;
    QCPGraph* sepX;
    QCPGraph* sepY;
    QCPGraph* sepZ;
};

class VectorCanvasWindow : public CanvasWindow {
    Q_OBJECT
public:
    explicit VectorCanvasWindow(const QString& title, QWidget* parent = nullptr);
    ~VectorCanvasWindow() override;

    void addTarget(int channelId, const QString& label) override;
    void removeTarget(int channelId) override;

private slots:
    void onUpdateTimer();

private:
    QMap<int, VectorGraphs> activeGraphs;

    QComboBox* variationCombo;
    QStackedWidget* stackedWidget;

    QCustomPlot* combinedPlot;
    QCustomPlot* xPlot;
    QCustomPlot* yPlot;
    QCustomPlot* zPlot;

    QTimer* updateTimer;

    QVBoxLayout* controlsLayout;
    QMap<int, QWidget*> controlRows;
};

class CanvasDockItem : public QWidget {
    Q_OBJECT
private:
    DataType dataType;
    QString canvasName;
    CanvasWindow* popUpWindow;

    QVBoxLayout* targetsLayout;
    QList<int> subscribedChannels;

    void showAddTargetDialog();
    void addTargetUI(int channelId, const QString& label);

protected:
    void mouseReleaseEvent(QMouseEvent* event) override;

public:
    explicit CanvasDockItem(DataType type, const QString& name, QWidget* parent = nullptr);
    ~CanvasDockItem() override;
};
