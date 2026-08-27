// src/UI/Docking/Canvas.cpp
#include "Canvas.h"

#include <QDialog>
#include <QListWidget>
#include <QDialogButtonBox>
#include <QMouseEvent>
#include <QColorDialog>
#include <QRandomGenerator>
#include <QFrame>
#include <QComboBox>
#include <QStackedWidget>

#pragma region Helpers

/*
 * Helper function to apply the global dark theme to QCustomPlot instances.
 * Configures background colors, grid lines, and axis label colors.
 */





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

#pragma region CanvasWindow Base

/*
 * Base constructor for all canvas window types.
 * Initializes the default window properties and native styling.
 */





CanvasWindow::CanvasWindow(DataType type, const QString& title, QWidget* parent) 
    : QWidget(parent, Qt::Window), dataType(type), title(title) {
    setWindowTitle(title);
    resize(600, 400);
}

#pragma region ScalarCanvasWindow

/*
 * Initializes the ScalarCanvasWindow UI layout and update timer.
 * Sets up a single QCustomPlot for 1D time series tracking.
 */





ScalarCanvasWindow::ScalarCanvasWindow(const QString& title, QWidget* parent)
    : CanvasWindow(DataType::SCALAR, title, parent) {
    
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    customPlot = new QCustomPlot(this);
    applyDarkThemeToPlot(customPlot, "Value");
    layout->addWidget(customPlot);

    this->setProperty("lastSeenTime", -1.0);

    updateTimer = new QTimer(this);
    connect(updateTimer, &QTimer::timeout, this, &ScalarCanvasWindow::onUpdateTimer);
    updateTimer->start(16); 
}





/*
 * Cleans up the update timer upon window destruction.
 */





ScalarCanvasWindow::~ScalarCanvasWindow() {
    updateTimer->stop();
}





/*
 * Adds a new target graph to the plot with a randomized hue.
 * Registers the channel ID for active polling.
 */





void ScalarCanvasWindow::addTarget(int channelId) {
    if (activeGraphs.contains(channelId)) return;

    QCPGraph* newGraph = customPlot->addGraph();
    int hue = QRandomGenerator::global()->bounded(360);
    newGraph->setPen(QPen(QColor::fromHsv(hue, 200, 240), 2));
    
    activeGraphs.insert(channelId, newGraph);
}





/*
 * Removes an existing target graph from the plot and clears its data.
 * Unregisters the channel ID from active polling.
 */





void ScalarCanvasWindow::removeTarget(int channelId) {
    if (activeGraphs.contains(channelId)) {
        customPlot->removeGraph(activeGraphs[channelId]);
        activeGraphs.remove(channelId);
        customPlot->replot();
    }
}





/*
 * High-speed polling loop for fetching data from the TelemetryRegistry.
 * Intelligently scales axes and handles simulation resets.
 */





void ScalarCanvasWindow::onUpdateTimer() {
    auto& registry = TelemetryRegistry::getInstance();
    double latestTime = 0;
    bool hasNewData = false;

    for (auto it = activeGraphs.begin(); it != activeGraphs.end(); ++it) {
        int id = it.key();
        auto channel = registry.getScalar(id);
        if (!channel) continue;

        std::vector<ScalarPoint> points = channel->snapshot();
        if (points.empty()) continue;

        size_t validStartIndex = 0;
        for (size_t i = 1; i < points.size(); ++i) {
            if (points[i].time < points[i-1].time) {
                validStartIndex = i; 
            }
        }

        QVector<double> keys, values;
        keys.reserve(points.size() - validStartIndex);
        values.reserve(points.size() - validStartIndex);
        
        for (size_t i = validStartIndex; i < points.size(); ++i) {
            keys.append(points[i].time);
            values.append(points[i].value);
            if (points[i].time > latestTime) latestTime = points[i].time;
        }

        it.value()->setData(keys, values);
    }

    double lastSeenTime = this->property("lastSeenTime").toDouble();

    if (latestTime < lastSeenTime) {
        lastSeenTime = -1.0;
    }

    if (latestTime > lastSeenTime) {
        hasNewData = true;
        this->setProperty("lastSeenTime", latestTime);
    }

    if (hasNewData) {
        customPlot->xAxis->setRange(latestTime - 5.0, latestTime + 0.5); 
        customPlot->yAxis->rescale();
        customPlot->replot();
    }
}


#pragma region VectorCanvasWindow 

/*
 * Initializes the VectorCanvasWindow UI layout and interaction modes.
 * Sets up a dropdown for plot variation and a stacked widget for different views.
 */





VectorCanvasWindow::VectorCanvasWindow(const QString& title, QWidget* parent)
    : CanvasWindow(DataType::VECTOR, title, parent) {
    
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
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

    this->setProperty("lastSeenTime", -1.0);

    updateTimer = new QTimer(this);
    connect(updateTimer, &QTimer::timeout, this, &VectorCanvasWindow::onUpdateTimer);
    updateTimer->start(16);
}





/*
 * Cleans up the update timer upon window destruction.
 */





VectorCanvasWindow::~VectorCanvasWindow() {
    updateTimer->stop();
}





