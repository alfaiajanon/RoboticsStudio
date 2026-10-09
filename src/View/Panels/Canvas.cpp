// src/UI/Docking/Canvas.cpp
#include "Canvas.h"
#include "Application/Application.h"
#include "Document/Components/ComponentInstance.h"
#include "Telemetry/Storage/TimeSeriesBuffer.h"
#include "Utils/Log.h"

#include <QDialog>
#include <limits>
#include <algorithm>
#include <QTreeWidget>
#include <QHeaderView>
#include <QPainter>
#include <QPainterPath>
#include <QIcon>
#include <QPixmap>
#include <QDialogButtonBox>
#include <QMouseEvent>
#include <QColorDialog>
#include <QRandomGenerator>
#include <QFrame>
#include <QComboBox>
#include <QStackedWidget>
#include <qobject.h>
#include "Document/Components/ComponentBlueprint.h"
#include "Telemetry/Sources/ComponentIOSource.h"




#pragma region Helpers

// Two-slot icon for the target picker: left = input/output arrow, right =
// "physical" plug (left empty for virtual signals so the names stay aligned).
static QIcon makeTargetIcon(bool isInput, bool physical) {
    const int slot = 16;
    QPixmap pm(slot * 2, slot);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);

    // I/O arrow: green head on the right = input, orange head on the left = output
    QColor ioColor = isInput ? QColor("#4CAF50") : QColor("#FF9800");
    p.setPen(QPen(ioColor, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawLine(QPointF(3, 8), QPointF(12, 8));
    if (isInput) {
        p.drawLine(QPointF(12, 8), QPointF(8, 4));
        p.drawLine(QPointF(12, 8), QPointF(8, 12));
    } else {
        p.drawLine(QPointF(3, 8), QPointF(7, 4));
        p.drawLine(QPointF(3, 8), QPointF(7, 12));
    }

    if (physical) {
        // plug: body + two prongs
        QColor pc("#9E9E9E");
        p.setPen(Qt::NoPen);
        p.setBrush(pc);
        p.drawRoundedRect(QRectF(slot + 4, 6, 8, 6), 1.5, 1.5);
        p.drawRect(QRectF(slot + 5.5, 2.5, 1.6, 3.5));
        p.drawRect(QRectF(slot + 8.9, 2.5, 1.6, 3.5));
        p.drawRect(QRectF(slot + 7.2, 12, 1.6, 2.5));
    }
    return QIcon(pm);
}

static void applyDarkThemeToPlot(QCustomPlot* plot, const QString& yLabel) {
    QColor bgColor("#1e1e1e");
    QColor textColor("#dcdcdc");
    QColor gridColor("#3a3a3a");
    QColor axisColor("#7a7a7a");

    plot->setBackground(QBrush(bgColor));

    plot->xAxis->setLabel("Time (s)");
    plot->yAxis->setLabel(yLabel);

    plot->xAxis->setLabelColor(textColor);
    plot->yAxis->setLabelColor(textColor);
    plot->xAxis->setTickLabelColor(textColor);
    plot->yAxis->setTickLabelColor(textColor);

    QPen axisPen(axisColor, 1);
    plot->xAxis->setBasePen(axisPen);
    plot->xAxis->setTickPen(axisPen);
    plot->xAxis->setSubTickPen(axisPen);
    plot->yAxis->setBasePen(axisPen);
    plot->yAxis->setTickPen(axisPen);
    plot->yAxis->setSubTickPen(axisPen);

    QPen gridPen(gridColor, 1, Qt::SolidLine);
    plot->xAxis->grid()->setPen(gridPen);
    plot->yAxis->grid()->setPen(gridPen);
    plot->xAxis->grid()->setZeroLinePen(QPen(axisColor, 1));
    plot->yAxis->grid()->setZeroLinePen(QPen(axisColor, 1));

    plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
}










// How much history stays in a graph; older points are dropped as new ones arrive.
static constexpr double kHistorySeconds = 30.0;

// A full snapshot can hold points of an earlier run before a clock reset (time
// jumps backwards); only the latest run is plotted.
static size_t latestRunStart(const std::vector<TimeSeriesDataPoint>& points) {
    size_t start = 0;
    for (size_t i = 1; i < points.size(); ++i)
        if (points[i].time < points[i - 1].time) start = i;
    return start;
}

// Fetches what a target has produced since the last poll. The first poll
// (target not in `lastTime`) takes the whole history with snapshotAll();
// every later one only asks for points newer than the last plotted time.
// `reset` is set when the clock restarted and the graph must be cleared first.
// Returns false when there is nothing to add.
static bool pollNewPoints(TelemetrySource* source, QMap<QString, double>& lastTime, const QString& key,
                          std::vector<TimeSeriesDataPoint>& out, size_t& startIdx, bool& reset) {
    reset = false;
    startIdx = 0;
    auto it = lastTime.find(key);
    if (it == lastTime.end()) {
        out = std::any_cast<std::vector<TimeSeriesDataPoint>>(source->snapshotAll());
        startIdx = latestRunStart(out);
        reset = true;
        lastTime.insert(key, out.empty() ? std::numeric_limits<double>::lowest() : out.back().time);
        return !out.empty();
    }

    out = std::any_cast<std::vector<TimeSeriesDataPoint>>(source->snapshotFrom(it.value()));
    if (out.empty()) return false;
    if (out.front().time <= it.value()) {   // buffer handed back everything: clock was reset
        reset = true;
        startIdx = latestRunStart(out);
    }
    it.value() = out.back().time;
    return true;
}

#pragma region CanvasWindow Base

CanvasWindow::CanvasWindow(DataType type, const QString& title, QWidget* parent)
    : QWidget(parent, Qt::Window), dataType(type), title(title) {
    setWindowTitle(title);
    resize(750, 450); // Slightly wider to accommodate the side panel
}












#pragma region ScalarCanvasWindow

ScalarCanvasWindow::ScalarCanvasWindow(const QString& title, QWidget* parent)
    : CanvasWindow(DataType::SCALAR, title, parent) {

    QHBoxLayout* rootLayout = new QHBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);

    customPlot = new QCustomPlot(this);
    applyDarkThemeToPlot(customPlot, "Value");
    rootLayout->addWidget(customPlot, 1);

    QFrame* sidePanel = new QFrame(this);
    sidePanel->setFixedWidth(200);
    sidePanel->setStyleSheet("QFrame { background-color: #252526; border-left: 1px solid #333; }");
    controlsLayout = new QVBoxLayout(sidePanel);
    controlsLayout->setAlignment(Qt::AlignTop);
    controlsLayout->setContentsMargins(8, 8, 8, 8);
    controlsLayout->setSpacing(6);

    QLabel* legendLabel = new QLabel("<b>Sources</b>", sidePanel);
    legendLabel->setStyleSheet("color: #dcdcdc; border: none;");
    controlsLayout->addWidget(legendLabel);

    rootLayout->addWidget(sidePanel, 0);

    updateTimer = new QTimer(this);
    connect(updateTimer, &QTimer::timeout, this, &ScalarCanvasWindow::onUpdateTimer);
    updateTimer->start(33);
}

