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
#include <QInputDialog>
#include <QMessageBox>
#include <QFile>
#include <QDir>
#include <QJsonArray>
#include <QDebug>



void fixComboBoxPolicy(QComboBox* combo) {
    combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    combo->setMinimumContentsLength(5); 
    combo->setMinimumWidth(100); 
    combo->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
}


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
    
    auto& componentMap = Application::getInstance()->getProject()->getComponentMap();
    for (ComponentInstance* c : componentMap) {
        if (c->parentUid == -1 && c->uid != currentUid) {
            combo->addItem(QString("%1 (UID: %2)").arg(c->name).arg(c->uid), c->uid);
        }
    }
}




void InspectorPanel::populateAvailableConnectors(QComboBox* combo, int targetUid) {
    combo->clear();
    if (targetUid <= 0) return;
    
    ComponentInstance* targetComp = Application::getInstance()->getProject()->getComponentByUid(targetUid);
    QList<QString> connectors = targetComp->getFreeConnections();
    for (const QString& connName : connectors) {
        combo->addItem(connName, connName);
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

    if (currentUid == 0) {
        build_UID_0();
        return;
    }

    Project* project = Application::getInstance()->getProject();
    auto reloadSimulation = [this, project]() {
        QTimer::singleShot(0, this, [this, project]() {
            SimulationManager* simManager = Application::getInstance()->getSimulationManager();
            std::lock_guard<std::mutex> lock(simManager->physicsMutex);
            
            project->refresh();
            MujocoContext::getInstance()->loadModelFromString(
                project->generateMujocoXML().toStdString()
            );
            simManager->cacheMujocoIds(
                project->getRootComponent(), 
                MujocoContext::getInstance()->getModel()
            );
            Application::getInstance()->getEditor()->refresh();
        });
    };


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
        build_inputs(comp);
    }

    if (!comp->blueprint->outputDefs.isEmpty()) {
        build_outputs(comp);
    }

    if (!comp->blueprint->connectors.isEmpty()) {
        QGroupBox* connectorsBox = new QGroupBox("Connectors", this);
        QVBoxLayout* connectorsLayout = new QVBoxLayout(connectorsBox);

        QMap<QString, QPair<int, QString>> activeConnections;
        activeConnections = comp->getActiveConnections();
        

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
            fixComboBoxPolicy(childUidCombo);
            childUidCombo->setFocusPolicy(Qt::StrongFocus); 
            childUidCombo->installEventFilter(this);

            QComboBox* snapCombo = nullptr;
            QDoubleSpinBox* snapSpinBox = nullptr;
            QWidget* snapWidget = nullptr;

            bool isConnected = activeConnections.contains(connId);

            if(comp->selfConnector == connId){ 

                // for parent connection (disabled if active)
                grid->addWidget(childUidCombo, 1, 0, 1, 3);
                childUidCombo->clear();
                childUidCombo->addItem("Connected to Parent", -1);
                childUidCombo->setCurrentIndex(0);
                frame->setEnabled(false);
                
            }else{

                // for child connections
                grid->addWidget(childUidCombo, 1, 0, 1, 2);
                populateAvailableComponents(childUidCombo); 

                if (isConnected) {
                    int childUid = activeConnections[connId].first;
                    QString childConnId = activeConnections[connId].second;
                    ComponentInstance* childComp = project->getComponentByUid(childUid);
    
                    QComboBox* childConnectorCombo = new QComboBox(frame);
                    fixComboBoxPolicy(childConnectorCombo);
                    childConnectorCombo->setFocusPolicy(Qt::StrongFocus);
                    childConnectorCombo->installEventFilter(this);
                    grid->addWidget(childConnectorCombo, 2, 0, 1, 1);
                    
                    // Add the active component back in manually
                    childUidCombo->addItem(QString("%1 (Active)").arg(childComp->name), childComp->uid);
                    childUidCombo->setCurrentIndex(childUidCombo->count() - 1); 
    
                    populateAvailableConnectors(childConnectorCombo, childUid);
                    childConnectorCombo->addItem(childConnId + " (Active)", childConnId);
                    childConnectorCombo->setCurrentIndex(childConnectorCombo->count() - 1);
    
                    if (def.mechanics.snapAngles.isEmpty()) {
                        snapSpinBox = new QDoubleSpinBox(frame);
                        snapSpinBox->setRange(-180.0, 180.0);
                        snapSpinBox->setSuffix("°");
                        snapSpinBox->setValue(childComp->snapAngle); 
                        snapSpinBox->setFocusPolicy(Qt::StrongFocus);
                        snapSpinBox->installEventFilter(this);
                        snapWidget = snapSpinBox;
    
                        // signal: snapangle
                        connect(snapSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [childComp, reloadSimulation](double val) {
                            childComp->snapAngle = val;
                            reloadSimulation();
                        });
                    } else {
                        snapCombo = new QComboBox(frame);
                        for (float angle : def.mechanics.snapAngles) {
                            snapCombo->addItem(QString::number(angle) + "°", angle);
                        }
                        int snapIdx = snapCombo->findData(childComp->snapAngle);
                        if (snapIdx != -1) snapCombo->setCurrentIndex(snapIdx); 
                        snapCombo->setFocusPolicy(Qt::StrongFocus);
                        snapCombo->installEventFilter(this);
                        snapWidget = snapCombo;
    
                        // signal: snapangle
                        connect(snapCombo, &QComboBox::currentIndexChanged, this, [childComp, snapCombo, reloadSimulation]() {
                            childComp->snapAngle = snapCombo->currentData().toFloat();
                            reloadSimulation();
                        });
                    }
                    
                    grid->addWidget(snapWidget, 2, 1, 1, 1);
    
                    // signal: connector
                    connect(childConnectorCombo, &QComboBox::currentIndexChanged, this, [childComp, childConnectorCombo, reloadSimulation]() {
                        childComp->selfConnector = childConnectorCombo->currentText();
                        reloadSimulation();
                    });
    
                    // signal: component switch
                    connect(childUidCombo, &QComboBox::currentIndexChanged, this, [project, comp, capturedUid = currentUid, connId, childUidCombo, childComp, reloadSimulation]() {
                        int newUid = childUidCombo->currentData().toInt();
                        if (newUid == childComp->uid || newUid == -1) return;
                        
                        childComp->parentUid = -1;
                        childComp->parentConnector = "";
                        comp->children.removeAll(childComp);
                        
                        ComponentInstance* newChild = project->getComponentByUid(newUid);
                        if (newChild) {
                            newChild->parentUid = capturedUid;
                            newChild->parentConnector = connId;
                            newChild->snapAngle = childComp->snapAngle;  
                            comp->children.append(newChild);
                            if (!newChild->blueprint->connectors.isEmpty()) {
                                newChild->selfConnector = newChild->blueprint->connectors.first().id;
                            } else {
                                newChild->selfConnector = ""; 
                            }
                        }
                        reloadSimulation();
                    });
    
                    QPushButton* actionBtn = new QPushButton("X", frame);
                    actionBtn->setMaximumWidth(50);
                    actionBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
                    grid->addWidget(actionBtn, 1, 2, 2, 1);
    
                    connect(actionBtn, &QPushButton::clicked, this, [comp, childComp, reloadSimulation]() {
                        childComp->parentUid = -1;
                        childComp->parentConnector = "";
                        comp->children.removeAll(childComp);
                        reloadSimulation();
                    });
    
                } else {
                    childUidCombo->addItem("-", -1);
                    childUidCombo->setCurrentIndex(childUidCombo->count() - 1);
    
                    ConnectorDropTargetBtn* actionBtn = new ConnectorDropTargetBtn(def.id, "+", frame);
                    actionBtn->setMaximumWidth(50);
                    actionBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
                    grid->addWidget(actionBtn, 1, 2, 1, 1);
    
                    EditorWindow* mainWindow = qobject_cast<EditorWindow*>(this->window());
                    if (mainWindow) {
                        connect(mainWindow, &EditorWindow::componentLibDragStart, actionBtn, [actionBtn](const QString& /*modelId*/) {
                            actionBtn->setStyleSheet("border: 1px solid #5fa4ff;"); 
                        });
                        connect(mainWindow, &EditorWindow::componentLibDragEnd, actionBtn, [actionBtn]() {
                            actionBtn->setStyleSheet(""); 
                        });
                    }

                    // signal: component
                    connect(childUidCombo, &QComboBox::currentIndexChanged, this, [this, comp, childUidCombo, def, snapCombo, snapSpinBox, reloadSimulation](int index) {
                        if (childUidCombo->currentData().toInt() != -1) {
                            int newUid = childUidCombo->currentData().toInt();
                            ComponentInstance* newChild = Application::getInstance()->getProject()->getComponentByUid(newUid);
                            if (newChild) {
                                newChild->parentUid = currentUid;
                                newChild->parentConnector = def.id;
                                newChild->selfConnector=newChild->blueprint->connectors.first().id;
                                comp->children.append(newChild);
                                if(newChild->blueprint->connectors.isEmpty()){
                                    newChild->selfConnector = "";
                                }else{
                                    newChild->selfConnector = newChild->blueprint->connectors.first().id;
                                }
                                newChild->snapAngle = 0.0f;
                                if (snapCombo) snapCombo->setCurrentIndex(0);
                                if (snapSpinBox) snapSpinBox->setValue(0.0);
                                reloadSimulation();
                            }
                        }
                    });
    
                    connect(actionBtn, &QPushButton::clicked, this, [this]() {
                        Toast::showMessage(this, "drag a component from the Component Library");
                    });
                    
                    // signal: create component
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
            }

            connectorsLayout->addWidget(frame);
        }
        mainLayout->addWidget(connectorsBox);
    }
    mainLayout->addStretch(); 
}








