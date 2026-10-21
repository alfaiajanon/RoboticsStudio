#include "InspectorPanel.h"

#include <QScrollArea>
#include <QLineEdit>
#include <QSpinBox>
#include <limits>
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
#include "Commands/WiringCommands.h"
#include "Commands/GenericCommand.h"
#include "Document/PowerNetwork.h"
#include <QCheckBox>
#include <QDoubleSpinBox>
#include "Simulation/SimulationManager.h"
#include "Utils/Log.h"
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
    emptyLabel->setStyleSheet("color: #73757b; font-style: italic;");
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
    if (!comp || !comp->getBlueprint()) {
        showEmptyState();
        return;
    }

    QGroupBox* infoBox = new QGroupBox("Component Info", this);
    QFormLayout* infoLayout = new QFormLayout(infoBox);
    QLineEdit* compNameEdit = new QLineEdit(comp->name, infoBox);
    applyShrinkablePolicy(compNameEdit);
    infoLayout->addRow("Component Name:", compNameEdit);
    connect(compNameEdit, &QLineEdit::editingFinished, this, [this, compNameEdit, capturedUid = comp->uid]() {
        Project* proj = Application::getInstance()->getProject();
        ComponentInstance* c = proj->getComponentByUid(capturedUid);
        if (!c) return;
        const QString oldName = c->name;
        const QString newName = compNameEdit->text().trimmed();
        if (newName.isEmpty()) { compNameEdit->setText(oldName); return; }   // a nameless component can't be told apart in the tree
        if (newName == oldName) return;

        auto setName = [capturedUid](const QString& n) {
            if (auto* inst = Application::getInstance()->getProject()->getComponentByUid(capturedUid))
                inst->name = n;
            // Value-only commands don't refresh the editor, and the scene tree shows the name.
            Application::getInstance()->getEditor()->scheduleRefresh();
        };
        pushCommand(
            [setName, newName]() { setName(newName); },
            [setName, oldName]() { setName(oldName); },
            "Rename component",
            QString(),
            false   // name is a label only; no sim reload needed
        );
    });
    infoLayout->addRow("Model ID:", createShrinkableLabel(comp->modelId, infoBox));
    infoLayout->addRow("UID:", createShrinkableLabel(QString::number(comp->uid), infoBox));
    mainLayout->addWidget(infoBox);

    // Controls come from kind-joint inputs, readouts from outputs with a numeric runtime
    // slot; image signals (camera outputs, display inputs) get their own group.
    bool hasControls = false;
    for (const InterfaceDef& def : comp->getBlueprint()->interfaceInputs) {
        if (def.target.kind == "joint") { hasControls = true; break; }
    }
    bool hasReadouts = false;
    for (auto it = comp->getBlueprint()->interfaceOutputs.constBegin(); it != comp->getBlueprint()->interfaceOutputs.constEnd(); ++it) {
        if (!comp->getSensorValue(it.key()).data.empty()) { hasReadouts = true; break; }
    }
    if (hasControls) {
        build_inputs(comp);
    }
    if (hasReadouts) {
        build_outputs(comp);
    }
    const bool hasImages = !comp->getBlueprint()->cameras.isEmpty() || !comp->getBlueprint()->displays.isEmpty();
    if (hasImages) {
        build_images(comp);
    }
    if (!comp->getBlueprint()->connectors.isEmpty()) {
        build_connectors(comp);
    }
    build_pins(comp);
    if (comp->getBlueprint()->power.present) build_power(comp);

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

    for (const QString& key : comp->getBlueprint()->interfaceInputs.keys()) {
        InterfaceDef def = comp->getBlueprint()->interfaceInputs[key];
        if (def.target.kind != "joint") continue;   // only joint inputs have a control (edit-mode posing)
        const QString jointId = def.target.id;

        // Domain is advisory: ranged -> slider + bounded spin box, unbounded -> spin box only.
        const bool ranged = def.domain.type == "ranged";

        QWidget* itemWidget = new QWidget(inputBox);
        QVBoxLayout* itemVBox = new QVBoxLayout(itemWidget);
        itemVBox->setContentsMargins(0, 0, 0, 8);

        QLabel* titleLabel = new QLabel(def.name + ":", itemWidget);
        titleLabel->setWordWrap(true);

        QWidget* targetWidget = new QWidget(itemWidget);
        QHBoxLayout* targetHBox = new QHBoxLayout(targetWidget);
        targetHBox->setContentsMargins(0, 0, 0, 0);

        BasicIOValue jointValue = comp->getJointValue(jointId);
        double currentVal = jointValue.data.empty() ? 0.0 : jointValue.data[0];

        QSlider* slider = nullptr;
        if (ranged) {
            slider = new QSlider(Qt::Horizontal, targetWidget);
            slider->setObjectName("inp_slider_" + jointId); // for updateJointValues()
            slider->setRange(static_cast<int>(def.domain.parameters.value("min")), static_cast<int>(def.domain.parameters.value("max")));
            slider->setValue(currentVal);
            slider->setFocusPolicy(Qt::StrongFocus);
            slider->installEventFilter(this);
        }

        QDoubleSpinBox* spinBox = new QDoubleSpinBox(targetWidget);
        spinBox->setObjectName("inp_spin_" + jointId); // for updateJointValues()
        if (ranged) {
            spinBox->setRange(def.domain.parameters.value("min"), def.domain.parameters.value("max"));
        } else {
            spinBox->setRange(-1e9, 1e9);   // QDoubleSpinBox defaults to 0..99.99
        }
        spinBox->setValue(currentVal);
        spinBox->setFocusPolicy(Qt::StrongFocus);
        spinBox->installEventFilter(this);

        if (slider) targetHBox->addWidget(slider);
        targetHBox->addWidget(spinBox);

        itemVBox->addWidget(titleLabel);
        itemVBox->addWidget(targetWidget);
        inputLayout->addWidget(itemWidget);

        rows.append({jointId, slider, spinBox});
    }
    mainLayout->addWidget(inputBox);

    for (const InputRow& row : rows) {
        if (row.slider) {
            connect(row.slider, &QSlider::valueChanged, row.spinBox, [spinBox = row.spinBox](int val) {
                spinBox->setValue(val);
            });
            connect(row.spinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), row.slider, [slider = row.slider](double val) {
                slider->setValue(val);
            });
        }

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
                        BasicIOValue v = c->getJointValue(jkey);
                        v.data[0] = newVal;
                        c->setJointValue(jkey, v);
                    }
                },
                [capturedUid, jkey, oldVal]() {
                    if (auto* c = Application::getInstance()->getProject()->getComponentByUid(capturedUid)) {
                        BasicIOValue v = c->getJointValue(jkey);
                        v.data[0] = oldVal;
                        c->setJointValue(jkey, v);
                    }
                },
                "Set " + jkey,
                mergeKey,
                false // joint targets sync live; no MJCF rebuild needed
            );
        };

        // Mouse drag: capture the pre-drag value, seal the session with a
        // unique merge key, and switch to live preview.
        if (slider) connect(slider, &QSlider::sliderPressed, this, [this, capturedUid, jkey]() {
            ComponentInstance* c = Application::getInstance()->getProject()->getComponentByUid(capturedUid);
            if (!c) return;
            dragStartValue = c->getJointValue(jkey).data[0];
            dragMergeKey = QString("joint:%1:%2:drag:%3").arg(capturedUid).arg(jkey).arg(++dragSessionCounter);
            dragInProgress = true;
        });

        if (slider) connect(slider, &QSlider::valueChanged, this,
                [this, capturedUid, jkey, slider, nudgeMergeKey, pushJointCommand](int val) {
            ComponentInstance* c = Application::getInstance()->getProject()->getComponentByUid(capturedUid);
            if (!c) return;
            if (slider->isSliderDown()) {
                // Live preview only -- the physics loop picks this up every frame;
                // the undoable command is committed once on sliderReleased.
                BasicIOValue v=c->getJointValue(jkey);
                v.data[0]=val;
                c->setJointValue(jkey, v);
            } else {
                // Keyboard/wheel step (or programmatic sync from the spinbox):
                // commit immediately, merged with neighbouring nudges.
                double oldVal = c->getJointValue(jkey).data[0];
                pushJointCommand(oldVal, static_cast<double>(val), nudgeMergeKey);
            }
        });

        // End of drag: commit a single command (old = pre-drag value) under the
        // drag's unique key, finalizing the session. Value-only commands don't
        // trigger an editor refresh -- the widgets already show the value --
        // so no rebuild happens unless one was suppressed mid-drag.
        if (slider) connect(slider, &QSlider::sliderReleased, this, [this, slider, capturedUid, jkey, pushJointCommand]() {
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
            double oldVal = activeComp->getJointValue(jkey).data[0];
            pushJointCommand(oldVal, spinBox->value(), nudgeMergeKey);
        });
    }
}