ScalarCanvasWindow::~ScalarCanvasWindow() {
    updateTimer->stop();
}

QColor ScalarCanvasWindow::targetColor(const QString& sourceKey) const {
    QCPGraph* g = activeGraphs.value(sourceKey, nullptr);
    return g ? g->pen().color() : QColor();
}

void ScalarCanvasWindow::addTarget(QString sourceKey, const QString& label, const QColor& color) {
    if (activeGraphs.contains(sourceKey)) return;

    QCPGraph* newGraph = customPlot->addGraph();
    int hue = QRandomGenerator::global()->bounded(360);
    QColor initialColor = color.isValid() ? color : QColor::fromHsv(hue, 200, 240);
    newGraph->setPen(QPen(initialColor, 2));

    activeGraphs.insert(sourceKey, newGraph);

    QWidget* rowWidget = new QWidget();
    QHBoxLayout* rowLayout = new QHBoxLayout(rowWidget);
    rowLayout->setContentsMargins(0, 0, 0, 0);

    QLabel* nameLabel = new QLabel(label);
    nameLabel->setStyleSheet("color: #dcdcdc; border: none;");

    QPushButton* colorBtn = new QPushButton();
    colorBtn->setFixedSize(20, 20);
    colorBtn->setCursor(Qt::PointingHandCursor);
    colorBtn->setStyleSheet(QString("background-color: %1; border: 1px solid #555; border-radius: 2px;").arg(initialColor.name()));

    connect(colorBtn, &QPushButton::clicked, this, [this, newGraph, colorBtn]() {
        QColor newCol = QColorDialog::getColor(newGraph->pen().color(), this, "Select Graph Color",
                                               QColorDialog::DontUseNativeDialog);
        if (newCol.isValid()) {
            newGraph->setPen(QPen(newCol, 2));
            colorBtn->setStyleSheet(QString("background-color: %1; border: 1px solid #555; border-radius: 2px;").arg(newCol.name()));
            customPlot->replot();
        }
    });

    rowLayout->addWidget(nameLabel);
    rowLayout->addStretch();
    rowLayout->addWidget(colorBtn);

    controlsLayout->addWidget(rowWidget);
    controlRows.insert(sourceKey, rowWidget);
}