#pragma region build uid 0

void InspectorPanel::build_UID_0() {
    Project* project = Application::getInstance()->getProject();
    if (!project) return;

    QJsonObject projData = project->getProjectData();
    QJsonObject meta = projData["meta"].toObject();
    QJsonObject scriptObj = projData["script"].toObject();

    // Reusable lambda to cleanly reload physics and UI when the root connection changes
    auto reloadSimulation = [this, project]() {
        QTimer::singleShot(0, this, [this, project]() {
            SimulationManager* simManager = Application::getInstance()->getSimulationManager();
            std::lock_guard<std::mutex> lock(simManager->physicsMutex);
            MujocoContext::getInstance()->loadModelFromString(project->generateMujocoXML().toStdString());
            simManager->cacheMujocoIds(project->getRootComponent(), MujocoContext::getInstance()->getModel());
            Application::getInstance()->getEditor()->refresh();
        });
    };

    QGroupBox* globalBox = new QGroupBox("Global Robot Settings", this);
    QFormLayout* globalLayout = new QFormLayout(globalBox);

    // --- 1. Robot Name ---
    QLineEdit* nameEdit = new QLineEdit(meta["name"].toString(), globalBox);
    globalLayout->addRow("Robot Name:", nameEdit);
    
    connect(nameEdit, &QLineEdit::textChanged, this, [project](const QString& text) {
        QJsonObject pData = project->getProjectData();
        QJsonObject m = pData["meta"].toObject();
        m["name"] = text;
        pData["meta"] = m;
        project->setProjectData(pData);
    });

    // --- 2. Advanced Scripting UI ---
    QWidget* scriptWidget = new QWidget(globalBox);
    QHBoxLayout* scriptLayout = new QHBoxLayout(scriptWidget);
    scriptLayout->setContentsMargins(0, 0, 0, 0);

    QComboBox* scriptCombo = new QComboBox(scriptWidget);
    fixComboBoxPolicy(scriptCombo);
    QPushButton* btnAddScript = new QPushButton("Add", scriptWidget);
    QPushButton* btnDelScript = new QPushButton("Delete", scriptWidget);
    
    scriptLayout->addWidget(scriptCombo);
    scriptLayout->addWidget(btnAddScript);
    scriptLayout->addWidget(btnDelScript);
    globalLayout->addRow("Control Script:", scriptWidget);

    int activeScriptIdx = scriptObj["current"].toInt();
    QJsonArray allScriptsArr = scriptObj["paths"].toArray();

    scriptCombo->blockSignals(true);
    for (const QJsonValue& val : allScriptsArr) {
        scriptCombo->addItem(val.toString());
    }
    if (activeScriptIdx >= 0 && activeScriptIdx < allScriptsArr.size()) {
        scriptCombo->setCurrentIndex(activeScriptIdx);
    } else {
        scriptCombo->setCurrentIndex(-1);
    }
    scriptCombo->blockSignals(false);

    // Signal: Switch Active Script
    connect(scriptCombo, &QComboBox::currentTextChanged, this, [this, project](const QString& text) {
        if (text.isEmpty()) return;
        
        QJsonObject pData = project->getProjectData();
        QJsonObject sObj = pData["script"].toObject();
        int oldActive = sObj["current"].toInt();
        QJsonArray oArr = sObj["paths"].toArray();
        
        int index = -1;
        for (int i = 0; i < oArr.size(); ++i) {
            if (oArr[i].toString() == text) {
                index = i;
                break;
            }
        }
        sObj["current"] = index;
        pData["script"] = sObj;
        project->setProjectData(pData);
        
        if (auto panel = Application::getInstance()->getEditor()->scriptPanel) {
            Project* project = Application::getInstance()->getProject();
            project->setScript(index);
            panel->loadScript(project->getProjectDirectory() + "/" + text);
        }
    });

    // Signal: Add New Script
    connect(btnAddScript, &QPushButton::clicked, this, [this, project, scriptCombo]() {
        bool ok;
        QString name = QInputDialog::getText(this, "New Script", "Script File Name (e.g. custom.js):", QLineEdit::Normal, "", &ok);
        
        if (ok && !name.isEmpty()) {
            if (!name.endsWith(".js")) name += ".js";
            
            QFile file(project->getProjectDirectory() + "/" + name);
            if (!file.exists() && file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                file.write("// New Script: " + name.toUtf8() + "\n\nfunction setup() {\n\n}\n\nfunction loop() {\n\n}\n");
                file.close();
            }

            QJsonObject pData = project->getProjectData();
            QJsonObject sObj = pData["script"].toObject();
            QJsonArray oArr = sObj["paths"].toArray();
            oArr.append(name);
            sObj["paths"] = oArr;
            pData["script"] = sObj;
            project->setProjectData(pData);
            
            scriptCombo->addItem(name);
        }
    });

    // Signal: Delete Script
    connect(btnDelScript, &QPushButton::clicked, this, [this, project, scriptCombo]() {
        QString toDelete = scriptCombo->currentText();
        if (toDelete.isEmpty()) return;

        if (QMessageBox::question(this, "Delete Script", "Delete " + toDelete + " from disk?", QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
            QFile file(project->getProjectDirectory() + "/" + toDelete);
            if (file.exists()) file.remove();

            QJsonObject pData = project->getProjectData();
            QJsonObject sObj = pData["script"].toObject();
            QJsonArray allScriptsArr = sObj["paths"].toArray();
            QJsonArray newPaths;
            
            for (const QJsonValue& val : allScriptsArr) {
                if (val.toString() != toDelete) newPaths.append(val);
            }
            
            int deleteIndex = -1;
            for (int i = 0; i < allScriptsArr.size(); ++i) {
                if (allScriptsArr[i].toString() == toDelete) {
                    deleteIndex = i;
                    break;
                }
            }
            if (sObj["current"].toInt() == deleteIndex) {
                if (!newPaths.isEmpty()) {
                    sObj["current"] = 0; 
                } else {
                    sObj["current"] = -1;
                }
            }
            
            sObj["paths"] = newPaths;
            pData["script"] = sObj;
            project->setProjectData(pData);
            
            scriptCombo->removeItem(scriptCombo->currentIndex());
            
            QString newActive = sObj["current"].toInt() >= 0 ? newPaths[sObj["current"].toInt()].toString() : "";
            if(!newActive.isEmpty()) {
                if (auto panel = Application::getInstance()->getEditor()->scriptPanel) {
                    QJsonArray scriptPaths = project->getScriptPaths();
                    int idx_from_paths = -1;
                    for (int i = 0; i < scriptPaths.size(); ++i) {
                        if (scriptPaths[i].toString() == newActive) {
                            idx_from_paths = i;
                            break;
                        }
                    }
                    if (idx_from_paths != -1) {
                        project->setScript(idx_from_paths);
                        Log::info("Switched active script to: " + project->getProjectDirectory() + "/" + newActive);
                        panel->loadScript(project->getProjectDirectory() + "/" + newActive);
                    }
                }
            }
        }
    });

    mainLayout->addWidget(globalBox);


    // --- 3. Base Component Attachment (Virtual Origin) ---
    QGroupBox* connectorsBox = new QGroupBox("Base Component Attachment", this);
    QVBoxLayout* connectorsLayout = new QVBoxLayout(connectorsBox);

    QFrame* frame = new QFrame(connectorsBox);
    frame->setFrameShape(QFrame::StyledPanel);
    QGridLayout* grid = new QGridLayout(frame);
    grid->setContentsMargins(5, 5, 5, 5);
    grid->setSpacing(4);

    QLabel* originLabel = new QLabel("root", frame);
    QFont boldFont = originLabel->font();
    boldFont.setBold(true);
    originLabel->setFont(boldFont);
    grid->addWidget(originLabel, 0, 0, 1, 3);

    QComboBox* childUidCombo = new QComboBox(frame);
    fixComboBoxPolicy(childUidCombo);
    childUidCombo->setFocusPolicy(Qt::StrongFocus);
    childUidCombo->installEventFilter(this);

    // FIX: Check if an actual component is anchored to the virtual origin
    ComponentInstance* rootComp = project->getRootComponent();
    bool isConnected = (rootComp != nullptr);
    QString connId = "root";

    if (isConnected) {
        grid->addWidget(childUidCombo, 1, 0, 1, 2);
        populateAvailableComponents(childUidCombo); 

        QComboBox* childConnectorCombo = new QComboBox(frame);
        fixComboBoxPolicy(childConnectorCombo);
        childConnectorCombo->setFocusPolicy(Qt::StrongFocus);
        childConnectorCombo->installEventFilter(this);
        grid->addWidget(childConnectorCombo, 2, 0, 1, 1);
        
        childUidCombo->addItem(QString("%1 (Active)").arg(rootComp->name), rootComp->uid);
        childUidCombo->setCurrentIndex(childUidCombo->count() - 1); 

        populateAvailableConnectors(childConnectorCombo, rootComp->uid);
        QString activeConn = rootComp->selfConnector.isEmpty() ? "not found" : rootComp->selfConnector;
        childConnectorCombo->addItem(activeConn + " (Active)", activeConn);
        childConnectorCombo->setCurrentIndex(childConnectorCombo->count() - 1);

        QDoubleSpinBox* snapSpinBox = new QDoubleSpinBox(frame);
        snapSpinBox->setRange(-180.0, 180.0);
        snapSpinBox->setSuffix("°");
        snapSpinBox->setValue(rootComp->snapAngle); 
        snapSpinBox->setFocusPolicy(Qt::StrongFocus);
        snapSpinBox->installEventFilter(this);
        grid->addWidget(snapSpinBox, 2, 1, 1, 1);

        connect(snapSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [rootComp, reloadSimulation](double val) {
            rootComp->snapAngle = val;
            reloadSimulation();
        });

        connect(childConnectorCombo, &QComboBox::currentIndexChanged, this, [rootComp, childConnectorCombo, reloadSimulation]() {
            rootComp->selfConnector = childConnectorCombo->currentText();
            reloadSimulation();
        });

        connect(childUidCombo, &QComboBox::currentIndexChanged, this, [project, rootComp, connId, childUidCombo, reloadSimulation]() {
            int newUid = childUidCombo->currentData().toInt();
            if (newUid == rootComp->uid || newUid == -1) return;
            
            // Detach the old root
            rootComp->parentUid = -1;
            rootComp->parentConnector = "";
            
            // Anchor the new root to virtual UID 0
            ComponentInstance* newChild = project->getComponentByUid(newUid);
            if (newChild) {
                newChild->parentUid = 0;
                newChild->parentConnector = connId;
                newChild->snapAngle = rootComp->snapAngle;  
                
                if (newChild->blueprint && !newChild->blueprint->connectors.isEmpty()) {
                    newChild->selfConnector = newChild->blueprint->connectors.first().id;
                } else {
                    newChild->selfConnector = ""; 
                }
            }
            reloadSimulation();
        });

        QPushButton* actionBtn = new QPushButton("X", frame);
        actionBtn->setMaximumWidth(50);
        actionBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        grid->addWidget(actionBtn, 1, 2, 2, 1);

        connect(actionBtn, &QPushButton::clicked, this, [rootComp, reloadSimulation]() {
            // Detach by clearing parent data (no longer calling comp->children.removeAll)
            rootComp->parentUid = -1;
            rootComp->parentConnector = "";
            reloadSimulation();
        });

    } else {
        // UI when NO component is anchored to the virtual Origin
        childUidCombo->addItem("-", -1);
        childUidCombo->setCurrentIndex(childUidCombo->count() - 1);

        ConnectorDropTargetBtn* actionBtn = new ConnectorDropTargetBtn(connId, "+", frame);
        actionBtn->setMaximumWidth(50);
        actionBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        grid->addWidget(actionBtn, 1, 2, 1, 1);

        EditorWindow* mainWindow = qobject_cast<EditorWindow*>(this->window());
        if (mainWindow) {
            connect(mainWindow, &EditorWindow::componentLibDragStart, actionBtn, [actionBtn](const QString& /*modelId*/) {
                actionBtn->setStyleSheet("border: 1px solid #5fa4ff;"); 
            });
            connect(mainWindow, &EditorWindow::componentLibDragEnd, actionBtn, [actionBtn]() {
                actionBtn->setStyleSheet(""); 
            });
        }

        connect(childUidCombo, &QComboBox::currentIndexChanged, this, [childUidCombo, connId, reloadSimulation](int index) {
            if (childUidCombo->currentData().toInt() != -1) {
                int newUid = childUidCombo->currentData().toInt();
                ComponentInstance* newChild = Application::getInstance()->getProject()->getComponentByUid(newUid);
                if (newChild) {
                    newChild->parentUid = 0; 
                    newChild->parentConnector = connId;
                    
                    if (newChild->blueprint && !newChild->blueprint->connectors.isEmpty()) {
                        newChild->selfConnector = newChild->blueprint->connectors.first().id;
                    } else {
                        newChild->selfConnector = "";
                    }
                    newChild->snapAngle = 0.0f;
                    reloadSimulation();
                }
            }
        });

        connect(actionBtn, &QPushButton::clicked, this, [this]() {
            Toast::showMessage(this, "Drag a component from the Component Library");
        });
        
        connect(actionBtn, &ConnectorDropTargetBtn::componentDropped, this, [connId, reloadSimulation](const QString& targetConn, const QString& modelId) {
            Project* project = Application::getInstance()->getProject();
            // Pass 0 as the parentUid to anchor it to the absolute root
            project->createComponentInstance(0, targetConn, modelId, "", 0.0f);
            reloadSimulation();
        });
    }

    connectorsLayout->addWidget(frame);
    mainLayout->addWidget(connectorsBox);
    
    mainLayout->addStretch();
}








#pragma region buildUI helpers

void InspectorPanel::build_inputs(ComponentInstance* comp) {
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
        slider->setValue(comp->getJointTarget(jkey));
        slider->setFocusPolicy(Qt::StrongFocus);
        slider->installEventFilter(this);

        QDoubleSpinBox* spinBox = new QDoubleSpinBox(targetWidget);
        spinBox->setRange(def.range.first, def.range.second);
        spinBox->setValue(comp->getJointTarget(jkey));
        spinBox->setFocusPolicy(Qt::StrongFocus);
        spinBox->installEventFilter(this);

        connect(slider, &QSlider::valueChanged, spinBox, [spinBox](int val) {
            spinBox->setValue(val);
        });
        connect(spinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), slider, [slider](double val) {
            slider->setValue(val);
        });

        connect(spinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [capturedUid = currentUid, jkey](double val) {
            if (ComponentInstance* activeComp = Application::getInstance()->getProject()->getComponentByUid(capturedUid)) {
                activeComp->setJointTarget(jkey, val);
            }
        });

        targetHBox->addWidget(slider);
        targetHBox->addWidget(spinBox);
        inputLayout->addRow(def.name + ":", targetWidget);
    }
    mainLayout->addWidget(inputBox);
}





void InspectorPanel::build_outputs(ComponentInstance* comp) {
    QGroupBox* outputBox = new QGroupBox("Sensor Outputs", this);
    QFormLayout* outputLayout = new QFormLayout(outputBox);

    for (const QString& key : comp->blueprint->outputDefs.keys()) {
        QLabel* valueLabel = new QLabel(QString::number(comp->getSensorCurrent(key)), outputBox);
        valueLabel->setObjectName("lbl_out_" + key);
        outputLayout->addRow(comp->blueprint->outputDefs[key].name + " (" + comp->blueprint->outputDefs[key].unit + "):", valueLabel);
    }
    mainLayout->addWidget(outputBox);
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