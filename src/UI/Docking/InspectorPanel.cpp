#include "InspectorPanel.h"
#include "Simulation/Components/ComponentInstance.h"
#include "Simulation/Components/ComponentBlueprint.h"
#include "Simulation/Components/LibraryManager.h"
#include "Simulation/SimulationManager.h"
#include "Core/Application.h" 
#include <QScrollArea>
#include <QLineEdit>
#include <QListWidget>
#include <QFrame>
#include <QGridLayout>
#include "UI/Toast/Toast.h"




ConnectorDropTargetBtn::ConnectorDropTargetBtn(const QString& connectorId, const QString& text, QWidget* parent) 
    : QPushButton(text, parent), connectorId(connectorId) {
    setAcceptDrops(true);
}




void ConnectorDropTargetBtn::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasFormat("application/rs-component")) {
        event->acceptProposedAction();
    }
}





void ConnectorDropTargetBtn::dragLeaveEvent(QDragLeaveEvent* event) {
    // previous drag logic, remvoed for now
}




void ConnectorDropTargetBtn::dragMoveEvent(QDragMoveEvent* event) {
    if (event->mimeData()->hasFormat("application/rs-component")) {
        event->acceptProposedAction();
    }
}




void ConnectorDropTargetBtn::dropEvent(QDropEvent* event) {
    setStyleSheet(""); 
    
    if (event->mimeData()->hasFormat("application/rs-component")) {
        QString modelId = QString::fromUtf8(event->mimeData()->data("application/rs-component"));
        emit componentDropped(connectorId, modelId);
        event->acceptProposedAction();
    }
}




/*
 * Intercepts scroll wheel events on child widgets before they process them.
 * Forwards the scroll to the main panel unless the widget has been explicitly clicked.
 */
bool InspectorPanel::eventFilter(QObject* obj, QEvent* event) {
    if (event->type() == QEvent::Wheel) {
        QWidget* widget = qobject_cast<QWidget*>(obj);
        
        if (widget && !widget->hasFocus()) {
            event->ignore(); 
            if (widget->parentWidget()) {
                QCoreApplication::sendEvent(widget->parentWidget(), event); // Manually scroll 
            }
            return true; 
        }
    }
    return QWidget::eventFilter(obj, event);
}




InspectorPanel::InspectorPanel(QWidget* parent) : QWidget(parent), currentUid(-1) {
    QVBoxLayout* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);

    QScrollArea* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    contentWidget = new QWidget(scrollArea);
    mainLayout = new QVBoxLayout(contentWidget);
    mainLayout->setAlignment(Qt::AlignTop);

    scrollArea->setWidget(contentWidget);
    outerLayout->addWidget(scrollArea);

    showEmptyState();
}




void InspectorPanel::clearLayout(QLayout* layout) {
    if (!layout) return;
    QLayoutItem* item;
    while ((item = layout->takeAt(0)) != nullptr) {
        if (item->widget()) {
            delete item->widget();
        } else if (item->layout()) {
            clearLayout(item->layout());
            delete item->layout();
        }
        delete item;
    }
}




void InspectorPanel::showEmptyState() {
    clearLayout(mainLayout);
    QLabel* emptyLabel = new QLabel("No Component Selected", this);
    emptyLabel->setAlignment(Qt::AlignCenter);
    emptyLabel->setStyleSheet("color: gray; font-style: italic;");
    mainLayout->addWidget(emptyLabel);
}




void InspectorPanel::setComponent(int uid) {
    if (currentUid == uid) return; 
    currentUid = uid;
    buildUI();
}




void InspectorPanel::populateAvailableComponents(QComboBox* combo) {
    combo->clear();
    combo->addItem("component...", -1);
    
    auto& componentMap = Application::getInstance()->getProject()->getComponentMap();
    for (ComponentInstance* c : componentMap) {
        if (c->parentUid == -1 && c->uid != currentUid) {
            combo->addItem(QString("%1 (UID: %2)").arg(c->name).arg(c->uid), c->uid);
        }
    }
}