void ScalarCanvasWindow::removeTarget(QString sourceKey) {
    if (activeGraphs.contains(sourceKey)) {
        customPlot->removeGraph(activeGraphs[sourceKey]);
        activeGraphs.remove(sourceKey);
        lastTime.remove(sourceKey);
        customPlot->replot();
    }
    if (controlRows.contains(sourceKey)) {
        QWidget* row = controlRows.take(sourceKey);
        row->deleteLater();
    }
}

void ScalarCanvasWindow::onUpdateTimer() {
    auto& registry = TelemetryRegistry::getInstance();
    double latestTime = std::numeric_limits<double>::lowest();
    bool gotData = false;

    for (auto it = activeGraphs.begin(); it != activeGraphs.end(); ++it) {
        auto source = registry.getSource(it.key());
        if (!source) continue;

        std::vector<TimeSeriesDataPoint> points;
        size_t startIdx;
        bool reset;
        if (pollNewPoints(source.get(), lastTime, it.key(), points, startIdx, reset)) {
            QCPGraph* graph = it.value();
            if (reset) graph->data()->clear();

            QVector<double> keys, values;
            keys.reserve(points.size() - startIdx);
            values.reserve(points.size() - startIdx);
            for (size_t i = startIdx; i < points.size(); ++i) {
                keys.append(points[i].time);
                values.append(points[i].values[0]);
            }
            graph->addData(keys, values, true);
            graph->data()->removeBefore(points.back().time - kHistorySeconds);
            gotData = true;
        }
        latestTime = std::max(latestTime, lastTime.value(it.key(), latestTime));
    }

    if (gotData) {
        customPlot->xAxis->setRange(latestTime - 5.0, latestTime + 0.5);
        customPlot->yAxis->rescale();
        customPlot->replot();
    }
}




















#pragma region VectorCanvasWindow

VectorCanvasWindow::VectorCanvasWindow(const QString& title, QWidget* parent)
    : CanvasWindow(DataType::VECTOR, title, parent) {

    QHBoxLayout* rootLayout = new QHBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);

    QWidget* plotArea = new QWidget();
    QVBoxLayout* mainLayout = new QVBoxLayout(plotArea);
    mainLayout->setContentsMargins(8, 8, 8, 8);

    QHBoxLayout* topBar = new QHBoxLayout();
    topBar->addWidget(new QLabel("Plot Variation:"));
    variationCombo = new QComboBox();
    variationCombo->addItem("Decomposed");
    variationCombo->addItem("Decomposed (Separate)");
    variationCombo->addItem("Axis");
    topBar->addWidget(variationCombo);
    topBar->addStretch();
    mainLayout->addLayout(topBar);

    stackedWidget = new QStackedWidget(this);
    mainLayout->addWidget(stackedWidget);

    combinedPlot = new QCustomPlot();
    applyDarkThemeToPlot(combinedPlot, "Value");
    stackedWidget->addWidget(combinedPlot);

    QWidget* sepWidget = new QWidget();
    QVBoxLayout* sepLayout = new QVBoxLayout(sepWidget);
    sepLayout->setContentsMargins(0, 0, 0, 0);
    xPlot = new QCustomPlot(); applyDarkThemeToPlot(xPlot, "X Value");
    yPlot = new QCustomPlot(); applyDarkThemeToPlot(yPlot, "Y Value");
    zPlot = new QCustomPlot(); applyDarkThemeToPlot(zPlot, "Z Value");
    sepLayout->addWidget(xPlot);
    sepLayout->addWidget(yPlot);
    sepLayout->addWidget(zPlot);
    stackedWidget->addWidget(sepWidget);

    QWidget* axisWidget = new QWidget();
    stackedWidget->addWidget(axisWidget);

    connect(variationCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            stackedWidget, &QStackedWidget::setCurrentIndex);

    rootLayout->addWidget(plotArea, 1);

    QFrame* sidePanel = new QFrame(this);
    sidePanel->setFixedWidth(200);
    sidePanel->setStyleSheet("QFrame { background-color: #252526; border-left: 1px solid #333; }");
    controlsLayout = new QVBoxLayout(sidePanel);
    controlsLayout->setAlignment(Qt::AlignTop);
    controlsLayout->setContentsMargins(8, 8, 8, 8);
    controlsLayout->setSpacing(6);

    QLabel* legendLabel = new QLabel("<b>Sources</b>", sidePanel);
    legendLabel->setStyleSheet("color: #dcdcdc; border: none;");
    controlsLayout->addWidget(legendLabel);

    rootLayout->addWidget(sidePanel, 0);

    updateTimer = new QTimer(this);
    connect(updateTimer, &QTimer::timeout, this, &VectorCanvasWindow::onUpdateTimer);
    updateTimer->start(33);
}

