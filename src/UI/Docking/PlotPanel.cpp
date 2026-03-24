#include "PlotPanel.h"
#include <QPushButton>
#include <QCheckBox>
#include <QLabel>
#include <QHBoxLayout>
#include "Core/Application.h"
#include <functional>
#include <QColorDialog>





PlotPanel::PlotPanel(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    customPlot = new QCustomPlot(this);
    customPlot->setMinimumHeight(300); 
    
    customPlot->setBackground(QBrush(QColor(30, 30, 30)));
    customPlot->xAxis->setLabel("Time (s)");
    customPlot->yAxis->setLabel("Value");
    customPlot->xAxis->setBasePen(QPen(Qt::white));
    customPlot->xAxis->setTickPen(QPen(Qt::white));
    customPlot->xAxis->setTickLabelColor(Qt::white);
    customPlot->yAxis->setBasePen(QPen(Qt::white));
    customPlot->yAxis->setTickPen(QPen(Qt::white));
    customPlot->yAxis->setTickLabelColor(Qt::white);
    customPlot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);

    mainLayout->addWidget(customPlot, 2); 



    // Theme setup
    QColor textColor("#bbb"); 
    customPlot->setBackground(QBrush(QColor(30, 30, 30)));
    
    customPlot->xAxis->setLabel("Time (s)");
    customPlot->yAxis->setLabel("Value");
    customPlot->xAxis->setLabelColor(textColor);
    customPlot->yAxis->setLabelColor(textColor);
    customPlot->xAxis->setTickLabelColor(textColor);
    customPlot->yAxis->setTickLabelColor(textColor);

    QPen axisPen(textColor);
    customPlot->xAxis->setBasePen(axisPen);
    customPlot->xAxis->setTickPen(axisPen);
    customPlot->xAxis->setSubTickPen(axisPen);
    customPlot->yAxis->setBasePen(axisPen);
    customPlot->yAxis->setTickPen(axisPen);
    customPlot->yAxis->setSubTickPen(axisPen);

    QColor gridColor(80, 80, 80);
    QPen gridPen(gridColor, 1, Qt::DotLine); 
    customPlot->xAxis->grid()->setPen(gridPen);
    customPlot->yAxis->grid()->setPen(gridPen);
    customPlot->xAxis->grid()->setZeroLinePen(QPen(textColor, 1, Qt::SolidLine));
    customPlot->yAxis->grid()->setZeroLinePen(QPen(textColor, 1, Qt::SolidLine));

    
    // target list setup
    targetListArea = new QScrollArea(this);
    targetListArea->setWidgetResizable(true);
    
    targetListWidget = new QWidget();
    targetListLayout = new QVBoxLayout(targetListWidget);
    targetListLayout->setAlignment(Qt::AlignTop);
    
    targetListArea->setWidget(targetListWidget);
    mainLayout->addWidget(targetListArea, 1); 

    
    
    // Add Target Button
    QPushButton* addBtn = new QPushButton("+ Add Plot Target", this);
    mainLayout->addWidget(addBtn);
    connect(addBtn, &QPushButton::clicked, this, &PlotPanel::showAddTargetDialog);

    
    

    updateTimer = new QTimer(this);
    connect(updateTimer, &QTimer::timeout, this, &PlotPanel::onUpdateTimer);
    updateTimer->start(16); 
}








void PlotPanel::addTarget(const PlotTarget& target) {
    Project* proj = Application::getInstance()->getProject();
    ComponentInstance* comp = proj->getComponentByUid(target.compUid);
    if (!comp) return;

    QString targetId = getTargetId(target);

    RingBuffer<PlotPoint>* buffer = nullptr;
    if (target.type == PlotTargetType::SENSOR) {
        buffer = comp->getSensorBuffer(target.ioKey);
    } else {
        buffer = comp->getActuatorBuffer(target.ioKey);
    }
    
    if (buffer) buffer->resize(2000);

    proj->getActivePlots()->append(target);

    QCPGraph* newGraph = customPlot->addGraph();
    newGraph->setPen(QPen(target.color, 2));
    
    targetGraphs.insert(targetId, newGraph);
    targetCursors.insert(targetId, 0); 

    buildTargetRowUI(target);
}








void PlotPanel::removeTarget(const QString& targetId) {
    Project* proj = Application::getInstance()->getProject();
    
    for (int i = 0; i < proj->getActivePlots()->size(); ++i) {
        if (getTargetId(proj->getActivePlots()->at(i)) == targetId) {
            ComponentInstance* comp = proj->getComponentByUid(proj->getActivePlots()->at(i).compUid);
            if (comp) {
                RingBuffer<PlotPoint>* buffer = (proj->getActivePlots()->at(i).type == PlotTargetType::SENSOR) ? 
                    comp->getSensorBuffer(proj->getActivePlots()->at(i).ioKey) : 
                    comp->getActuatorBuffer(proj->getActivePlots()->at(i).ioKey);
                
                if (buffer) buffer->resize(0); 
            }
            proj->getActivePlots()->removeAt(i);
            break;
        }
    }

    if (targetGraphs.contains(targetId)) {
        customPlot->removeGraph(targetGraphs[targetId]);
        targetGraphs.remove(targetId);
        targetCursors.remove(targetId);
        customPlot->replot();
    }

    QWidget* rowWidget = targetListWidget->findChild<QWidget*>(targetId);
    if (rowWidget) {
        targetListLayout->removeWidget(rowWidget);
        rowWidget->deleteLater();
    }
}