void InspectorPanel::populateAvailableConnectors(QComboBox* combo, int targetUid) {
    combo->clear();
    combo->addItem("connector...");
    
    if (targetUid <= 0) return;
    
    ComponentInstance* targetComp = Application::getInstance()->getProject()->getComponentByUid(targetUid);
    if (targetComp && targetComp->blueprint && !targetComp->blueprint->connectors.isEmpty()) {
        combo->addItem(targetComp->blueprint->connectors.keys().first());
    }
}




/*
* Dynamically constructs the inspector UI based on the active selection.
* Builds global settings, standard info, inputs/outputs, and complex connection grids.
*/
#pragma region buildUI

void InspectorPanel::buildUI() {
    clearLayout(mainLayout);

    if (currentUid == -1) {
        showEmptyState();
        return;
    }

    Project* project = Application::getInstance()->getProject();

    if (currentUid == 0) {
        QJsonObject meta = project->getProjectData()["meta"].toObject();

        QGroupBox* globalBox = new QGroupBox("Global Robot Settings", this);
        QFormLayout* globalLayout = new QFormLayout(globalBox);

        globalLayout->addRow("Robot Name:", new QLineEdit(meta["name"].toString(), globalBox));

        QWidget* scriptWidget = new QWidget(globalBox);
        QHBoxLayout* scriptLayout = new QHBoxLayout(scriptWidget);
        scriptLayout->setContentsMargins(0, 0, 0, 0);
        scriptLayout->addWidget(new QLineEdit(meta["script_path"].toString(), scriptWidget));
        scriptLayout->addWidget(new QPushButton("Browse...", scriptWidget));
        globalLayout->addRow("Control Script:", scriptWidget);

        QListWidget* depList = new QListWidget(globalBox);
        for (const QJsonValue& val : meta["dependencies"].toArray()) {
            depList->addItem(val.toString());
        }
        globalLayout->addRow("Dependencies:", depList);

        mainLayout->addWidget(globalBox);
        mainLayout->addStretch();
        return;
    }

    ComponentInstance* comp = project->getComponentByUid(currentUid);
    if (!comp || !comp->blueprint) {
        showEmptyState();
        return;
    }

    QGroupBox* infoBox = new QGroupBox("Component Info", this);
    QFormLayout* infoLayout = new QFormLayout(infoBox);
    infoLayout->addRow("Name:", new QLabel(comp->name, infoBox));
    infoLayout->addRow("Model:", new QLabel(comp->model, infoBox));
    mainLayout->addWidget(infoBox);

    if (!comp->blueprint->inputDefs.isEmpty()) {
        QGroupBox* inputBox = new QGroupBox("Inputs & Controls", this);
        QFormLayout* inputLayout = new QFormLayout(inputBox);

        for (const QString& key : comp->blueprint->inputDefs.keys()) {
            IODef def = comp->blueprint->inputDefs[key];
            QWidget* targetWidget = new QWidget(inputBox); 
            QHBoxLayout* targetHBox = new QHBoxLayout(targetWidget);
            targetHBox->setContentsMargins(0, 0, 0, 0);

            QString jkey = def.targetJoint;

            QSlider* slider = new QSlider(Qt::Horizontal, targetWidget);
            slider->setRange(def.range.first, def.range.second);
            slider->setValue(comp->getJointTarget(jkey)); // switch from Actuator to Joint (as it is editor)
            slider->setFocusPolicy(Qt::StrongFocus); 
            slider->installEventFilter(this);

            QDoubleSpinBox* spinBox = new QDoubleSpinBox(targetWidget);
            spinBox->setRange(def.range.first, def.range.second);
            spinBox->setValue(comp->getJointTarget(jkey));
            spinBox->setFocusPolicy(Qt::StrongFocus); 
            spinBox->installEventFilter(this);

            connect(slider, &QSlider::valueChanged, spinBox, [spinBox](int val){
                spinBox->setValue(val); 
            });
            connect(spinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), slider, [slider](double val){ 
                slider->setValue(val); 
            });

            connect(spinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [capturedUid = currentUid, jkey](double val) {
                if (ComponentInstance* activeComp = Application::getInstance()->getProject()->getComponentByUid(capturedUid)) {
                    activeComp->setJointTarget(jkey, val);
                    // Log::info(QString("Set Joint Target: %1 --> %2").arg(jkey).arg(val));
                }
            });

            targetHBox->addWidget(slider);
            targetHBox->addWidget(spinBox);
            inputLayout->addRow(def.name + ":", targetWidget);
        }
        mainLayout->addWidget(inputBox);
    }

    if (!comp->blueprint->outputDefs.isEmpty()) {
        QGroupBox* outputBox = new QGroupBox("Sensor Outputs", this);
        QFormLayout* outputLayout = new QFormLayout(outputBox);

        for (const QString& key : comp->blueprint->outputDefs.keys()) {
            QLabel* valueLabel = new QLabel(QString::number(comp->getSensorCurrent(key)), outputBox);
            valueLabel->setObjectName("lbl_out_" + key); 
            outputLayout->addRow(comp->blueprint->outputDefs[key].name + " (" + comp->blueprint->outputDefs[key].unit + "):", valueLabel);
        }
        mainLayout->addWidget(outputBox);
    }

    if (!comp->blueprint->connectors.isEmpty()) {
        QGroupBox* connectorsBox = new QGroupBox("Connectors", this);
        QVBoxLayout* connectorsLayout = new QVBoxLayout(connectorsBox);

        QMap<QString, QPair<int, QString>> activeConnections;
        for (ComponentInstance* child : project->getComponentMap().values()) {
            if (child->parentUid == currentUid) {
                activeConnections.insert(child->parentConnector, qMakePair(child->uid, child->selfConnector));
            }
        }

        for (const QString& connId : comp->blueprint->connectors.keys()) {
            ConnectorDef def = comp->blueprint->connectors[connId];

            QFrame* frame = new QFrame(connectorsBox);
            frame->setFrameShape(QFrame::StyledPanel);
            
            QGridLayout* grid = new QGridLayout(frame);
            grid->setContentsMargins(5, 5, 5, 5);
            grid->setSpacing(4);

            QLabel* nameLabel = new QLabel(def.id, frame);
            QFont boldFont = nameLabel->font();
            boldFont.setBold(true);
            nameLabel->setFont(boldFont);
            grid->addWidget(nameLabel, 0, 0, 1, 3);

            QComboBox* childUidCombo = new QComboBox(frame);
            childUidCombo->setFocusPolicy(Qt::StrongFocus); 
            childUidCombo->installEventFilter(this);
            grid->addWidget(childUidCombo, 1, 0, 1, 2);

            QComboBox* childConnectorCombo = new QComboBox(frame);
            childConnectorCombo->setFocusPolicy(Qt::StrongFocus);
            childConnectorCombo->installEventFilter(this);
            grid->addWidget(childConnectorCombo, 2, 0, 1, 1);

            QComboBox* snapCombo = nullptr;
            QDoubleSpinBox* snapSpinBox = nullptr;
            QWidget* snapWidget = nullptr;

            if (def.mechanics.snapAngles.isEmpty()) {
                snapSpinBox = new QDoubleSpinBox(frame);
                snapSpinBox->setRange(-180.0, 180.0);
                snapSpinBox->setSuffix("°");
                snapSpinBox->setFocusPolicy(Qt::StrongFocus);
                snapSpinBox->installEventFilter(this);
                snapWidget = snapSpinBox;
            } else {
                snapCombo = new QComboBox(frame);
                for (float angle : def.mechanics.snapAngles) {
                    snapCombo->addItem(QString::number(angle) + "°", angle);
                }
                snapCombo->setFocusPolicy(Qt::StrongFocus);
                snapCombo->installEventFilter(this);
                snapWidget = snapCombo;
            }
            grid->addWidget(snapWidget, 2, 1, 1, 1);

            bool isConnected = activeConnections.contains(connId);

            if (isConnected) {
                childUidCombo->addItem(QString("UID: %1").arg(activeConnections[connId].first)); 
                childConnectorCombo->addItem(activeConnections[connId].second);
                childUidCombo->setEnabled(false);
                childConnectorCombo->setEnabled(false);
                snapWidget->setEnabled(false);

                QPushButton* actionBtn = new QPushButton("X", frame);
                actionBtn->setMaximumWidth(50);
                actionBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
                grid->addWidget(actionBtn, 1, 2, 2, 1);

                connect(actionBtn, &QPushButton::clicked, this, []() {
                    /* TODO: Disconnect logic */ 
                });
                
            } else {
                populateAvailableComponents(childUidCombo);
                populateAvailableConnectors(childConnectorCombo, -1);

                ConnectorDropTargetBtn* actionBtn = new ConnectorDropTargetBtn(def.id, "+", frame);
                actionBtn->setMaximumWidth(50);
                actionBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
                grid->addWidget(actionBtn, 1, 2, 2, 1);

                EditorWindow* mainWindow = qobject_cast<EditorWindow*>(this->window());
                if (mainWindow) {
                    connect(mainWindow, &EditorWindow::componentLibDragStart, actionBtn, [actionBtn](const QString& /*modelId*/) {
                        actionBtn->setStyleSheet("border: 1px solid #5fa4ff;"); 
                    });
                    connect(mainWindow, &EditorWindow::componentLibDragEnd, actionBtn, [actionBtn]() {
                        actionBtn->setStyleSheet(""); 
                    });
                }

                connect(actionBtn, &QPushButton::clicked, this, [this]() {
                    Toast::showMessage(this, "drag a component from the Component Library");
                });
                
                
                connect(actionBtn, &ConnectorDropTargetBtn::componentDropped, this, [this, capturedUid = currentUid, snapCombo, snapSpinBox](const QString& targetConn, const QString& modelId) {
                    float snapAngle = 0.0f;
                    if (snapCombo) snapAngle = snapCombo->currentData().toFloat();
                    else if (snapSpinBox) snapAngle = snapSpinBox->value();

                    Project* project = Application::getInstance()->getProject();
                    project->createComponentInstance(capturedUid, targetConn, modelId, "", snapAngle);
                    
                    QTimer::singleShot(0, this, [this, project]() {
                        SimulationManager* simManager = Application::getInstance()->getSimulationManager();
                        std::lock_guard<std::mutex> lock(simManager->physicsMutex);

                        MujocoContext::getInstance()->loadModelFromString(
                            project->generateMujocoXML().toStdString()
                        );

                        simManager->cacheMujocoIds(
                            project->getRootComponent(),
                            MujocoContext::getInstance()->getModel()
                        );

                        Application::getInstance()->getEditor()->refresh();
                    });
                });
            }
            connectorsLayout->addWidget(frame);
        }
        mainLayout->addWidget(connectorsBox);
    }
    mainLayout->addStretch(); 
}







void InspectorPanel::setInputState(bool flag) {
    contentWidget->setEnabled(flag);
}





void InspectorPanel::updateLiveValues() {
    if (currentUid <= 0) return;

    if (ComponentInstance* comp = Application::getInstance()->getProject()->getComponentByUid(currentUid)) {
        for (const QString& key : comp->blueprint->outputDefs.keys()) {
            if (QLabel* label = this->findChild<QLabel*>("lbl_out_" + key)) {
                label->setText(QString::number(comp->getSensorCurrent(key), 'f', 2)); 
            }
        }
    }
}