VectorCanvasWindow::~VectorCanvasWindow() {
    updateTimer->stop();
}

QColor VectorCanvasWindow::targetColor(const QString& sourceKey) const {
    auto it = activeGraphs.constFind(sourceKey);
    return it == activeGraphs.constEnd() ? QColor() : it->sepX->pen().color();
}

void VectorCanvasWindow::addTarget(QString sourceKey, const QString& label, const QColor& color) {
    if (activeGraphs.contains(sourceKey)) return;

    VectorGraphs vg;
    int hue = QRandomGenerator::global()->bounded(360);
    QColor baseColor = color.isValid() ? color : QColor::fromHsv(hue, 200, 240);

    vg.combinedX = combinedPlot->addGraph();
    vg.combinedX->setPen(QPen(baseColor, 2, Qt::SolidLine));
    vg.combinedY = combinedPlot->addGraph();
    vg.combinedY->setPen(QPen(baseColor, 2, Qt::DashLine));
    vg.combinedZ = combinedPlot->addGraph();
    vg.combinedZ->setPen(QPen(baseColor, 2, Qt::DotLine));

    vg.sepX = xPlot->addGraph();
    vg.sepX->setPen(QPen(baseColor, 2));
    vg.sepY = yPlot->addGraph();
    vg.sepY->setPen(QPen(baseColor, 2));
    vg.sepZ = zPlot->addGraph();
    vg.sepZ->setPen(QPen(baseColor, 2));

    activeGraphs.insert(sourceKey, vg);

    QWidget* rowWidget = new QWidget();
    QHBoxLayout* rowLayout = new QHBoxLayout(rowWidget);
    rowLayout->setContentsMargins(0, 0, 0, 0);

    QLabel* nameLabel = new QLabel(label);
    nameLabel->setStyleSheet("color: #dcdcdc; border: none;");

    QPushButton* colorBtn = new QPushButton();
    colorBtn->setFixedSize(20, 20);
    colorBtn->setCursor(Qt::PointingHandCursor);
    colorBtn->setStyleSheet(QString("background-color: %1; border: 1px solid #555; border-radius: 2px;").arg(baseColor.name()));

    connect(colorBtn, &QPushButton::clicked, this, [this, sourceKey, colorBtn]() {
        if (!activeGraphs.contains(sourceKey)) return;

        QColor newCol = QColorDialog::getColor(activeGraphs[sourceKey].sepX->pen().color(), this, "Select Graph Color",
                                               QColorDialog::DontUseNativeDialog);
        if (newCol.isValid()) {
            VectorGraphs& g = activeGraphs[sourceKey];
            g.combinedX->setPen(QPen(newCol, 2, Qt::SolidLine));
            g.combinedY->setPen(QPen(newCol, 2, Qt::DashLine));
            g.combinedZ->setPen(QPen(newCol, 2, Qt::DotLine));
            g.sepX->setPen(QPen(newCol, 2));
            g.sepY->setPen(QPen(newCol, 2));
            g.sepZ->setPen(QPen(newCol, 2));

            colorBtn->setStyleSheet(QString("background-color: %1; border: 1px solid #555; border-radius: 2px;").arg(newCol.name()));

            combinedPlot->replot();
            xPlot->replot();
            yPlot->replot();
            zPlot->replot();
        }
    });

    rowLayout->addWidget(nameLabel);
    rowLayout->addStretch();
    rowLayout->addWidget(colorBtn);

    controlsLayout->addWidget(rowWidget);
    controlRows.insert(sourceKey, rowWidget);
}

void VectorCanvasWindow::removeTarget(QString sourceKey) {
    if (activeGraphs.contains(sourceKey)) {
        VectorGraphs vg = activeGraphs[sourceKey];
        combinedPlot->removeGraph(vg.combinedX);
        combinedPlot->removeGraph(vg.combinedY);
        combinedPlot->removeGraph(vg.combinedZ);
        xPlot->removeGraph(vg.sepX);
        yPlot->removeGraph(vg.sepY);
        zPlot->removeGraph(vg.sepZ);
        activeGraphs.remove(sourceKey);
        lastTime.remove(sourceKey);

        combinedPlot->replot();
        xPlot->replot();
        yPlot->replot();
        zPlot->replot();
    }
    if (controlRows.contains(sourceKey)) {
        QWidget* row = controlRows.take(sourceKey);
        row->deleteLater();
    }
}