// Builds a UI row for a plot target, including visibility toggle, 
// dynamic component labels, a color picker button, and a remove button.
// Binds UI interactions directly to the QCustomPlot graphs and Project state.
void PlotPanel::buildTargetRowUI(const PlotTarget& target) {
    QString targetId = getTargetId(target);
    Project* proj = Application::getInstance()->getProject();
    ComponentInstance* comp = proj->getComponentByUid(target.compUid);
    QString compName = comp ? comp->name : "Unknown Component"; 

    QWidget* rowWidget = new QWidget(targetListWidget);
    rowWidget->setObjectName(targetId); 
    QHBoxLayout* rowLayout = new QHBoxLayout(rowWidget);
    rowLayout->setContentsMargins(5, 5, 5, 5);

    QCheckBox* visToggle = new QCheckBox(this);
    visToggle->setChecked(target.isVisible);
    rowLayout->addWidget(visToggle);

    QVBoxLayout* labelLayout = new QVBoxLayout();
    QString typeStr = (target.type == PlotTargetType::SENSOR) ? "Sensor Output" : "Actuator Input";
    
    QLabel* nameLabel = new QLabel(QString("<b>%1</b> (UID: %2)").arg(compName).arg(target.compUid));
    QLabel* detailLabel = new QLabel(QString("<span style='color:gray;'>%1: %2</span>").arg(typeStr).arg(target.ioKey));
    
    labelLayout->addWidget(nameLabel);
    labelLayout->addWidget(detailLabel);
    rowLayout->addLayout(labelLayout);

    rowLayout->addStretch();

    // --- NEW: Interactive Color Picker Button ---
    QPushButton* colorBtn = new QPushButton(this);
    colorBtn->setFixedSize(16, 16);
    colorBtn->setCursor(Qt::PointingHandCursor);
    colorBtn->setStyleSheet(QString("background-color: %1; border-radius: 2px; border: 1px solid #555;").arg(target.color.name()));
    rowLayout->addWidget(colorBtn);

    QPushButton* closeBtn = new QPushButton("✖", this);
    closeBtn->setFixedSize(24, 24);
    closeBtn->setStyleSheet("color: #E53935; border: none; font-weight: bold;");
    rowLayout->addWidget(closeBtn);

    targetListLayout->addWidget(rowWidget);

    // --- CONNECTIONS ---

    connect(closeBtn, &QPushButton::clicked, this, [this, targetId]() {
        removeTarget(targetId);
    });

    connect(colorBtn, &QPushButton::clicked, this, [this, targetId, colorBtn]() {
        // Grab current color from the graph
        QColor currentColor = Qt::white;
        if (targetGraphs.contains(targetId)) {
            currentColor = targetGraphs[targetId]->pen().color();
        }

        // Open the Qt Color Picker Dialog
        QColor newColor = QColorDialog::getColor(currentColor, this, "Select Line Color");

        if (newColor.isValid()) {
            // 1. Update the button's visual color
            colorBtn->setStyleSheet(QString("background-color: %1; border-radius: 2px; border: 1px solid #555;").arg(newColor.name()));

            // 2. Update the graph line
            if (targetGraphs.contains(targetId)) {
                targetGraphs[targetId]->setPen(QPen(newColor, 2));
                customPlot->replot();
            }

            // 3. Update the underlying Project data
            Project* proj = Application::getInstance()->getProject();
            for (int i = 0; i < proj->getActivePlots()->toList().size(); ++i) {
                if (getTargetId(proj->getActivePlots()->toList()[i]) == targetId) {
                    proj->getActivePlots()->toList()[i].color = newColor;
                    break;
                }
            }
        }
    });

    connect(visToggle, &QCheckBox::toggled, this, [this, targetId](bool checked) {
        if (targetGraphs.contains(targetId)) {
            targetGraphs[targetId]->setVisible(checked);
            
            Project* proj = Application::getInstance()->getProject();
            for (int i = 0; i < proj->getActivePlots()->toList().size(); ++i) {
                if (getTargetId(proj->getActivePlots()->toList()[i]) == targetId) {
                    proj->getActivePlots()->toList()[i].isVisible = checked;
                    break;
                }
            }
            customPlot->replot(); 
        }
    });
}







