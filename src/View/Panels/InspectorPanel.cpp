#include "InspectorPanel.h"

#include <QScrollArea>
#include <QLineEdit>
#include <QListWidget>
#include <QFrame>
#include <QGridLayout>
#include <QUndoStack>
#include <QInputDialog>
#include <QMessageBox>
#include <QFile>
#include <QDir>
#include <QJsonArray>
#include <QTimer>
#include <QDebug>
#include <qobject.h>

#include "Application/Application.h"
#include "Document/Project.h"
#include "Document/Components/ComponentInstance.h"
#include "Document/Components/ComponentBlueprint.h"
#include "Commands/CommandUtils.h"
#include "Commands/AddComponentCommand.h"
#include "Commands/RemoveComponentCommand.h"
#include "Simulation/SimulationManager.h"
#include "View/Widgets/Toast.h"


// UI Polish: Utility to force widgets to shrink gracefully in layouts
void applyShrinkablePolicy(QWidget* widget) {
    widget->setMinimumWidth(30);
    QSizePolicy policy = widget->sizePolicy();
    policy.setHorizontalPolicy(QSizePolicy::Expanding);
    widget->setSizePolicy(policy);
}


void fixComboBoxPolicy(QComboBox* combo) {
    combo->setSizeAdjustPolicy(QComboBox::AdjustToContentsOnFirstShow);
    applyShrinkablePolicy(combo);
}


// UI Polish: Uses a transparent line edit to allow long text to shrink/scroll instead of forcing layout expansion
QLineEdit* createShrinkableLabel(const QString& text, QWidget* parent = nullptr) {
    QLineEdit* le = new QLineEdit(text, parent);
    le->setReadOnly(true);
    le->setFrame(false);
    le->setStyleSheet("background: transparent; border: none; color: palette(text);");
    le->setCursorPosition(0);
    applyShrinkablePolicy(le);
    return le;
}


#pragma region ConnectorDropTargetBtn

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
    // No hover-state cleanup needed currently
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


#pragma region setup

InspectorPanel::InspectorPanel(QWidget* parent) : QWidget(parent), currentUid(-1) {
    QVBoxLayout* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);

    QScrollArea* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    contentWidget = new QWidget(scrollArea);
    mainLayout = new QVBoxLayout(contentWidget);
    mainLayout->setAlignment(Qt::AlignTop);

    mainLayout->setSpacing(16);
    mainLayout->setContentsMargins(12, 12, 12, 12);

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