void VectorCanvasWindow::onUpdateTimer() {
    auto& registry = TelemetryRegistry::getInstance();
    double latestTime = std::numeric_limits<double>::lowest();
    bool gotData = false;

    for (auto it = activeGraphs.begin(); it != activeGraphs.end(); ++it) {
        auto source = registry.getSource(it.key());
        if (!source) continue;

        std::vector<TimeSeriesDataPoint> points;
        size_t startIdx;
        bool reset;
        if (pollNewPoints(source.get(), lastTime, it.key(), points, startIdx, reset)) {
            VectorGraphs& vg = it.value();
            QCPGraph* all[6] = {vg.combinedX, vg.combinedY, vg.combinedZ, vg.sepX, vg.sepY, vg.sepZ};
            if (reset) for (QCPGraph* g : all) g->data()->clear();

            QVector<double> keys, xVals, yVals, zVals;
            const int count = static_cast<int>(points.size() - startIdx);
            keys.reserve(count);
            xVals.reserve(count);
            yVals.reserve(count);
            zVals.reserve(count);
            for (size_t i = startIdx; i < points.size(); ++i) {
                keys.append(points[i].time);
                xVals.append(points[i].values.size() > 0 ? points[i].values[0] : 0.0);
                yVals.append(points[i].values.size() > 1 ? points[i].values[1] : 0.0);
                zVals.append(points[i].values.size() > 2 ? points[i].values[2] : 0.0);
            }
            vg.combinedX->addData(keys, xVals, true);
            vg.combinedY->addData(keys, yVals, true);
            vg.combinedZ->addData(keys, zVals, true);
            vg.sepX->addData(keys, xVals, true);
            vg.sepY->addData(keys, yVals, true);
            vg.sepZ->addData(keys, zVals, true);
            for (QCPGraph* g : all) g->data()->removeBefore(points.back().time - kHistorySeconds);
            gotData = true;
        }
        latestTime = std::max(latestTime, lastTime.value(it.key(), latestTime));
    }

    if (gotData) {
        int mode = variationCombo->currentIndex();
        if (mode == 0) {
            combinedPlot->xAxis->setRange(latestTime - 5.0, latestTime + 0.5);
            combinedPlot->yAxis->rescale();
            combinedPlot->replot();
        } else if (mode == 1) {
            for (QCustomPlot* plot : {xPlot, yPlot, zPlot}) {
                plot->xAxis->setRange(latestTime - 5.0, latestTime + 0.5);
                plot->yAxis->rescale();
                plot->replot();
            }
        }
    }
}


#pragma region ImageCanvasWindow

ImageView::ImageView(QWidget* parent) : QWidget(parent) {
    setMinimumSize(160, 120);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void ImageView::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter p(this);
    p.fillRect(rect(), QColor("#111111"));
    if (image.isNull()) {
        p.setPen(QColor("#7a7a7a"));
        p.drawText(rect(), Qt::AlignCenter, "No frame yet");
        return;
    }
    QSize target = image.size().scaled(size(), Qt::KeepAspectRatio);
    QRect r(QPoint((width() - target.width()) / 2, (height() - target.height()) / 2), target);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    p.drawImage(r, image);
}

ImageCanvasWindow::ImageCanvasWindow(const QString& title, QWidget* parent)
    : CanvasWindow(DataType::IMAGE, title, parent) {
    setStyleSheet("background-color: #1e1e1e;");
    tilesLayout = new QHBoxLayout(this);
    tilesLayout->setContentsMargins(8, 8, 8, 8);
    tilesLayout->setSpacing(8);

    updateTimer = new QTimer(this);
    connect(updateTimer, &QTimer::timeout, this, &ImageCanvasWindow::onUpdateTimer);
    updateTimer->start(33);
}

ImageCanvasWindow::~ImageCanvasWindow() {
    updateTimer->stop();
}