// Text for a readout: one value, or all components comma-separated for vectors.
// Empty data (signal without a runtime slot) gives an empty string.
static QString formatReadout(const BasicIOValue& data, bool singleFull) {
    if (data.data.empty()) return QString();
    if (data.data.size() == 1) return singleFull ? QString::number(data.data[0]) : QString::number(data.data[0], 'f', 2);
    QStringList parts;
    for (double v : data.data) parts << QString::number(v, 'f', 2);
    return parts.join(", ");
}


// Outputs, then the part's internal state in a group of its own.
void InspectorPanel::build_outputs(ComponentInstance* comp) {
    auto buildGroup = [&](const QString& title, bool internal) {
        QGroupBox* box = nullptr;
        QFormLayout* form = nullptr;
        for (const QString& key : comp->getBlueprint()->interfaceOutputs.keys()) {
            const InterfaceDef& def = comp->getBlueprint()->interfaceOutputs[key];
            if (def.internal != internal) continue;
            BasicIOValue data = comp->getSensorValue(key);
            if (data.data.empty()) continue;   // no numeric slot (camera: see build_images)
            if (!box) {
                box = new QGroupBox(title, this);
                form = new QFormLayout(box);
            }
            QLineEdit* valueLabel = createShrinkableLabel("", box);
            valueLabel->setObjectName("lbl_out_" + key);
            valueLabel->setText(formatReadout(data, true));
            form->addRow(def.name + (def.unit.isEmpty() ? QString() : " (" + def.unit + ")") + ":", valueLabel);
        }
        if (box) mainLayout->addWidget(box);
    };
    buildGroup("Sensor Outputs", false);
    buildGroup("Internal (simulation-only)", true);
}