bool InspectorPanel::eventFilter(QObject* obj, QEvent* event) {
    if (event->type() == QEvent::Wheel) {
        QWidget* widget = qobject_cast<QWidget*>(obj);
        if (widget && !widget->hasFocus()) {
            event->ignore();
            if (widget->parentWidget()) {
                QCoreApplication::sendEvent(widget->parentWidget(), event);
            }
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

void InspectorPanel::setComponent(int uid) {
    if (currentUid == uid) return;
    currentUid = uid;
    buildUI();
}

void InspectorPanel::setInputState(bool flag) {
    contentWidget->setEnabled(flag);
}


#pragma region combo population helpers

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


#pragma region command helper

void InspectorPanel::pushCommand(std::function<void()> doFn, std::function<void()> undoFn,
                                  const QString& text, const QString& mergeKey, bool requiresReload) {
    Commands::push(std::move(doFn), std::move(undoFn), text, mergeKey, requiresReload);
}


#pragma region buildUI

void InspectorPanel::buildUI() {
    // A slider drag previews values live without commands; rebuilding here
    // would delete the slider mid-gesture. sliderReleased runs the deferred
    // rebuild if one was requested.
    if (dragInProgress) {
        rebuildPending = true;
        return;
    }
    rebuildPending = false;

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
    ComponentInstance* comp = project->getComponentByUid(currentUid);
    if (!comp || !comp->blueprint) {
        showEmptyState();
        return;
    }

    QGroupBox* infoBox = new QGroupBox("Component Info", this);
    QFormLayout* infoLayout = new QFormLayout(infoBox);
    infoLayout->addRow("Component Name:", createShrinkableLabel(comp->name, infoBox));
    infoLayout->addRow("Model ID:", createShrinkableLabel(comp->modelId, infoBox));
    infoLayout->addRow("UID:", createShrinkableLabel(QString::number(comp->uid), infoBox));
    mainLayout->addWidget(infoBox);

    if (!comp->blueprint->inputDefs.isEmpty()) {
        build_inputs(comp);
    }
    if (!comp->blueprint->outputDefs.isEmpty()) {
        build_outputs(comp);
    }
    if (!comp->blueprint->connectors.isEmpty()) {
        build_connectors(comp);
    }

    mainLayout->addStretch();
}


#pragma region buildUI helpers

void InspectorPanel::build_inputs(ComponentInstance* comp) {
    QGroupBox* inputBox = new QGroupBox("Inputs & Controls", this);
    QVBoxLayout* inputLayout = new QVBoxLayout(inputBox);

    struct InputRow {
        QString jkey;
        QSlider* slider;
        QDoubleSpinBox* spinBox;
    };
    QList<InputRow> rows;

    for (const QString& key : comp->blueprint->inputDefs.keys()) {
        IODef def = comp->blueprint->inputDefs[key];

        QWidget* itemWidget = new QWidget(inputBox);
        QVBoxLayout* itemVBox = new QVBoxLayout(itemWidget);
        itemVBox->setContentsMargins(0, 0, 0, 8);

        QLabel* titleLabel = new QLabel(def.name + ":", itemWidget);
        titleLabel->setWordWrap(true);

        QWidget* targetWidget = new QWidget(itemWidget);
        QHBoxLayout* targetHBox = new QHBoxLayout(targetWidget);
        targetHBox->setContentsMargins(0, 0, 0, 0);

        double currentVal = std::get<double>(comp->getJointTarget(def.targetJoint));

        QSlider* slider = new QSlider(Qt::Horizontal, targetWidget);
        slider->setObjectName("inp_slider_" + def.targetJoint); // for updateJointValues()
        slider->setRange(def.range.first, def.range.second);
        slider->setValue(currentVal);
        slider->setFocusPolicy(Qt::StrongFocus);
        slider->installEventFilter(this);

        QDoubleSpinBox* spinBox = new QDoubleSpinBox(targetWidget);
        spinBox->setObjectName("inp_spin_" + def.targetJoint); // for updateJointValues()
        spinBox->setRange(def.range.first, def.range.second);
        spinBox->setValue(currentVal);
        spinBox->setFocusPolicy(Qt::StrongFocus);
        spinBox->installEventFilter(this);

        targetHBox->addWidget(slider);
        targetHBox->addWidget(spinBox);

        itemVBox->addWidget(titleLabel);
        itemVBox->addWidget(targetWidget);
        inputLayout->addWidget(itemWidget);

        rows.append({def.targetJoint, slider, spinBox});
    }
    mainLayout->addWidget(inputBox);

    for (const InputRow& row : rows) {
        connect(row.slider, &QSlider::valueChanged, row.spinBox, [spinBox = row.spinBox](int val) {
            spinBox->setValue(val);
        });
        connect(row.spinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), row.slider, [slider = row.slider](double val) {
            slider->setValue(val);
        });

        QString jkey = row.jkey;
        QSlider* slider = row.slider;
        QDoubleSpinBox* spinBox = row.spinBox;
        int capturedUid = currentUid;
        // Merge keys: keyboard/wheel/spinbox nudges share one key per field, so
        // a run of consecutive nudges collapses into a single undo step. Mouse
        // drags get a UNIQUE key per drag (assigned in sliderPressed) -- a
        // finished drag is sealed and the next edit always starts a new undo
        // step, never merging into it. (Merging only ever happens with the
        // command on top of the stack.)
        QString nudgeMergeKey = QString("joint:%1:%2:nudge").arg(capturedUid).arg(jkey);

        auto pushJointCommand = [this, capturedUid, jkey](double oldVal, double newVal, const QString& mergeKey) {
            if (std::abs(oldVal - newVal) < 1e-9) return;
            pushCommand(
                [capturedUid, jkey, newVal]() {
                    if (auto* c = Application::getInstance()->getProject()->getComponentByUid(capturedUid)) {
                        IOData v = newVal; c->setJointTarget(jkey, v);
                    }
                },
                [capturedUid, jkey, oldVal]() {
                    if (auto* c = Application::getInstance()->getProject()->getComponentByUid(capturedUid)) {
                        IOData v = oldVal; c->setJointTarget(jkey, v);
                    }
                },
                "Set " + jkey,
                mergeKey,
                false // joint targets sync live; no MJCF rebuild needed
            );
        };

        // Mouse drag: capture the pre-drag value, seal the session with a
        // unique merge key, and switch to live preview.
        connect(slider, &QSlider::sliderPressed, this, [this, capturedUid, jkey]() {
            ComponentInstance* c = Application::getInstance()->getProject()->getComponentByUid(capturedUid);
            if (!c) return;
            dragStartValue = std::get<double>(c->getJointTarget(jkey));
            dragMergeKey = QString("joint:%1:%2:drag:%3").arg(capturedUid).arg(jkey).arg(++dragSessionCounter);
            dragInProgress = true;
        });

        connect(slider, &QSlider::valueChanged, this,
                [this, capturedUid, jkey, slider, nudgeMergeKey, pushJointCommand](int val) {
            ComponentInstance* c = Application::getInstance()->getProject()->getComponentByUid(capturedUid);
            if (!c) return;
            if (slider->isSliderDown()) {
                // Live preview only -- the physics loop picks this up every frame;
                // the undoable command is committed once on sliderReleased.
                IOData v = static_cast<double>(val); c->setJointTarget(jkey, v);
            } else {
                // Keyboard/wheel step (or programmatic sync from the spinbox):
                // commit immediately, merged with neighbouring nudges.
                double oldVal = std::get<double>(c->getJointTarget(jkey));
                pushJointCommand(oldVal, static_cast<double>(val), nudgeMergeKey);
            }
        });

        // End of drag: commit a single command (old = pre-drag value) under the
        // drag's unique key, finalizing the session. Value-only commands don't
        // trigger an editor refresh -- the widgets already show the value --
        // so no rebuild happens unless one was suppressed mid-drag.
        connect(slider, &QSlider::sliderReleased, this, [this, slider, capturedUid, jkey, pushJointCommand]() {
            dragInProgress = false;
            pushJointCommand(dragStartValue, static_cast<double>(slider->value()), dragMergeKey);
            if (rebuildPending) {
                rebuildPending = false;
                // Deferred: buildUI() deletes this very slider, so rebuilding
                // inside the sliderReleased emission would be a use-after-free.
                QTimer::singleShot(0, this, [this]() { buildUI(); });
            }
        });

        connect(spinBox, &QDoubleSpinBox::editingFinished, this,
                [this, capturedUid, jkey, spinBox, nudgeMergeKey, pushJointCommand]() {
            ComponentInstance* activeComp = Application::getInstance()->getProject()->getComponentByUid(capturedUid);
            if (!activeComp) return;
            double oldVal = std::get<double>(activeComp->getJointTarget(jkey));
            pushJointCommand(oldVal, spinBox->value(), nudgeMergeKey);
        });
    }
}


void InspectorPanel::build_outputs(ComponentInstance* comp) {
    QGroupBox* outputBox = new QGroupBox("Sensor Outputs", this);
    QFormLayout* outputLayout = new QFormLayout(outputBox);
    for (const QString& key : comp->blueprint->outputDefs.keys()) {
        IOData data = comp->getSensorCurrent(key);
        QLineEdit* valueLabel = createShrinkableLabel("", outputBox);
        valueLabel->setObjectName("lbl_out_" + key);

        if (std::holds_alternative<double>(data)) {
            valueLabel->setText(QString::number(std::get<double>(data)));
        } else if (std::holds_alternative<std::vector<double>>(data)) {
            const auto& vec = std::get<std::vector<double>>(data);
            QStringList parts;
            for (double v : vec) parts << QString::number(v, 'f', 2);
            valueLabel->setText(parts.join(", "));
        }

        outputLayout->addRow(comp->blueprint->outputDefs[key].name + " (" + comp->blueprint->outputDefs[key].unit + "):", valueLabel);
    }
    mainLayout->addWidget(outputBox);
}


void InspectorPanel::build_connectors(ComponentInstance* comp) {
    Project* project = Application::getInstance()->getProject();

    QGroupBox* connectorsBox = new QGroupBox("Connectors", this);
    QVBoxLayout* connectorsLayout = new QVBoxLayout(connectorsBox);

    QMap<QString, QPair<int, QString>> activeConnections = comp->getActiveConnections();

    struct ConnectorRow {
        QString connId;
        ConnectorDef def;
        bool isSelfConnector = false;
        bool isConnected = false;
        int childUid = -1;

        QComboBox* childUidCombo = nullptr;
        QComboBox* childConnectorCombo = nullptr;
        QComboBox* snapCombo = nullptr;
        QDoubleSpinBox* snapSpinBox = nullptr;
        QPushButton* detachBtn = nullptr;
        ConnectorDropTargetBtn* dropBtn = nullptr;
    };
    QList<ConnectorRow> rows;

    for (const QString& connId : comp->blueprint->connectors.keys()) {
        ConnectorRow row;
        row.connId = connId;
        row.def = comp->blueprint->connectors[connId];
        row.isSelfConnector = (comp->selfConnector == connId);
        row.isConnected = activeConnections.contains(connId);

        QFrame* frame = new QFrame(connectorsBox);
        frame->setFrameShape(QFrame::StyledPanel);
        QGridLayout* grid = new QGridLayout(frame);
        grid->setContentsMargins(5, 5, 5, 5);
        grid->setSpacing(4);

        grid->setColumnStretch(0, 1);

        QLabel* nameLabel = new QLabel(row.def.id, frame);
        QFont boldFont = nameLabel->font();
        boldFont.setBold(true);
        nameLabel->setFont(boldFont);
        grid->addWidget(nameLabel, 0, 0, 1, 3);

        row.childUidCombo = new QComboBox(frame);
        fixComboBoxPolicy(row.childUidCombo);
        row.childUidCombo->setFocusPolicy(Qt::StrongFocus);
        row.childUidCombo->installEventFilter(this);

        if (row.isSelfConnector) {
            grid->addWidget(row.childUidCombo, 1, 0, 1, 3);
            row.childUidCombo->addItem("Connected to Parent", -1);
            row.childUidCombo->setCurrentIndex(0);
            frame->setEnabled(false);

        } else if (row.isConnected) {
            row.childUid = activeConnections[connId].first;
            QString childConnId = activeConnections[connId].second;
            ComponentInstance* childComp = project->getComponentByUid(row.childUid);

            grid->addWidget(row.childUidCombo, 1, 0, 1, 2);
            populateAvailableComponents(row.childUidCombo);
            row.childUidCombo->addItem(QString("%1 (Active)").arg(childComp->name), childComp->uid);
            row.childUidCombo->setCurrentIndex(row.childUidCombo->count() - 1);

            row.childConnectorCombo = new QComboBox(frame);
            fixComboBoxPolicy(row.childConnectorCombo);
            row.childConnectorCombo->setFocusPolicy(Qt::StrongFocus);
            row.childConnectorCombo->installEventFilter(this);
            grid->addWidget(row.childConnectorCombo, 2, 0, 1, 1);

            populateAvailableConnectors(row.childConnectorCombo, row.childUid);
            row.childConnectorCombo->addItem(childConnId + " (Active)", childConnId);
            row.childConnectorCombo->setCurrentIndex(row.childConnectorCombo->count() - 1);

            if (row.def.mechanics.snapAngles.isEmpty()) {
                row.snapSpinBox = new QDoubleSpinBox(frame);
                row.snapSpinBox->setRange(-180.0, 180.0);
                row.snapSpinBox->setSuffix("°");
                row.snapSpinBox->setValue(childComp->snapAngle);
                row.snapSpinBox->setFocusPolicy(Qt::StrongFocus);
                row.snapSpinBox->installEventFilter(this);
                grid->addWidget(row.snapSpinBox, 2, 1, 1, 1);
            } else {
                row.snapCombo = new QComboBox(frame);
                for (float angle : row.def.mechanics.snapAngles) {
                    row.snapCombo->addItem(QString::number(angle) + "°", angle);
                }
                int snapIdx = row.snapCombo->findData(childComp->snapAngle);
                if (snapIdx != -1) row.snapCombo->setCurrentIndex(snapIdx);
                row.snapCombo->setFocusPolicy(Qt::StrongFocus);
                row.snapCombo->installEventFilter(this);
                grid->addWidget(row.snapCombo, 2, 1, 1, 1);
            }

            row.detachBtn = new QPushButton("X", frame);
            row.detachBtn->setFixedWidth(32);
            row.detachBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
            grid->addWidget(row.detachBtn, 1, 2, 2, 1);

        } else {
            row.childUidCombo->addItem("-", -1);
            row.childUidCombo->setCurrentIndex(row.childUidCombo->count() - 1);
            grid->addWidget(row.childUidCombo, 1, 0, 1, 2);
            populateAvailableComponents(row.childUidCombo);

            row.dropBtn = new ConnectorDropTargetBtn(row.def.id, "+", frame);
            row.dropBtn->setFixedWidth(32);
            row.dropBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
            grid->addWidget(row.dropBtn, 1, 2, 1, 1);
        }

        connectorsLayout->addWidget(frame);
        rows.append(row);
    }
    mainLayout->addWidget(connectorsBox);

    EditorWindow* mainWindow = qobject_cast<EditorWindow*>(this->window());
    int parentUid = currentUid;

    for (const ConnectorRow& row : rows) {
        if (row.isSelfConnector) continue;

        if (row.isConnected) {
            int childUid = row.childUid;
            QString connId = row.connId;

            if (row.snapSpinBox) {
                QDoubleSpinBox* snapSpinBox = row.snapSpinBox;
                connect(snapSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                        [this, childUid, snapSpinBox](double val) {
                    ComponentInstance* childComp = Application::getInstance()->getProject()->getComponentByUid(childUid);
                    if (!childComp) return;
                    float oldAngle = childComp->snapAngle;
                    pushCommand(
                        [childUid, val]() { if (auto* c = Application::getInstance()->getProject()->getComponentByUid(childUid)) c->snapAngle = val; },
                        [childUid, oldAngle]() { if (auto* c = Application::getInstance()->getProject()->getComponentByUid(childUid)) c->snapAngle = oldAngle; },
                        "Set snap angle",
                        QString("snap:%1").arg(childUid)
                    );
                });
            }
            if (row.snapCombo) {
                QComboBox* snapCombo = row.snapCombo;
                connect(snapCombo, &QComboBox::currentIndexChanged, this, [this, childUid, snapCombo]() {
                    ComponentInstance* childComp = Application::getInstance()->getProject()->getComponentByUid(childUid);
                    if (!childComp) return;
                    float newAngle = snapCombo->currentData().toFloat();
                    float oldAngle = childComp->snapAngle;
                    pushCommand(
                        [childUid, newAngle]() { if (auto* c = Application::getInstance()->getProject()->getComponentByUid(childUid)) c->snapAngle = newAngle; },
                        [childUid, oldAngle]() { if (auto* c = Application::getInstance()->getProject()->getComponentByUid(childUid)) c->snapAngle = oldAngle; },
                        "Set snap angle"
                    );
                });
            }

            QComboBox* childConnectorCombo = row.childConnectorCombo;
            connect(childConnectorCombo, &QComboBox::currentIndexChanged, this, [this, childUid, childConnectorCombo]() {
                ComponentInstance* childComp = Application::getInstance()->getProject()->getComponentByUid(childUid);
                if (!childComp) return;
                QString newConn = childConnectorCombo->currentText();
                QString oldConn = childComp->selfConnector;
                pushCommand(
                    [childUid, newConn]() { if (auto* c = Application::getInstance()->getProject()->getComponentByUid(childUid)) c->selfConnector = newConn; },
                    [childUid, oldConn]() { if (auto* c = Application::getInstance()->getProject()->getComponentByUid(childUid)) c->selfConnector = oldConn; },
                    "Reassign connector"
                );
            });

            QComboBox* childUidCombo = row.childUidCombo;
            connect(childUidCombo, &QComboBox::currentIndexChanged, this,
                    [this, project, parentUid, connId, childUidCombo, childUid]() {
                int newUid = childUidCombo->currentData().toInt();
                if (newUid == childUid || newUid == -1) return;

                ComponentInstance* comp = project->getComponentByUid(parentUid);
                ComponentInstance* oldChild = project->getComponentByUid(childUid);
                ComponentInstance* newChild = project->getComponentByUid(newUid);
                if (!comp || !oldChild || !newChild) return;

                float carriedSnapAngle = oldChild->snapAngle;
                QString newSelfConn = newChild->blueprint->connectors.isEmpty() ? "" : newChild->blueprint->connectors.first().id;

                auto* stack = Application::getInstance()->getUndoStack();
                stack->beginMacro("Replace connected component");
                pushCommand(
                    [comp, oldChild]() { oldChild->parentUid = -1; oldChild->parentConnector = ""; comp->children.removeAll(oldChild); },
                    [comp, oldChild, parentUid, connId]() { oldChild->parentUid = parentUid; oldChild->parentConnector = connId; comp->children.append(oldChild); },
                    "Detach"
                );
                pushCommand(
                    [comp, newChild, parentUid, connId, carriedSnapAngle, newSelfConn]() {
                        newChild->parentUid = parentUid; newChild->parentConnector = connId;
                        newChild->snapAngle = carriedSnapAngle; newChild->selfConnector = newSelfConn;
                        comp->children.append(newChild);
                    },
                    [comp, newChild]() { newChild->parentUid = -1; newChild->parentConnector = ""; comp->children.removeAll(newChild); },
                    "Attach"
                );
                stack->endMacro();
            });

            QPushButton* detachBtn = row.detachBtn;
            connect(detachBtn, &QPushButton::clicked, this, [this, project, parentUid, connId, childUid]() {
                ComponentInstance* comp = project->getComponentByUid(parentUid);
                ComponentInstance* childComp = project->getComponentByUid(childUid);
                if (!comp || !childComp) return;

                QMessageBox box(this);
                box.setWindowTitle("Remove Attached Component");
                box.setText(QString("\"%1\" is attached to connector \"%2\".").arg(childComp->name, connId));
                box.setInformativeText("Detach keeps it in the project as an unattached component.\n"
                                       "Delete removes it -- and everything attached to it -- from the project.");
                QPushButton* detachChoice = box.addButton("Detach", QMessageBox::AcceptRole);
                QPushButton* deleteChoice = box.addButton("Delete Permanently", QMessageBox::DestructiveRole);
                box.addButton(QMessageBox::Cancel);
                box.exec();

                if (box.clickedButton() == detachChoice) {
                    pushCommand(
                        [comp, childComp]() { childComp->parentUid = -1; childComp->parentConnector = ""; comp->children.removeAll(childComp); },
                        [comp, childComp, parentUid, connId]() { childComp->parentUid = parentUid; childComp->parentConnector = connId; comp->children.append(childComp); },
                        "Detach component"
                    );
                } else if (box.clickedButton() == deleteChoice) {
                    Application::getInstance()->getUndoStack()->push(new RemoveComponentCommand(project, childUid));
                }
            });

        } else {
            QComboBox* childUidCombo = row.childUidCombo;
            ConnectorDropTargetBtn* dropBtn = row.dropBtn;
            QString defId = row.def.id;

            if (mainWindow) {
                connect(mainWindow, &EditorWindow::componentLibDragStart, dropBtn, [dropBtn](const QString&) {
                    dropBtn->setStyleSheet("border: 1px solid #5fa4ff;");
                });
                connect(mainWindow, &EditorWindow::componentLibDragEnd, dropBtn, [dropBtn]() {
                    dropBtn->setStyleSheet("");
                });
            }

            connect(childUidCombo, &QComboBox::currentIndexChanged, this,
                    [this, project, parentUid, defId, childUidCombo](int) {
                if (childUidCombo->currentData().toInt() == -1) return;
                int newUid = childUidCombo->currentData().toInt();

                ComponentInstance* comp = project->getComponentByUid(parentUid);
                ComponentInstance* newChild = project->getComponentByUid(newUid);
                if (!comp || !newChild) return;

                QString newSelfConn = newChild->blueprint->connectors.isEmpty() ? "" : newChild->blueprint->connectors.first().id;
                pushCommand(
                    [comp, newChild, parentUid, defId, newSelfConn]() {
                        newChild->parentUid = parentUid; newChild->parentConnector = defId;
                        newChild->snapAngle = 0.0f; newChild->selfConnector = newSelfConn;
                        comp->children.append(newChild);
                    },
                    [comp, newChild]() { newChild->parentUid = -1; newChild->parentConnector = ""; comp->children.removeAll(newChild); },
                    "Attach component"
                );
            });

            connect(dropBtn, &QPushButton::clicked, this, [this]() {
                Toast::showMessage(this, "drag a component from the Component Library");
            });

            connect(dropBtn, &ConnectorDropTargetBtn::componentDropped, this,
                    [this, parentUid](const QString& targetConn, const QString& modelId) {
                Project* project = Application::getInstance()->getProject();
                auto* stack = Application::getInstance()->getUndoStack();
                stack->push(new AddComponentCommand(project, parentUid, targetConn, modelId, "", 0.0f));
            });
        }
    }
}


#pragma region build uid 0

void InspectorPanel::build_UID_0() {
    Project* project = Application::getInstance()->getProject();
    if (!project) return;

    build_globalSettings(project);
    build_rootAttachment(project);
}


void InspectorPanel::build_globalSettings(Project* project) {
    QJsonObject projData = project->getProjectData();
    QJsonObject meta = projData["meta"].toObject();
    QJsonObject scriptObj = projData["script"].toObject();

    QGroupBox* globalBox = new QGroupBox("Global Robot Settings", this);
    QFormLayout* globalLayout = new QFormLayout(globalBox);

    QLineEdit* nameEdit = new QLineEdit(meta["name"].toString(), globalBox);
    applyShrinkablePolicy(nameEdit);
    globalLayout->addRow("Robot Name:", nameEdit);

    QWidget* scriptWidget = new QWidget(globalBox);
    QGridLayout* scriptLayout = new QGridLayout(scriptWidget);
    scriptLayout->setContentsMargins(0, 0, 0, 0);
    scriptLayout->setSpacing(4);

    QComboBox* scriptCombo = new QComboBox(scriptWidget);
    fixComboBoxPolicy(scriptCombo);
    QPushButton* btnAddScript = new QPushButton("Add", scriptWidget);
    QPushButton* btnDelScript = new QPushButton("Delete", scriptWidget);
    scriptLayout->addWidget(scriptCombo, 0, 0, 1, 2);
    scriptLayout->addWidget(btnAddScript, 1, 0);
    scriptLayout->addWidget(btnDelScript, 1, 1);
    globalLayout->addRow("Control Script:", scriptWidget);

    int activeScriptIdx = scriptObj["current"].toInt();
    QJsonArray allScriptsArr = scriptObj["paths"].toArray();

    scriptCombo->blockSignals(true);
    for (const QJsonValue& val : allScriptsArr) {
        scriptCombo->addItem(val.toString());
    }
    scriptCombo->setCurrentIndex(
        (activeScriptIdx >= 0 && activeScriptIdx < allScriptsArr.size()) ? activeScriptIdx : -1
    );
    scriptCombo->blockSignals(false);

    mainLayout->addWidget(globalBox);

    connect(nameEdit, &QLineEdit::editingFinished, this, [this, project, nameEdit]() {
        QString newName = nameEdit->text();
        QString oldName = project->getProjectData()["meta"].toObject()["name"].toString();
        if (newName == oldName) return;

        pushCommand(
            [project, newName]() {
                QJsonObject pData = project->getProjectData();
                QJsonObject m = pData["meta"].toObject();
                m["name"] = newName;
                pData["meta"] = m;
                project->setProjectData(pData);
            },
            [project, oldName]() {
                QJsonObject pData = project->getProjectData();
                QJsonObject m = pData["meta"].toObject();
                m["name"] = oldName;
                pData["meta"] = m;
                project->setProjectData(pData);
            },
            "Rename robot",
            QString(),
            false // the name only labels <mujoco model="...">; no sim reload needed
        );
        // ...but the scene tree displays the name, and value-only commands
        // don't trigger an editor refresh on their own.
        Application::getInstance()->getEditor()->scheduleRefresh();
    });

    connect(scriptCombo, &QComboBox::currentTextChanged, this, [this, project](const QString& text) {
        if (text.isEmpty()) return;

        QJsonObject pData = project->getProjectData();
        QJsonObject sObj = pData["script"].toObject();
        QJsonArray pathsArr = sObj["paths"].toArray();

        int index = -1;
        for (int i = 0; i < pathsArr.size(); ++i) {
            if (pathsArr[i].toString() == text) { index = i; break; }
        }
        sObj["current"] = index;
        pData["script"] = sObj;
        project->setProjectData(pData);

        project->setScript(index);
        if (auto panel = Application::getInstance()->getEditor()->scriptPanel) {
            panel->loadScript(project->getProjectDirectory() + "/" + text);
        }
    });

    connect(btnAddScript, &QPushButton::clicked, this, [this, project, scriptCombo]() {
        bool ok;
        QString name = QInputDialog::getText(this, "New Script", "Script File Name (e.g. custom.js):", QLineEdit::Normal, "", &ok);
        if (!ok || name.isEmpty()) return;

        if (!name.endsWith(".js")) name += ".js";

        QFile file(project->getProjectDirectory() + "/" + name);
        if (!file.exists() && file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            file.write("// New Script: " + name.toUtf8() + "\n\nfunction setup() {\n\n}\n\nfunction loop() {\n\n}\n");
            file.close();
        }

        QJsonArray newArr = project->getScriptPaths();
        newArr.append(name);
        project->setScriptPaths(newArr);
        project->setScript(newArr.size() - 1);
        project->saveProject();

        scriptCombo->addItem(name);
    });

    connect(btnDelScript, &QPushButton::clicked, this, [this, project, scriptCombo]() {
        QString toDelete = scriptCombo->currentText();
        int comboIndex = scriptCombo->currentIndex();
        if (toDelete.isEmpty()) return;

        if (QMessageBox::question(this, "Delete Script", "Delete " + toDelete + " from disk?",
                                   QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
            return;
        }

        QFile file(project->getProjectDirectory() + "/" + toDelete);
        if (file.exists()) file.remove();

        QJsonArray oldPaths = project->getScriptPaths();
        QJsonArray newPaths;
        for (const QJsonValue& val : oldPaths) {
            if (val.toString() != toDelete) newPaths.append(val);
        }

        int newActiveIndex = newPaths.isEmpty() ? -1 : 0;
        project->setScriptPaths(newPaths);
        project->setScript(newActiveIndex);
        project->saveProject();

        scriptCombo->removeItem(comboIndex);

        if (auto panel = Application::getInstance()->getEditor()->scriptPanel) {
            if (newActiveIndex >= 0) {
                QString newActiveName = newPaths[newActiveIndex].toString();
                panel->loadScript(project->getProjectDirectory() + "/" + newActiveName);
            }
        }
    });
}


void InspectorPanel::build_rootAttachment(Project* project) {
    ComponentInstance* rootComp = project->getRootComponent();
    bool isConnected = (rootComp != nullptr);
    const QString connId = "root";

    QGroupBox* connectorsBox = new QGroupBox("Base Component Attachment", this);
    QVBoxLayout* connectorsLayout = new QVBoxLayout(connectorsBox);

    QFrame* frame = new QFrame(connectorsBox);
    frame->setFrameShape(QFrame::StyledPanel);
    QGridLayout* grid = new QGridLayout(frame);
    grid->setContentsMargins(5, 5, 5, 5);
    grid->setSpacing(4);

    grid->setColumnStretch(0, 1);

    QLabel* originLabel = new QLabel("root", frame);
    QFont boldFont = originLabel->font();
    boldFont.setBold(true);
    originLabel->setFont(boldFont);
    grid->addWidget(originLabel, 0, 0, 1, 3);

    QComboBox* childUidCombo = new QComboBox(frame);
    fixComboBoxPolicy(childUidCombo);
    childUidCombo->setFocusPolicy(Qt::StrongFocus);
    childUidCombo->installEventFilter(this);
    populateAvailableComponents(childUidCombo);

    QPushButton* detachBtn = nullptr;
    ConnectorDropTargetBtn* dropBtn = nullptr;

    if (isConnected) {
        grid->addWidget(childUidCombo, 1, 0, 1, 2);
        childUidCombo->addItem(QString("%1 (Active)").arg(rootComp->name), rootComp->uid);
        childUidCombo->setCurrentIndex(childUidCombo->count() - 1);

        detachBtn = new QPushButton("X", frame);
        detachBtn->setFixedWidth(32);
        detachBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        grid->addWidget(detachBtn, 1, 2, 2, 1);
    } else {
        childUidCombo->addItem("-", -1);
        childUidCombo->setCurrentIndex(childUidCombo->count() - 1);
        grid->addWidget(childUidCombo, 1, 0, 1, 2);

        dropBtn = new ConnectorDropTargetBtn(connId, "+", frame);
        dropBtn->setFixedWidth(32);
        dropBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        grid->addWidget(dropBtn, 1, 2, 1, 1);
    }

    double yaw = project->getRootRotation().yaw();
    double pitch = project->getRootRotation().pitch();
    double roll = project->getRootRotation().roll();

    QLabel* rotLabel = new QLabel("Rotation (°)", frame);
    grid->addWidget(rotLabel, 3, 0, 1, 3);

    QDoubleSpinBox* spinRoll = new QDoubleSpinBox(frame);
    QDoubleSpinBox* spinPitch = new QDoubleSpinBox(frame);
    QDoubleSpinBox* spinYaw = new QDoubleSpinBox(frame);

    auto setupSpinBox = [](QDoubleSpinBox* spin, const QString& prefix, double val) {
        spin->setRange(-360.0, 360.0);
        spin->setDecimals(1);
        spin->setSingleStep(15.0);
        spin->setPrefix(prefix);
        spin->setValue(val);
        spin->setFocusPolicy(Qt::StrongFocus);
        applyShrinkablePolicy(spin);
    };
    setupSpinBox(spinRoll, "R: ", roll);
    setupSpinBox(spinPitch, "P: ", pitch);
    setupSpinBox(spinYaw, "Y: ", yaw);

    QHBoxLayout* rotLayout = new QHBoxLayout();
    rotLayout->addWidget(spinRoll);
    rotLayout->addWidget(spinPitch);
    rotLayout->addWidget(spinYaw);
    grid->addLayout(rotLayout, 4, 0, 1, 3);

    connectorsLayout->addWidget(frame);
    mainLayout->addWidget(connectorsBox);
    mainLayout->addStretch();

    if (isConnected) {
        connect(childUidCombo, &QComboBox::currentIndexChanged, this, [this, project, rootComp, childUidCombo]() {
            int newUid = childUidCombo->currentData().toInt();
            if (newUid == rootComp->uid || newUid == -1) return;

            ComponentInstance* newChild = project->getComponentByUid(newUid);
            if (!newChild) return;

            float carriedSnapAngle = rootComp->snapAngle;
            QString newSelfConn = newChild->blueprint->connectors.isEmpty() ? "" : newChild->blueprint->connectors.first().id;
            ComponentInstance* oldRoot = rootComp;

            auto* stack = Application::getInstance()->getUndoStack();
            stack->beginMacro("Replace root component");
            pushCommand(
                [project, oldRoot]() { oldRoot->parentUid = -1; oldRoot->parentConnector = ""; project->resetRootComponent(); },
                [project, oldRoot]() { oldRoot->parentUid = 0; oldRoot->parentConnector = "root"; project->setRootComponent(oldRoot); },
                "Detach root"
            );
            pushCommand(
                [project, newChild, carriedSnapAngle, newSelfConn]() {
                    newChild->parentUid = 0; newChild->parentConnector = "root";
                    newChild->snapAngle = carriedSnapAngle; newChild->selfConnector = newSelfConn;
                    project->setRootComponent(newChild);
                },
                [project, newChild]() { newChild->parentUid = -1; newChild->parentConnector = ""; project->resetRootComponent(); },
                "Attach root"
            );
            stack->endMacro();
        });

        connect(detachBtn, &QPushButton::clicked, this, [this, project, rootComp]() {
            QMessageBox box(this);
            box.setWindowTitle("Remove Root Component");
            box.setText(QString("\"%1\" is attached as the root component.").arg(rootComp->name));
            box.setInformativeText("Detach keeps it in the project as an unattached component.\n"
                                   "Delete removes it -- and everything attached to it -- from the project.");
            QPushButton* detachChoice = box.addButton("Detach", QMessageBox::AcceptRole);
            QPushButton* deleteChoice = box.addButton("Delete Permanently", QMessageBox::DestructiveRole);
            box.addButton(QMessageBox::Cancel);
            box.exec();

            if (box.clickedButton() == detachChoice) {
                pushCommand(
                    [project, rootComp]() { rootComp->parentUid = -1; rootComp->parentConnector = ""; project->resetRootComponent(); },
                    [project, rootComp]() { rootComp->parentUid = 0; rootComp->parentConnector = "root"; project->setRootComponent(rootComp); },
                    "Detach root component"
                );
            } else if (box.clickedButton() == deleteChoice) {
                Application::getInstance()->getUndoStack()->push(new RemoveComponentCommand(project, rootComp->uid));
            }
        });

    } else {
        EditorWindow* mainWindow = qobject_cast<EditorWindow*>(this->window());
        if (mainWindow) {
            connect(mainWindow, &EditorWindow::componentLibDragStart, dropBtn, [dropBtn](const QString&) {
                dropBtn->setStyleSheet("border: 1px solid #5fa4ff;");
            });
            connect(mainWindow, &EditorWindow::componentLibDragEnd, dropBtn, [dropBtn]() {
                dropBtn->setStyleSheet("");
            });
        }

        connect(childUidCombo, &QComboBox::currentIndexChanged, this, [this, project, childUidCombo](int) {
            if (childUidCombo->currentData().toInt() == -1) return;
            int newUid = childUidCombo->currentData().toInt();
            ComponentInstance* newChild = project->getComponentByUid(newUid);
            if (!newChild) return;

            QString newSelfConn = newChild->blueprint->connectors.isEmpty() ? "" : newChild->blueprint->connectors.first().id;
            pushCommand(
                [project, newChild, newSelfConn]() {
                    newChild->parentUid = 0; newChild->parentConnector = "root";
                    newChild->snapAngle = 0.0f; newChild->selfConnector = newSelfConn;
                    project->setRootComponent(newChild);
                },
                [project, newChild]() { newChild->parentUid = -1; newChild->parentConnector = ""; project->resetRootComponent(); },
                "Attach root component"
            );
        });

        connect(dropBtn, &QPushButton::clicked, this, [this]() {
            Toast::showMessage(this, "Drag a component from the Component Library");
        });

        connect(dropBtn, &ConnectorDropTargetBtn::componentDropped, this, [this](const QString& targetConn, const QString& modelId) {
            Project* project = Application::getInstance()->getProject();
            auto* stack = Application::getInstance()->getUndoStack();
            stack->push(new AddComponentCommand(project, 0, targetConn, modelId, "", 0.0f));
        });
    }

    auto wireRotation = [this, project, spinRoll, spinPitch, spinYaw](const char* axisKey) {
        return [this, project, spinRoll, spinPitch, spinYaw, axisKey]() {
            Rotation oldRot = project->getRootRotation();
            Rotation newRot(spinRoll->value(), spinPitch->value(), spinYaw->value());
            pushCommand(
                [project, newRot]() { project->setRootRotation(newRot); project->saveProject(); },
                [project, oldRot]() { project->setRootRotation(oldRot); project->saveProject(); },
                "Set rotation",
                QString("rootRotation:%1").arg(axisKey)
            );
        };
    };
    connect(spinRoll, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, wireRotation("roll"));
    connect(spinPitch, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, wireRotation("pitch"));
    connect(spinYaw, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, wireRotation("yaw"));
}


#pragma region live updates

/*
 * Syncs the joint input widgets (sliders/spinboxes) from the document without
 * rebuilding the panel. Used when a value-only command is undone -- a full
 * buildUI() would de-focus widgets and flicker for no structural reason.
 * Programmatic setValue() re-enters the valueChanged handlers, but they
 * compare against the document (which already holds the restored value), so
 * no new command is pushed.
 */
void InspectorPanel::updateJointValues() {
    if (currentUid <= 0) return;

    ComponentInstance* comp = Application::getInstance()->getProject()->getComponentByUid(currentUid);
    if (!comp || !comp->blueprint) return;

    for (const QString& key : comp->blueprint->inputDefs.keys()) {
        QString jkey = comp->blueprint->inputDefs[key].targetJoint;
        double val = std::get<double>(comp->getJointTarget(jkey));

        if (QSlider* slider = this->findChild<QSlider*>("inp_slider_" + jkey)) {
            slider->setValue(static_cast<int>(val));
        }
        if (QDoubleSpinBox* spinBox = this->findChild<QDoubleSpinBox*>("inp_spin_" + jkey)) {
            spinBox->setValue(val);
        }
    }
}


void InspectorPanel::updateLiveValues() {
    if (currentUid <= 0) return;

    if (ComponentInstance* comp = Application::getInstance()->getProject()->getComponentByUid(currentUid)) {
        for (const QString& key : comp->blueprint->outputDefs.keys()) {
            if (QLineEdit* label = this->findChild<QLineEdit*>("lbl_out_" + key)) {
                IOData data = comp->getSensorCurrent(key);
                if (std::holds_alternative<double>(data)) {
                    label->setText(QString::number(std::get<double>(data), 'f', 2));
                } else if (std::holds_alternative<std::vector<double>>(data)) {
                    const auto& vec = std::get<std::vector<double>>(data);
                    QStringList parts;
                    for (double v : vec) parts << QString::number(v, 'f', 2);
                    label->setText(parts.join(", "));
                }
                label->setCursorPosition(0);
            }
        }
    }
}