void ImageCanvasWindow::addTarget(QString sourceKey, const QString& label, const QColor& color) {
    Q_UNUSED(color);
    if (tiles.contains(sourceKey)) return;

    ImageTile tile;
    tile.container = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(tile.container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    QLabel* title = new QLabel(label, tile.container);
    title->setStyleSheet("color: #dcdcdc; font-weight: bold;");
    tile.view = new ImageView(tile.container);
    tile.info = new QLabel("waiting for frames", tile.container);
    tile.info->setStyleSheet("color: #9a9a9a;");

    layout->addWidget(title);
    layout->addWidget(tile.view, 1);
    layout->addWidget(tile.info);

    tilesLayout->addWidget(tile.container, 1);
    tiles.insert(sourceKey, tile);
}

void ImageCanvasWindow::removeTarget(QString sourceKey) {
    auto it = tiles.find(sourceKey);
    if (it == tiles.end()) return;
    it->container->deleteLater();
    tiles.erase(it);
}

void ImageCanvasWindow::onUpdateTimer() {
    if (!isVisible()) return;
    auto& registry = TelemetryRegistry::getInstance();
    for (auto it = tiles.begin(); it != tiles.end(); ++it) {
        auto source = registry.getSource(it.key());
        if (!source) continue;
        ImageIOValue frame = std::any_cast<ImageIOValue>(source->snapshotAll());

        // Only convert when a new frame arrived; seq 0 = cleared (sim reset).
        if (!frame.pixels) {
            if (it->shown) {
                it->view->setImage(QImage());
                it->info->setText("waiting for frames");
                it->shown = false;
                it->lastSeq = 0;
            }
            continue;
        }
        if (it->shown && frame.seq == it->lastSeq) continue;
        it->lastSeq = frame.seq;
        it->shown = true;
        it->view->setImage(ImageBuffer::toQImage(frame));
        it->info->setText(QString("%1x%2 %3 · frame %4 · t=%5 s")
                              .arg(frame.width).arg(frame.height)
                              .arg(frame.format == ImageFormat::Depth32F ? "depth" : "rgb")
                              .arg(frame.seq)
                              .arg(frame.time, 0, 'f', 3));
    }
}




#pragma region CanvasDockItem (The Card)

CanvasDockItem::CanvasDockItem(DataType type, const QString& name, QWidget* parent)
    : QWidget(parent), dataType(type), canvasName(name) {

    QVBoxLayout* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);

    QFrame* mainFrame = new QFrame(this);
    mainFrame->setStyleSheet("QFrame { background-color: rgba(128, 128, 128, 0.05); border: 1px solid rgba(128, 128, 128, 0.2); border-radius: 4px; }");

    QVBoxLayout* mainLayout = new QVBoxLayout(mainFrame);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(6);
    outerLayout->addWidget(mainFrame);

    QHBoxLayout* headerLayout = new QHBoxLayout();
    QLabel* iconLabel = new QLabel();
    if (type == DataType::SCALAR) iconLabel->setText("📈");
    else if (type == DataType::VECTOR) iconLabel->setText("📐");
    else if (type == DataType::IMAGE) iconLabel->setText("📷");
    iconLabel->setStyleSheet("border: none; font-size: 18px;");

    QLabel* nameLabel = new QLabel("<b>" + name + "</b>");
    nameLabel->setStyleSheet("border: none;");

    headerLayout->addWidget(iconLabel);
    headerLayout->addSpacing(4);
    headerLayout->addWidget(nameLabel);
    headerLayout->addStretch();
    mainLayout->addLayout(headerLayout);

    QWidget* targetsContainer = new QWidget();
    targetsContainer->setStyleSheet("background-color: transparent; border: none;");
    targetsLayout = new QVBoxLayout(targetsContainer);
    targetsLayout->setContentsMargins(28, 0, 0, 0);
    targetsLayout->setSpacing(2);
    mainLayout->addWidget(targetsContainer);

    QHBoxLayout* actionsLayout = new QHBoxLayout();

    QPushButton* addTargetBtn = new QPushButton("+ Add Target");
    QPushButton* viewBtn = new QPushButton("View");
    toggleBtn = new QPushButton("Disable");
    QPushButton* deleteBtn = new QPushButton("✖");
    deleteBtn->setFixedWidth(30);

    actionsLayout->addWidget(addTargetBtn);
    actionsLayout->addStretch();
    actionsLayout->addWidget(viewBtn);
    actionsLayout->addWidget(toggleBtn);
    actionsLayout->addWidget(deleteBtn);

    mainLayout->addLayout(actionsLayout);

    if (type == DataType::SCALAR) popUpWindow = new ScalarCanvasWindow(name, nullptr);
    else if (type == DataType::IMAGE) popUpWindow = new ImageCanvasWindow(name, nullptr);
    else popUpWindow = new VectorCanvasWindow(name, nullptr);

    connect(addTargetBtn, &QPushButton::clicked, this, [this]() {
        showAddTargetDialog();
    });

    connect(viewBtn, &QPushButton::clicked, this, [this]() {
        if (popUpWindow) {
            popUpWindow->show();
            popUpWindow->raise();
            popUpWindow->activateWindow();
        }
    });

    connect(toggleBtn, &QPushButton::clicked, this, [this]() {
        setCapturing(!enabled);
    });

    connect(deleteBtn, &QPushButton::clicked, this, [this]() {
        closeCanvas();
    });
}