// Status line of an image signal: size, format, newest frame.
static QString formatImageReadout(const ImageIOValue& img) {
    QString text = QString("%1x%2 %3").arg(img.width).arg(img.height)
                       .arg(img.format == ImageFormat::Depth32F ? "depth" : "rgb");
    if (!img.pixels) return text + " · no frame";
    return text + QString(" · frame %1 · t=%2 s").arg(img.seq).arg(img.time, 0, 'f', 2);
}


// Image signals: camera outputs and display inputs. View the pixels with an
// Image canvas in the plot panel.
void InspectorPanel::build_images(ComponentInstance* comp) {
    QGroupBox* imageBox = new QGroupBox("Cameras & Displays", this);
    QFormLayout* imageLayout = new QFormLayout(imageBox);
    ComponentBlueprint* bp = comp->getBlueprint();

    auto addRows = [&](const QMap<QString, InterfaceDef>& defs, const char* kind) {
        for (auto it = defs.constBegin(); it != defs.constEnd(); ++it) {
            if (it.value().target.kind != kind) continue;
            QLineEdit* valueLabel = createShrinkableLabel("", imageBox);
            valueLabel->setObjectName("lbl_img_" + it.key());
            valueLabel->setText(formatImageReadout(comp->getImageValue(it.key())));
            imageLayout->addRow(it.value().name + ":", valueLabel);
        }
    };
    addRows(bp->interfaceOutputs, "camera");
    addRows(bp->interfaceInputs, "display");
    mainLayout->addWidget(imageBox);
}


// The part's signal pins. Read-only. Prototype: the names the sketch uses
// (robot_pins.h; the numbers follow from the part's uid). Real: the board
// pins they are wired to (the Wiring page), which the sketch uses directly.
void InspectorPanel::build_pins(ComponentInstance* comp) {
    Project* project = Application::getInstance()->getProject();
    if (Project::isBoard(comp)) {
        build_board(comp);
        return;
    }
    const bool real = project->getLevel() == ProjectLevel::Real;
    const RealBoard board = real ? project->realBoard() : RealBoard();
    const QList<BoardPin> pins = real ? board.pinsOf(comp->uid) : project->idealBoard().pinsOf(comp->uid);
    if (pins.isEmpty() && !real) return;

    QGroupBox* box = new QGroupBox(real ? QString("Pins (wired to %1)").arg(board.isValid() ? board.instance()->name : "no board")
                                        : QString("Pins (ideal board)"), this);
    box->setToolTip(real ? "The board pins this part is wired to: use the number in the sketch, e.g. servo.attach(9)"
                         : "Use these names in the sketch, e.g. servo.attach(COMP_6_SIGNAL); they come from robot_pins.h");
    QFormLayout* form = new QFormLayout(box);
    for (const BoardPin& p : pins) {
        QString text;
        if (real) {
            text = QString("%1 %2 = pin %3").arg(board.instance()->name, p.boardPinId.toUpper()).arg(p.number);
        } else {
            text = QString("%1 = %2").arg(p.constant).arg(p.number);
            if (!p.alias.isEmpty()) text += QString("  (or %1)").arg(p.alias);
        }
        form->addRow(QString("%1 (%2):").arg(p.pinId, p.type), createShrinkableLabel(text, box));
    }
    if (real && pins.isEmpty()) {
        bool typed = false;
        for (const PinDef& p : comp->getBlueprint()->pins) typed = typed || p.isBoardPin();
        if (!typed) return;
        form->addRow(createShrinkableLabel("Not wired to the board: open the Wiring page (Ctrl+3).", box));
    }
    mainLayout->addWidget(box);
}