void PlotPanel::onUpdateTimer() {
    Project* proj = Application::getInstance()->getProject();
    double latestGlobalTime = 0.0;
    bool needsReplot = false;

    for (const PlotTarget& target : proj->getActivePlots()->toList()) {
        if (!target.isVisible) continue; 

        QString targetId = getTargetId(target);
        ComponentInstance* comp = proj->getComponentByUid(target.compUid);
        if (!comp) continue;

        RingBuffer<PlotPoint>* buffer = (target.type == PlotTargetType::SENSOR) ? 
                    comp->getSensorBuffer(target.ioKey) : 
                    comp->getActuatorBuffer(target.ioKey);

        if (!buffer || !targetCursors.contains(targetId) || !targetGraphs.contains(targetId)) continue;

        std::vector<PlotPoint> newPoints = buffer->pullNewData(targetCursors[targetId]);

        if (!newPoints.empty()) {
            QCPGraph* graph = targetGraphs[targetId];
            for (const PlotPoint& pt : newPoints) {
                graph->addData(pt.time, pt.value);
                if (pt.time > latestGlobalTime) latestGlobalTime = pt.time;
            }
            needsReplot = true;
        }
    }

    if (needsReplot) {
        for (const PlotTarget& target : proj->getActivePlots()->toList()) {
            QString id = getTargetId(target);
            if (targetGraphs.contains(id)) {
                targetGraphs[id]->data()->removeBefore(latestGlobalTime - memoryWindowSeconds);
            }
        }
        customPlot->rescaleAxes();
        customPlot->yAxis->scaleRange(1.2, customPlot->yAxis->range().center());
        customPlot->xAxis->setRange(latestGlobalTime, displayWindowSeconds, Qt::AlignRight);

        customPlot->replot();
    }
}









void PlotPanel::showAddTargetDialog() {
    QDialog dialog(this);
    dialog.setWindowTitle("Select Plot Target");
    dialog.setMinimumSize(400, 300);
    dialog.setStyleSheet("background-color: #2D2D2D; color: white;");

    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    QListWidget* listWidget = new QListWidget(&dialog);
    listWidget->setStyleSheet("QListWidget { background-color: #1E1E1E; border: 1px solid #444; } "
                              "QListWidget::item:selected { background-color: #2E7D32; }");
    layout->addWidget(listWidget);

    Project* proj = Application::getInstance()->getProject();
    ComponentInstance* root = proj->getRootComponent();

    std::function<void(ComponentInstance*)> addCompToList = [&](ComponentInstance* comp) {
        if (!comp) return;

        if (comp->blueprint) {
            for (const QString& key : comp->blueprint->outputDefs.keys()) {
                QString mujocoId = QString("comp_%1_%2").arg(comp->uid).arg(key);
                QString display = QString("[SENSOR]   %1  (%2)").arg(mujocoId).arg(comp->name);
                
                QListWidgetItem* item = new QListWidgetItem(display, listWidget);
                item->setData(Qt::UserRole, QString("%1|0|%2").arg(comp->uid).arg(key)); 
            }
            
            for (const QString& key : comp->blueprint->inputDefs.keys()) {
                QString mujocoId = QString("comp_%1_%2").arg(comp->uid).arg(key);
                QString display = QString("[ACTUATOR] %1  (%2)").arg(mujocoId).arg(comp->name);
                
                QListWidgetItem* item = new QListWidgetItem(display, listWidget);
                item->setData(Qt::UserRole, QString("%1|1|%2").arg(comp->uid).arg(key));
            }
        }

        for (ComponentInstance* child : comp->children) {
            addCompToList(child);
        }
    };

    addCompToList(root);

    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted) {
        QListWidgetItem* selected = listWidget->currentItem();
        if (selected) {
            QStringList parts = selected->data(Qt::UserRole).toString().split('|');
            if (parts.size() == 3) {
                PlotTarget target;
                target.compUid = parts[0].toInt();
                target.type = (parts[1] == "0") ? PlotTargetType::SENSOR : PlotTargetType::ACTUATOR;
                target.ioKey = parts[2];

                int hue = QRandomGenerator::global()->bounded(360);
                target.color = QColor::fromHsv(hue, 220, 240);
                target.isVisible = true;

                QString newTargetId = getTargetId(target);
                for (const PlotTarget& existing : proj->getActivePlots()->toList()) {
                    if (getTargetId(existing) == newTargetId) {
                        return; 
                    }
                }

                addTarget(target);
            }
        }
    }
}








void PlotPanel::clearAllGraphs() {
    for (QCPGraph* graph : targetGraphs.values()) {
        graph->data()->clear();
    }

    for (QString key : targetCursors.keys()) {
        targetCursors[key] = 0;
    }

    customPlot->xAxis->setRange(0, displayWindowSeconds);
    customPlot->replot();

    Project* proj = Application::getInstance()->getProject();
    for (const PlotTarget& target : proj->getActivePlots()->toList()) {
        ComponentInstance* comp = proj->getComponentByUid(target.compUid);
        if (comp) {
            RingBuffer<PlotPoint>* buffer = (target.type == PlotTargetType::SENSOR) ? 
                comp->getSensorBuffer(target.ioKey) : 
                comp->getActuatorBuffer(target.ioKey);
            
            if (buffer) buffer->clear();
        }
    }
}