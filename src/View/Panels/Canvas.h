// src/UI/Docking/Canvas.h
#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QMap>
#include <QTimer>
#include <QPointer>
#include <QJsonObject>
#include <QJsonArray>
#include <qobject.h>
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

    // An invalid colour picks a random one.
    virtual void addTarget(QString sourceKey, const QString& label, const QColor& color = QColor()) = 0;
    virtual void removeTarget(QString sourceKey) = 0;
    virtual QColor targetColor(const QString& sourceKey) const = 0;
    DataType getType() const { return dataType; }
};

class ScalarCanvasWindow : public CanvasWindow {
    Q_OBJECT
private:
    QCustomPlot* customPlot;
    QTimer* updateTimer;
    QMap<QString, QCPGraph*> activeGraphs;
    QMap<QString, double> lastTime;   // newest point already plotted per target; absent = not seeded yet

    QVBoxLayout* controlsLayout;
    QMap<QString, QWidget*> controlRows;

public:
    explicit ScalarCanvasWindow(const QString& title, QWidget* parent = nullptr);
    ~ScalarCanvasWindow() override;

    void addTarget(QString sourceKey, const QString& label, const QColor& color = QColor()) override;
    void removeTarget(QString sourceKey) override;
    QColor targetColor(const QString& sourceKey) const override;

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

    void addTarget(QString sourceKey, const QString& label, const QColor& color = QColor()) override;
    void removeTarget(QString sourceKey) override;
    QColor targetColor(const QString& sourceKey) const override;

private slots:
    void onUpdateTimer();

private:
    QMap<QString, VectorGraphs> activeGraphs;
    QMap<QString, double> lastTime;   // newest point already plotted per target; absent = not seeded yet

    QComboBox* variationCombo;
    QStackedWidget* stackedWidget;

    QCustomPlot* combinedPlot;
    QCustomPlot* xPlot;
    QCustomPlot* yPlot;
    QCustomPlot* zPlot;

    QTimer* updateTimer;

    QVBoxLayout* controlsLayout;
    QMap<QString, QWidget*> controlRows;
};

// Draws one image scaled to fit, aspect kept, unsmoothed (pixels stay
// visible for low-res cameras and displays).
class ImageView : public QWidget {
    Q_OBJECT
    QImage image;
public:
    explicit ImageView(QWidget* parent = nullptr);
    void setImage(const QImage& img) { image = img; update(); }
protected:
    void paintEvent(QPaintEvent* event) override;
};

struct ImageTile {
    QWidget* container;
    ImageView* view;
    QLabel* info;
    quint64 lastSeq = 0;
    bool shown = false;
};

// Newest frame of each image target (camera outputs, display inputs), side by side.
class ImageCanvasWindow : public CanvasWindow {
    Q_OBJECT
public:
    explicit ImageCanvasWindow(const QString& title, QWidget* parent = nullptr);
    ~ImageCanvasWindow() override;

    void addTarget(QString sourceKey, const QString& label, const QColor& color = QColor()) override;
    void removeTarget(QString sourceKey) override;
    QColor targetColor(const QString& sourceKey) const override { Q_UNUSED(sourceKey); return QColor(); }

private slots:
    void onUpdateTimer();

private:
    QHBoxLayout* tilesLayout;
    QMap<QString, ImageTile> tiles;
    QTimer* updateTimer;
};

class CanvasDockItem : public QWidget {
    Q_OBJECT
private:
    DataType dataType;
    QString canvasName;
    QPointer<CanvasWindow> popUpWindow;
    bool enabled = true;   // whether this canvas currently holds subscriptions

    QVBoxLayout* targetsLayout;
    QList<QString> subscribedSources;

    QPushButton* toggleBtn = nullptr;
    bool removed = false;

    void showAddTargetDialog();
    void addTargetUI(QString channelKey, const QString& label);

protected:
    void mouseReleaseEvent(QMouseEvent* event) override;

public:
    explicit CanvasDockItem(DataType type, const QString& name, QWidget* parent = nullptr);
    ~CanvasDockItem() override;

    // Adds an I/O signal of a component instance. Returns false if the
    // component/signal no longer exists or is already on this canvas.
    bool attachIoTarget(int uid, const QString& signal, const QColor& color = QColor());
    // Starts/stops recording for every target (the Enable/Disable button).
    void setCapturing(bool on);
    // Unsubscribes, closes the pop-up and schedules the card for deletion.
    void closeCanvas();
    bool isRemoved() const { return removed; }

    // Project persistence ("plotting.canvases[]" in the .rsproj).
    QJsonObject toJson() const;
    void applyTargetsJson(const QJsonObject& obj);   // targets + enabled flag
};