void CanvasDockItem::setCapturing(bool on) {
    if (enabled == on) return;
    enabled = on;
    toggleBtn->setText(on ? "Disable" : "Enable");

    auto& registry = TelemetryRegistry::getInstance();
    for (const QString& sourceKey : subscribedSources) {
        if (auto ch = registry.getSource(sourceKey)) {
            if (on) ch->subscribe();
            else ch->unsubscribe();
        }
    }
}

void CanvasDockItem::closeCanvas() {
    if (removed) return;
    removed = true;

    if (enabled) {
        auto& registry = TelemetryRegistry::getInstance();
        for (const QString& sourceKey : subscribedSources) {
            if (auto ch = registry.getSource(sourceKey))
                ch->unsubscribe();
        }
    }
    subscribedSources.clear();
    enabled = false;   // the destructor must not unsubscribe a second time

    // The destructor deletes the pop-up; deleting it here as well left
    // the destructor calling deleteLater() on a dangling pointer.
    if (popUpWindow) popUpWindow->close();

    hide();
    this->deleteLater();
}

CanvasDockItem::~CanvasDockItem() {
    if (enabled) {
        auto& registry = TelemetryRegistry::getInstance();
        for (QString sourceKey : subscribedSources) {
            if (auto ch = registry.getSource(sourceKey))
                ch->unsubscribe();
        }
    }
    if (popUpWindow) popUpWindow->deleteLater();
}

void CanvasDockItem::mouseReleaseEvent(QMouseEvent* event) {
    QWidget::mouseReleaseEvent(event);
}

void CanvasDockItem::showAddTargetDialog() {
    QDialog dialog(this);
    dialog.setWindowTitle("Select Channel Target");
    dialog.setMinimumSize(420, 400);

    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    QTreeWidget* tree = new QTreeWidget();
    tree->setHeaderHidden(true);
    tree->setIconSize(QSize(32, 16));
    tree->setIndentation(18);
    tree->setUniformRowHeights(true);
    layout->addWidget(tree);

    auto& registry = TelemetryRegistry::getInstance();

    const SignalShape wantedShape = dataType == DataType::SCALAR ? SignalShape::Scalar
                                  : dataType == DataType::VECTOR ? SignalShape::Vector
                                  : SignalShape::Image;

    QMap<int, ComponentInstance*> compMap = Application::getInstance()->getProject()->getComponentMap();
    for (ComponentInstance* comp : compMap) {
        const int uid = comp->uid;
        ComponentBlueprint* bp = comp->getBlueprint();
        if (!bp) continue;

        QTreeWidgetItem* parentItem = new QTreeWidgetItem();
        parentItem->setText(0, QString("%1 (%2)").arg(comp->name).arg(uid));
        parentItem->setFlags(Qt::ItemIsEnabled);   // header only, not selectable
        QFont f = parentItem->font(0);
        f.setBold(true);
        parentItem->setFont(0, f);

        auto addGroup = [&](const QMap<QString, InterfaceDef>& defs, bool isInput) {
            for (auto it = defs.constBegin(); it != defs.constEnd(); ++it) {
                const QString key = it.key();
                if (bp->interfaceShape(it.value()) != wantedShape) continue;   // canvas only takes its own data type
                if (wantedShape != SignalShape::Image && bp->interfaceDim(it.value()) <= 0) continue;   // unresolved target
                if (subscribedSources.contains(QString("io:%1:%2").arg(uid).arg(key))) continue;

                QTreeWidgetItem* item = new QTreeWidgetItem(parentItem);
                item->setText(0, key);
                item->setIcon(0, makeTargetIcon(isInput, it.value().physical));
                item->setData(0, Qt::UserRole, uid);
                item->setData(0, Qt::UserRole + 1, key);
                item->setToolTip(0, QString("%1 %2%3%4")
                    .arg(isInput ? "Input" : "Output")
                    .arg(it.value().physical ? "(physical)" : "(virtual)")
                    .arg(it.value().unit.isEmpty() ? "" : " [")
                    .arg(it.value().unit.isEmpty() ? "" : it.value().unit + "]"));
            }
        };
        addGroup(bp->interfaceInputs, true);
        addGroup(bp->interfaceOutputs, false);

        if (parentItem->childCount() == 0) { delete parentItem; continue; }
        tree->addTopLevelItem(parentItem);
        parentItem->setExpanded(true);
    }

    if (tree->topLevelItemCount() == 0) {
        QLabel* empty = new QLabel("No available targets of this canvas's data type.");
        empty->setAlignment(Qt::AlignCenter);
        layout->insertWidget(0, empty);
    }

    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttonBox);
    QPushButton* okBtn = buttonBox->button(QDialogButtonBox::Ok);
    auto isTarget = [](QTreeWidgetItem* i) { return i && i->parent(); };
    okBtn->setEnabled(false);
    connect(tree, &QTreeWidget::currentItemChanged, &dialog, [okBtn, isTarget](QTreeWidgetItem* cur) {
        okBtn->setEnabled(isTarget(cur));
    });
    connect(tree, &QTreeWidget::itemDoubleClicked, &dialog, [&dialog, isTarget](QTreeWidgetItem* item) {
        if (isTarget(item)) dialog.accept();
    });
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted && isTarget(tree->currentItem())) {
        QTreeWidgetItem* cur = tree->currentItem();
        attachIoTarget(cur->data(0, Qt::UserRole).toInt(), cur->data(0, Qt::UserRole + 1).toString());
        // registry.clearInactive(); [TODO] important
    }
}