// A board: the sketch it runs at the Real level (its firmware slot) and what
// is wired to its pins.
void InspectorPanel::build_board(ComponentInstance* comp) {
    Project* project = Application::getInstance()->getProject();
    const BoardDef& def = comp->getBlueprint()->board;
    QGroupBox* box = new QGroupBox("Board", this);
    QFormLayout* form = new QFormLayout(box);

    if (project->getLevel() != ProjectLevel::Real) {
        form->addRow(createShrinkableLabel("Boards run their firmware at the Real level (make a Real copy of the project: "
                                           "project settings). At the Prototype level the ideal board runs the project's firmware.", box));
        mainLayout->addWidget(box);
        return;
    }

    QWidget* slot = new QWidget(box);
    QHBoxLayout* slotLayout = new QHBoxLayout(slot);
    slotLayout->setContentsMargins(0, 0, 0, 0);
    QComboBox* combo = new QComboBox(slot);
    fixComboBoxPolicy(combo);
    combo->addItem("(none)", QString());
    for (const QJsonValue& v : project->getScriptPaths()) {
        const QString path = v.toString();
        if (path.endsWith(".ino", Qt::CaseInsensitive) || path.endsWith(".cpp", Qt::CaseInsensitive)) combo->addItem(path, path);
    }
    combo->setCurrentIndex(std::max(0, combo->findData(comp->firmware)));
    combo->setToolTip("The sketch this board runs. It uses the board's own pins (9, A0, LED_BUILTIN), as on the real board.");
    QPushButton* openBtn = new QPushButton("Open", slot);
    openBtn->setEnabled(!comp->firmware.isEmpty());
    slotLayout->addWidget(combo, 1);
    slotLayout->addWidget(openBtn);
    form->addRow("Firmware:", slot);
    form->addRow("Compiles for:", createShrinkableLabel(def.fqbn, box));
    form->addRow("Logic:", createShrinkableLabel(QString("%1 V, ADC %2-bit").arg(def.logicVoltage).arg(def.adcBits), box));
    if (project->activeBoard() != comp)
        form->addRow(createShrinkableLabel("Idle: one board runs at a time until boards can talk to each other (UART).", box));

    const int uid = comp->uid;
    connect(combo, &QComboBox::currentIndexChanged, this, [combo, uid]() {
        Commands::setBoardFirmware(uid, combo->currentData().toString());
    });
    connect(openBtn, &QPushButton::clicked, this, [project, uid]() {
        ComponentInstance* b = project->getComponentByUid(uid);
        EditorWindow* editor = Application::getInstance()->getEditor();
        if (!b || b->firmware.isEmpty() || !editor->scriptPanel->confirmLeaveUnsaved()) return;
        editor->scriptPanel->loadScript(project->getProjectDirectory() + "/" + b->firmware);
        editor->showScriptPage();
    });
    mainLayout->addWidget(box);

    // What is on the board's pins: the numbers the sketch uses.
    const RealBoard rb = RealBoard::build(comp, project->getRootComponent(), project->netlist());
    QGroupBox* pinsBox = new QGroupBox("Pins", this);
    pinsBox->setToolTip("What is wired to the board's pins (the Wiring page). The sketch uses the pin numbers, "
                        "as on the real board: servo.attach(9), analogRead(A0)");
    QFormLayout* pinsForm = new QFormLayout(pinsBox);
    for (const BoardPin& p : rb.pins()) {
        const QString text = QString("%1 %2").arg(p.partName, p.pinId);
        pinsForm->addRow(QString("%1 (pin %2):").arg(p.boardPinId.toUpper()).arg(p.number), createShrinkableLabel(text, pinsBox));
    }
    if (rb.pins().isEmpty()) pinsForm->addRow(createShrinkableLabel("Nothing wired yet: open the Wiring page (Ctrl+3).", pinsBox));
    mainLayout->addWidget(pinsBox);
}