/*
 * Adds a new multi-dimensional target graph to all internal plots.
 * Configures distinct line styles for X, Y, and Z combined axes.
 */





void VectorCanvasWindow::addTarget(int channelId) {
    if (activeGraphs.contains(channelId)) return;

    VectorGraphs vg;
    int hue = QRandomGenerator::global()->bounded(360);
    QColor baseColor = QColor::fromHsv(hue, 200, 240);

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

    activeGraphs.insert(channelId, vg);
}





/*
 * Removes all representations of a multi-dimensional target graph.
 * Unregisters the channel ID from active polling.
 */





void VectorCanvasWindow::removeTarget(int channelId) {
    if (activeGraphs.contains(channelId)) {
        VectorGraphs vg = activeGraphs[channelId];
        combinedPlot->removeGraph(vg.combinedX);
        combinedPlot->removeGraph(vg.combinedY);
        combinedPlot->removeGraph(vg.combinedZ);
        xPlot->removeGraph(vg.sepX);
        yPlot->removeGraph(vg.sepY);
        zPlot->removeGraph(vg.sepZ);
        activeGraphs.remove(channelId);

        combinedPlot->replot();
        xPlot->replot();
        yPlot->replot();
        zPlot->replot();
    }
}





/*
 * High-speed polling loop for fetching 3D vector data from the TelemetryRegistry.
 * Updates both combined and separated plot views concurrently.
 */





void VectorCanvasWindow::onUpdateTimer() {
    auto& registry = TelemetryRegistry::getInstance();
    double latestTime = 0;
    bool hasNewData = false;

    for (auto it = activeGraphs.begin(); it != activeGraphs.end(); ++it) {
        int id = it.key();
        auto channel = registry.getVector(id);
        if (!channel) continue;

        std::vector<VectorPoint> points = channel->snapshot();
        if (points.empty()) continue;

        size_t validStartIndex = 0;
        for (size_t i = 1; i < points.size(); ++i) {
            if (points[i].time < points[i-1].time) validStartIndex = i;
        }

        QVector<double> keys, xVals, yVals, zVals;
        int count = points.size() - validStartIndex;
        keys.reserve(count); 
        xVals.reserve(count); 
        yVals.reserve(count); 
        zVals.reserve(count);

        for (size_t i = validStartIndex; i < points.size(); ++i) {
            keys.append(points[i].time);
            xVals.append(points[i].value.size() > 0 ? points[i].value[0] : 0.0);
            yVals.append(points[i].value.size() > 1 ? points[i].value[1] : 0.0);
            zVals.append(points[i].value.size() > 2 ? points[i].value[2] : 0.0);
            if (points[i].time > latestTime) latestTime = points[i].time;
        }

        VectorGraphs& vg = it.value();
        vg.combinedX->setData(keys, xVals);
        vg.combinedY->setData(keys, yVals);
        vg.combinedZ->setData(keys, zVals);
        vg.sepX->setData(keys, xVals);
        vg.sepY->setData(keys, yVals);
        vg.sepZ->setData(keys, zVals);
    }

    double lastSeenTime = this->property("lastSeenTime").toDouble();
    if (latestTime < lastSeenTime) lastSeenTime = -1.0;

    if (latestTime > lastSeenTime) {
        hasNewData = true;
        this->setProperty("lastSeenTime", latestTime);
    }

    if (hasNewData) {
        int mode = variationCombo->currentIndex();
        if (mode == 0) {
            combinedPlot->xAxis->setRange(latestTime - 5.0, latestTime + 0.5);
            combinedPlot->yAxis->rescale();
            combinedPlot->replot();
        } else if (mode == 1) {
            xPlot->xAxis->setRange(latestTime - 5.0, latestTime + 0.5); 
            xPlot->yAxis->rescale(); 
            xPlot->replot();
            
            yPlot->xAxis->setRange(latestTime - 5.0, latestTime + 0.5); 
            yPlot->yAxis->rescale(); 
            yPlot->replot();
            
            zPlot->xAxis->setRange(latestTime - 5.0, latestTime + 0.5); 
            zPlot->yAxis->rescale(); 
            zPlot->replot();
        }
    }
}


#pragma region CanvasDockItem (The Card)