bool CanvasDockItem::attachIoTarget(int uid, const QString& signal, const QColor& color) {
    Project* project = Application::getInstance()->getProject();
    ComponentInstance* comp = project->getComponentByUid(uid);
    ComponentBlueprint* bp = comp ? comp->getBlueprint() : nullptr;
    if (!bp) return false;

    const bool isInput = bp->interfaceInputs.contains(signal);
    if (!isInput && !bp->interfaceOutputs.contains(signal)) return false;

    const QString key = QString("io:%1:%2").arg(uid).arg(signal);
    if (subscribedSources.contains(key)) return false;

    auto source = TelemetryRegistry::getInstance().getOrCreateSource(
        key,
        [comp, signal, isInput]() {
            return std::make_shared<ComponentIOSource>(comp, signal, isInput);
        }
    );
    subscribedSources.append(key);
    if (enabled) source->subscribe();

    const QString label = QString("%1.%2").arg(comp->name).arg(signal);
    popUpWindow->addTarget(key, label, color);
    addTargetUI(key, label);
    return true;
}




QJsonObject CanvasDockItem::toJson() const {
    QJsonObject obj;
    obj["name"] = canvasName;
    obj["type"] = dataType == DataType::SCALAR ? "scalar" : dataType == DataType::VECTOR ? "vector" : "image";
    obj["enabled"] = enabled;

    QJsonArray targets;
    for (const QString& key : subscribedSources) {
        // key = "io:<uid>:<signal>"; the signal is everything after the second colon
        QJsonObject t;
        t["uid"] = key.section(':', 1, 1).toInt();
        t["signal"] = key.section(':', 2);
        if (popUpWindow) {
            QColor c = popUpWindow->targetColor(key);
            if (c.isValid()) t["color"] = c.name();
        }
        targets.append(t);
    }
    obj["targets"] = targets;
    return obj;
}




void CanvasDockItem::applyTargetsJson(const QJsonObject& obj) {
    // Attach while enabled so subscriptions are taken the normal way, then apply the saved state.
    for (const QJsonValue& v : obj["targets"].toArray()) {
        QJsonObject t = v.toObject();
        QColor color(t["color"].toString());
        if (!attachIoTarget(t["uid"].toInt(), t["signal"].toString(), color))
            Log::warning(QString("Plotting: skipped target '%1' of component %2 (no longer exists)")
                             .arg(t["signal"].toString()).arg(t["uid"].toInt()));
    }
    setCapturing(obj["enabled"].toBool(true));
}




void CanvasDockItem::addTargetUI(QString sourceKey, const QString& label) {
    QWidget* row = new QWidget();
    row->setStyleSheet("border: none;");
    QHBoxLayout* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 2, 0, 2);

    QLabel* textLabel = new QLabel("↳ " + label);

    QPushButton* removeBtn = new QPushButton("✖");
    removeBtn->setFixedSize(24, 24);
    removeBtn->setStyleSheet("color: #E53935; border: none; font-weight: bold; font-size: 14px; background: transparent;");

    rowLayout->addWidget(textLabel);
    rowLayout->addStretch();
    rowLayout->addWidget(removeBtn);

    targetsLayout->addWidget(row);

    connect(removeBtn, &QPushButton::clicked, this, [this, row, sourceKey]() {
        subscribedSources.removeOne(sourceKey);
        popUpWindow->removeTarget(sourceKey);

        if (enabled) {
            if (auto ch = TelemetryRegistry::getInstance().getSource(sourceKey)) ch->unsubscribe();
        }

        row->deleteLater();
    });
}