// Supplies, batteries and regulator modules: their per-instance settings
// (ComponentInstance::parameters, "power." keys), and what the part draws.
void InspectorPanel::build_power(ComponentInstance* comp) {
    Project* project = Application::getInstance()->getProject();
    const PowerDef& pw = comp->getBlueprint()->power;
    QGroupBox* box = new QGroupBox("Power", this);
    QFormLayout* form = new QFormLayout(box);
    if (project->getLevel() != ProjectLevel::Real)
        form->addRow(createShrinkableLabel("Used at the Real level; at the Prototype level power is ideal.", box));

    const int uid = comp->uid;
    // One undoable edit of a power setting; the wiring checks refresh with it.
    auto setParam = [uid](const char* key, const QVariant& value, const QString& text) {
        ComponentInstance* c = Application::getInstance()->getProject()->getComponentByUid(uid);
        if (!c) return;
        const bool had = c->parameters.contains(key);
        const QVariant old = c->parameters.value(key);
        if (had && old == value) return;
        const QString k = key;
        auto apply = [uid, k](bool set, const QVariant& v) {
            if (ComponentInstance* i = Application::getInstance()->getProject()->getComponentByUid(uid)) {
                if (set) i->parameters.insert(k, v); else i->parameters.remove(k);
            }
        };
        Application::getInstance()->getUndoStack()->push(new GenericCommand(
            [apply, value]() { apply(true, value); }, [apply, had, old]() { apply(had, old); }, text));
    };
    auto spin = [&](double value, double lo, double hi, double step, int decimals, const QString& suffix) {
        QDoubleSpinBox* s = new QDoubleSpinBox(box);
        s->setRange(lo, hi);
        s->setSingleStep(step);
        s->setDecimals(decimals);
        s->setSuffix(suffix);
        s->setValue(value);
        s->setKeyboardTracking(false);
        return s;
    };
    auto param = [comp](const char* key, double def) { return comp->parameters.value(key, def).toDouble(); };

    for (const SupplyDef& s : pw.supplies) {
        if (s.kind == "supply") {
            QDoubleSpinBox* volts = spin(param(PowerNetwork::kVoltage, s.voltage), 0.0, 30.0, 0.1, 2, " V");
            QDoubleSpinBox* limit = spin(param(PowerNetwork::kCurrentLimit, s.currentLimit), 0.0, 10.0, 0.1, 2, " A");
            limit->setToolTip("Over this current the supply drops its voltage (constant current); 0 = no limit");
            form->addRow("Voltage:", volts);
            form->addRow("Current limit:", limit);
            connect(volts, &QDoubleSpinBox::editingFinished, this, [setParam, volts]() {
                setParam(PowerNetwork::kVoltage, volts->value(), "Set supply voltage");
            });
            connect(limit, &QDoubleSpinBox::editingFinished, this, [setParam, limit]() {
                setParam(PowerNetwork::kCurrentLimit, limit->value(), "Set supply current limit");
            });
        } else if (s.kind == "battery" && pw.battery.present) {
            const double pct = param(PowerNetwork::kChargePercent, 100.0);
            QDoubleSpinBox* charge = spin(pct, 0.0, 100.0, 5.0, 0, " %");
            const BatteryDef& bat = pw.battery;
            const BatteryDef::Point pt = bat.packAt(pct);
            const QString perCell = bat.cells > 1 ? QString(" (%1 V per cell)").arg(pt.voltage / bat.cells, 0, 'f', 2) : QString();
            charge->setToolTip(QString("As a fuel gauge reads it: 0 % is the rated cut-off. At this charge: %1 V open circuit%2, "
                                       "%3 Ω inside (%4 mAh rated)")
                                   .arg(pt.voltage, 0, 'f', 2).arg(perCell).arg(pt.resistance, 0, 'f', 3).arg(bat.capacityAh * 1000.0));
            QCheckBox* drains = new QCheckBox("Drains during the run", box);
            drains->setChecked(comp->parameters.value(PowerNetwork::kDrains, false).toBool());
            drains->setToolTip("On: the charge goes down with the current drawn (sim time), and the voltage with it. "
                               "Heavy loads empty it sooner, a rest lets it recover a little, and past 0 % its voltage "
                               "collapses. Every Play starts from the charge set here.");
            form->addRow("Charge:", charge);
            form->addRow(createShrinkableLabel(QString("%1 V open circuit at this charge%2").arg(pt.voltage, 0, 'f', 2).arg(perCell), box));
            form->addRow(drains);
            if (bat.protection.present) {
                QCheckBox* bms = new QCheckBox("Protection circuit (BMS)", box);
                bms->setChecked(comp->parameters.value(PowerNetwork::kProtection, bat.protection.fitted).toBool());
                bms->setToolTip(QString("On: below %1 V per cell at the terminals for %2 ms the pack cuts itself off "
                                        "(0 V) until Stop, as a protection board does. Off: a bare pack keeps discharging.")
                                    .arg(bat.protection.cutoffV, 0, 'f', 2).arg(bat.protection.delay * 1000.0, 0, 'f', 0));
                form->addRow(bms);
                connect(bms, &QCheckBox::toggled, this, [setParam](bool on) {
                    setParam(PowerNetwork::kProtection, on, on ? "Battery protection on" : "Battery protection off");
                });
            }
            connect(charge, &QDoubleSpinBox::editingFinished, this, [setParam, charge]() {
                setParam(PowerNetwork::kChargePercent, charge->value(), "Set battery charge");
            });
            connect(drains, &QCheckBox::toggled, this, [setParam](bool on) {
                setParam(PowerNetwork::kDrains, on, on ? "Battery drains" : "Battery doesn't drain");
            });
        }
    }
    for (const RegulatorDef& r : pw.regulators) {
        if (r.kind != "buck") continue;
        QDoubleSpinBox* out = spin(param(PowerNetwork::kVOut, r.vOut), 1.25, 35.0, 0.1, 2, " V");
        out->setToolTip(QString("The module's output (its trimmer); up to %1 A, needs %2 V above it at the input").arg(r.iMax).arg(r.dropout));
        form->addRow("Output:", out);
        connect(out, &QDoubleSpinBox::editingFinished, this, [setParam, out]() {
            setParam(PowerNetwork::kVOut, out->value(), "Set buck output voltage");
        });
    }
    for (const PowerLoadDef& l : pw.loads) {
        QString text = QString("%1 mA").arg(l.current * 1000.0, 0, 'g', 3);
        if (l.minV > 0.0) text += QString(", works from %1 V").arg(l.minV);
        form->addRow(QString("Draws (%1):").arg(l.pin.toUpper()), createShrinkableLabel(text, box));
    }
    if (form->rowCount() == 0) { delete box; return; }
    // L298N module: its ENA/ENB jumpers tie the enable pins to 5 V (fitted on a new module).
    const ComponentBlueprint* bpd = comp->getBlueprint();
    if (bpd->emulatorDef.type == "hbridge_emulator" && bpd->emulatorDef.parameters.value("truth_table").toString() == "l298") {
        for (const auto& [key, label] : {std::pair<const char*, const char*>{"driver.ena_jumper", "ENA jumper fitted"},
                                          {"driver.enb_jumper", "ENB jumper fitted"}}) {
            QCheckBox* jumper = new QCheckBox(label, box);
            jumper->setChecked(comp->parameters.value(key, true).toBool());
            jumper->setToolTip("Fitted: the enable pin is tied to the module's 5 V, so that channel is always on and "
                               "the IN pins alone drive it (both LOW = brake). Remove it to PWM the enable pin from the board.");
            form->addRow(jumper);
            const char* k = key;
            connect(jumper, &QCheckBox::toggled, this, [setParam, k](bool on) {
                setParam(k, on, on ? "Fit L298N jumper" : "Remove L298N jumper");
            });
        }
    }
    mainLayout->addWidget(box);
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

    for (const QString& connId : comp->getBlueprint()->connectors.keys()) {
        ConnectorRow row;
        row.connId = connId;
        row.def = comp->getBlueprint()->connectors[connId];
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
            populateAvailableComponents(row.childUidCombo);
            row.childUidCombo->addItem("-", -1);
            row.childUidCombo->setCurrentIndex(row.childUidCombo->count() - 1);
            grid->addWidget(row.childUidCombo, 1, 0, 1, 2);

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
                QString newSelfConn = newChild->getBlueprint()->connectors.isEmpty() ? "" : newChild->getBlueprint()->connectors.first().id;

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
                    dropBtn->setStyleSheet("border: 1px solid #4a8fe7;");
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

                QString newSelfConn = newChild->getBlueprint()->connectors.isEmpty() ? "" : newChild->getBlueprint()->connectors.first().id;
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

    QGroupBox* globalBox = new QGroupBox("Global Robot Settings", this);
    QFormLayout* globalLayout = new QFormLayout(globalBox);

    QLineEdit* nameEdit = new QLineEdit(meta["name"].toString(), globalBox);
    applyShrinkablePolicy(nameEdit);
    globalLayout->addRow("Robot Name:", nameEdit);

    // The level is the project's for good: Prototype (one ideal board runs the
    // firmware below, every part auto-wired) or Real (boards with their own
    // firmware, wired on the Wiring page). A Prototype project moves on by
    // making a Real copy; the Prototype project stays as it is.
    const bool realLevel = project->getLevel() == ProjectLevel::Real;
    auto* levelLabel = createShrinkableLabel(realLevel ? "Real (boards and wiring)" : "Prototype (ideal board)", globalBox);
    levelLabel->setToolTip(realLevel ? "Boards from the library run their own firmware; wire them on the Wiring page (Ctrl+3). "
                                       "The board's limits, bus speeds and power apply."
                                     : "An ideal controller with unlimited pins, wired automatically.");
    globalLayout->addRow("Level:", levelLabel);
    if (!realLevel) {
        QPushButton* copyBtn = new QPushButton("Create Real copy...", globalBox);
        copyBtn->setToolTip("A new project at the Real level, next to this one: the same robot and sketches, "
                            "no board yet. This project stays as it is.");
        globalLayout->addRow("", copyBtn);
        connect(copyBtn, &QPushButton::clicked, this, []() {
            if (EditorWindow* editor = Application::getInstance()->getEditor()) editor->createRealCopy();
        });
    }

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
    globalLayout->addRow(project->getLevel() == ProjectLevel::Real ? "Sketches:" : "Firmware:", scriptWidget);
    if (project->getLevel() == ProjectLevel::Real)
        scriptCombo->setToolTip("The ideal board's sketch (Prototype). At the Real level each board picks its own in its Inspector.");

    // Same seed, same run: noise, drops and per-unit variation all derive from it.
    QSpinBox* seedSpin = new QSpinBox(globalBox);
    seedSpin->setRange(0, std::numeric_limits<int>::max());
    seedSpin->setValue(project->getSimulationSeed());
    seedSpin->setToolTip("Seed of every random generator in a run. The same seed repeats a run exactly.");
    applyShrinkablePolicy(seedSpin);
    globalLayout->addRow("Random Seed:", seedSpin);

    // Live document state: projectData["script"] is only refreshed on save.
    int activeScriptIdx = project->getCurrentScriptIdx();
    QJsonArray allScriptsArr = project->getScriptPaths();

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

    connect(seedSpin, &QSpinBox::editingFinished, this, [this, project, seedSpin]() {
        int newSeed = seedSpin->value();
        int oldSeed = project->getSimulationSeed();
        if (newSeed == oldSeed) return;

        pushCommand(
            [project, newSeed]() { project->setSimulationSeed(newSeed); },
            [project, oldSeed]() { project->setSimulationSeed(oldSeed); },
            "Set random seed",
            QString(),
            false // read when Play starts; nothing to reload
        );
        Application::getInstance()->getEditor()->scheduleRefresh();
    });

    connect(scriptCombo, &QComboBox::currentTextChanged, this, [this, project, scriptCombo](const QString& text) {
        if (text.isEmpty()) return;
        int index = project->getScriptPaths().toVariantList().indexOf(text);
        if (index < 0 || index == project->getCurrentScriptIdx()) return;

        // Switching shows the script in the editor: don't drop its pending edits.
        ScriptPanel* panel = Application::getInstance()->getEditor()->scriptPanel;
        if (panel && !panel->confirmLeaveUnsaved()) {
            QSignalBlocker block(scriptCombo);
            scriptCombo->setCurrentIndex(project->getCurrentScriptIdx());
            return;
        }

        Commands::setMcuScripts(project->getScriptPaths(), index, "Set active script");
        if (panel) panel->loadScript(project->getProjectDirectory() + "/" + text);
    });

    // Same dialog as the script panel's "+": firmware sketch, JS script or stage script.
    connect(btnAddScript, &QPushButton::clicked, this, []() {
        if (ScriptPanel* panel = Application::getInstance()->getEditor()->scriptPanel) panel->createScript();
    });

    connect(btnDelScript, &QPushButton::clicked, this, [this, project, scriptCombo]() {
        QString toDelete = scriptCombo->currentText();
        if (toDelete.isEmpty()) return;

        // A sketch in its own folder goes with its .h / .cpp files.
        const QString fullPath = project->getProjectDirectory() + "/" + toDelete;
        const bool folder = Project::inSketchFolder(toDelete);
        const QDir sketchDir = QFileInfo(fullPath).dir();
        QStringList files{fullPath};
        if (folder) {
            files.clear();
            for (const QString& f : sketchDir.entryList(QDir::Files)) files << sketchDir.filePath(f);
        }
        const QString what = folder ? QString("the sketch %1/ (%2 file%3)").arg(QFileInfo(toDelete).path()).arg(files.size()).arg(files.size() == 1 ? "" : "s")
                                    : toDelete;
        if (QMessageBox::question(this, "Delete Script", "Delete " + what + " from disk?",
                                   QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
            return;
        }

        // Undo writes the files back, so keep their content.
        QList<QPair<QString, QByteArray>> contents;
        for (const QString& f : files) {
            QFile file(f);
            if (file.open(QIODevice::ReadOnly)) contents.append({f, file.readAll()});
        }

        QJsonArray newPaths;
        for (const QJsonValue& val : project->getScriptPaths()) {
            if (val.toString() != toDelete) newPaths.append(val);
        }
        int newActiveIndex = newPaths.isEmpty() ? -1 : 0;

        const QString dirPath = sketchDir.absolutePath();
        Commands::setMcuScripts(newPaths, newActiveIndex, "Delete script",
            [contents, folder, dirPath]() {
                for (const auto& c : contents) QFile::remove(c.first);
                if (folder) QDir().rmdir(dirPath);   // only if empty
            },
            [contents, dirPath]() {
                QDir().mkpath(dirPath);
                for (const auto& c : contents) {
                    QFile file(c.first);
                    if (file.open(QIODevice::WriteOnly)) file.write(c.second);
                }
            });

        // Only replace the editor's file if it was the deleted one.
        ScriptPanel* panel = Application::getInstance()->getEditor()->scriptPanel;
        if (panel && QDir::cleanPath(panel->currentPath()) == QDir::cleanPath(fullPath)) {
            if (newActiveIndex >= 0)
                panel->loadScript(project->getProjectDirectory() + "/" + newPaths[newActiveIndex].toString());
            else
                panel->unloadScript();
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
            QString newSelfConn = "";
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
                    newChild->snapAngle = carriedSnapAngle;
                    newChild->selfConnector = newSelfConn;
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
                dropBtn->setStyleSheet("border: 1px solid #4a8fe7;");
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

            QString newSelfConn = "";
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
                [project, newRot]() { project->setRootRotation(newRot); },
                [project, oldRot]() { project->setRootRotation(oldRot); },
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
    if (!comp || !comp->getBlueprint()) return;

    for (const InterfaceDef& def : comp->getBlueprint()->interfaceInputs) {
        if (def.target.kind != "joint") continue;
        QString jkey = def.target.id;
        BasicIOValue jointValue = comp->getJointValue(jkey);
        if (jointValue.data.empty()) continue;
        double val = jointValue.data[0];

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
        for (const QString& key : comp->getBlueprint()->interfaceOutputs.keys()) {
            if (QLineEdit* label = this->findChild<QLineEdit*>("lbl_out_" + key)) {
                BasicIOValue data = comp->getSensorValue(key);
                if (!data.data.empty()) label->setText(formatReadout(data, false));
                label->setCursorPosition(0);
            }
            if (QLineEdit* label = this->findChild<QLineEdit*>("lbl_img_" + key)) {
                label->setText(formatImageReadout(comp->getImageValue(key)));
                label->setCursorPosition(0);
            }
        }
        for (const QString& key : comp->getBlueprint()->interfaceInputs.keys()) {
            if (QLineEdit* label = this->findChild<QLineEdit*>("lbl_img_" + key)) {
                label->setText(formatImageReadout(comp->getImageValue(key)));
                label->setCursorPosition(0);
            }
        }
    }
}