/*
 * Constructor for the UI card inside the PlotPanel dock.
 * Sets up distinct styling and interactive logic for managing canvas states.
 */





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
    QPushButton* toggleBtn = new QPushButton("Disable");
    QPushButton* deleteBtn = new QPushButton("✖");
    deleteBtn->setFixedWidth(30); 

    actionsLayout->addWidget(addTargetBtn);
    actionsLayout->addStretch(); 
    actionsLayout->addWidget(viewBtn);
    actionsLayout->addWidget(toggleBtn);
    actionsLayout->addWidget(deleteBtn);
    
    mainLayout->addLayout(actionsLayout);

    if (type == DataType::SCALAR) popUpWindow = new ScalarCanvasWindow(name, nullptr);
    else popUpWindow = new VectorCanvasWindow(name, nullptr); 

    connect(addTargetBtn, &QPushButton::clicked, this, [this, toggleBtn]() {
        bool isEnabled = (toggleBtn->text() == "Disable");
        this->setProperty("currentlyEnabled", isEnabled);
        showAddTargetDialog();
    });
    
    connect(viewBtn, &QPushButton::clicked, this, [this]() {
        if (popUpWindow) {
            popUpWindow->show();
            popUpWindow->raise();
            popUpWindow->activateWindow();
        }
    });

    connect(toggleBtn, &QPushButton::clicked, this, [this, toggleBtn]() {
        bool isEnabled = (toggleBtn->text() == "Disable");
        isEnabled = !isEnabled; 
        toggleBtn->setText(isEnabled ? "Disable" : "Enable");

        auto& registry = TelemetryRegistry::getInstance();
        for (int id : subscribedChannels) {
            if (dataType == DataType::SCALAR) {
                if (auto ch = registry.getScalar(id)) {
                    if (isEnabled) ch->subscribe();
                    else ch->unsubscribe();
                }
            } else if (dataType == DataType::VECTOR) {
                if (auto ch = registry.getVector(id)) {
                    if (isEnabled) ch->subscribe();
                    else ch->unsubscribe();
                }
            }
        }
    });

    connect(deleteBtn, &QPushButton::clicked, this, [this]() {
        auto& registry = TelemetryRegistry::getInstance();
        for (int id : subscribedChannels) {
            if (dataType == DataType::SCALAR) {
                if (auto ch = registry.getScalar(id)) ch->unsubscribe();
            } else if (dataType == DataType::VECTOR) {
                if (auto ch = registry.getVector(id)) ch->unsubscribe();
            }
        }
        subscribedChannels.clear();
        
        if (popUpWindow) {
            popUpWindow->close();
            popUpWindow->deleteLater();
        }
        
        this->deleteLater();
    });
}





/*
 * Cleans up registry subscriptions and sub-windows upon deletion.
 */





CanvasDockItem::~CanvasDockItem() {
    auto& registry = TelemetryRegistry::getInstance();
    for (int id : subscribedChannels) {
        if (dataType == DataType::SCALAR) {
            if (auto ch = registry.getScalar(id)) ch->unsubscribe();
        } else if (dataType == DataType::VECTOR) {
            if (auto ch = registry.getVector(id)) ch->unsubscribe();
        }
    }
    if (popUpWindow) popUpWindow->deleteLater();
}





/*
 * Absorbs the click event without popping up the window.
 */





void CanvasDockItem::mouseReleaseEvent(QMouseEvent* event) {
    QWidget::mouseReleaseEvent(event); 
}





/*
 * Shows a dialog to select data channels that match the canvas data type.
 */





void CanvasDockItem::showAddTargetDialog() {
    QDialog dialog(this);
    dialog.setWindowTitle("Select Channel Target");
    dialog.setMinimumSize(400, 300);

    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    QListWidget* listWidget = new QListWidget();
    layout->addWidget(listWidget);

    auto& registry = TelemetryRegistry::getInstance();
    QList<int> availableIds = registry.getChannelsOfType(dataType);

    for (int id : availableIds) {
        if (subscribedChannels.contains(id)) continue;

        QString label = "Unknown";
        if (dataType == DataType::SCALAR) {
            if (auto ch = registry.getScalar(id)) label = ch->getMeta().name;
        } else if (dataType == DataType::VECTOR) {
            if (auto ch = registry.getVector(id)) label = ch->getMeta().name;
        }

        QListWidgetItem* item = new QListWidgetItem(label);
        item->setData(Qt::UserRole, id);
        listWidget->addItem(item);
    }

    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttonBox);
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted && listWidget->currentItem()) {
        int channelId = listWidget->currentItem()->data(Qt::UserRole).toInt();
        QString label = listWidget->currentItem()->text();
        
        subscribedChannels.append(channelId);
        
        bool isEnabled = this->property("currentlyEnabled").toBool();
        if (isEnabled) {
            if (dataType == DataType::SCALAR) {
                if (auto ch = registry.getScalar(channelId)) ch->subscribe();
            } else if (dataType == DataType::VECTOR) {
                if (auto ch = registry.getVector(channelId)) ch->subscribe();
            }
        }

        popUpWindow->addTarget(channelId);
        addTargetUI(channelId, label);
    }
}





/*
 * Adds a visual pill to the card's layout representing the chosen target.
 * Includes a remove button for specific target deletion.
 */





void CanvasDockItem::addTargetUI(int channelId, const QString& label) {
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

    connect(removeBtn, &QPushButton::clicked, this, [this, row, channelId]() {
        subscribedChannels.removeOne(channelId);
        popUpWindow->removeTarget(channelId);
        
        auto& registry = TelemetryRegistry::getInstance();
        if (dataType == DataType::SCALAR) {
            if (auto ch = registry.getScalar(channelId)) ch->unsubscribe();
        } else if (dataType == DataType::VECTOR) {
            if (auto ch = registry.getVector(channelId)) ch->unsubscribe();
        }
        
        row->deleteLater();
    });
}