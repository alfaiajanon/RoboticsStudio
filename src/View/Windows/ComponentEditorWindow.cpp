// src/View/Windows/ComponentEditorWindow.cpp
#include "ComponentEditorWindow.h"
#include "Document/Components/ComponentData.h"
#include "View/Panels/ComponentEditor/KeyValueListWidget.h"
#include "View/Widgets/Toast.h"
#include "Document/Components/LibraryManager.h"
#include "Utils/Spatial.h"
#include <QListWidget>
#include <QFileDialog>
#include <QToolButton>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QFile>
#include <QJsonDocument>
#include <QFileInfo>
#include <qlineedit.h>
#include <QSplitter>
#include <QTabWidget>
#include <QEvent>
#include <QWheelEvent>
#include <QCoreApplication>
#include <QAbstractSpinBox>
#include <QAbstractSlider>
#include <QSet>
#include <QSpinBox>
#include <functional>




// ---------------- QList<Edge> helpers ----------------
static Edge findEdge(const QList<Edge>& joints, const QString& id) {
    for (const Edge& e : joints) if (e.id == id) return e;
    return Edge();
}

static void upsertEdge(QList<Edge>& joints, const Edge& edge) {
    for (int i = 0; i < joints.size(); ++i) {
        if (joints[i].id == edge.id) { joints[i] = edge; return; }
    }
    joints.append(edge);
}

static void removeEdgeById(QList<Edge>& joints, const QString& id) {
    for (int i = 0; i < joints.size(); ++i) {
        if (joints[i].id == id) { joints.removeAt(i); return; }
    }
}




// ---------------- QVariant map <-> QJsonObject ----------------
static QJsonObject variantMapToJson(const QMap<QString, QVariant>& map) {
    QJsonObject obj;
    for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
        obj[it.key()] = QJsonValue::fromVariant(it.value());
    }
    return obj;
}

static QMap<QString, QVariant> jsonToVariantMap(const QJsonObject& obj) {
    QMap<QString, QVariant> map;
    for (const QString& key : obj.keys()) {
        map[key] = obj[key].toVariant();
    }
    return map;
}




// ---------------- comma-list <-> Position/Rotation/QList<double> ----------------
static QString posToString(const Position& p) {
    return QString("%1, %2, %3").arg(p.x).arg(p.y).arg(p.z);
}

static Position stringToPos(const QString& s, const Position& fallback) {
    QStringList parts = s.split(',', Qt::SkipEmptyParts);
    if (parts.size() != 3) return fallback;
    bool ok1 = false, ok2 = false, ok3 = false;
    double x = parts[0].trimmed().toDouble(&ok1);
    double y = parts[1].trimmed().toDouble(&ok2);
    double z = parts[2].trimmed().toDouble(&ok3);
    if (!(ok1 && ok2 && ok3)) return fallback;
    return Position(x, y, z);
}

static QString rotToString(const Rotation& r) {
    return QString("%1, %2, %3").arg(r.roll()).arg(r.pitch()).arg(r.yaw());
}

static Rotation stringToRot(const QString& s, const Rotation& fallback) {
    QStringList parts = s.split(',', Qt::SkipEmptyParts);
    if (parts.size() != 3) return fallback;
    bool ok1 = false, ok2 = false, ok3 = false;
    double roll = parts[0].trimmed().toDouble(&ok1);
    double pitch = parts[1].trimmed().toDouble(&ok2);
    double yaw = parts[2].trimmed().toDouble(&ok3);
    if (!(ok1 && ok2 && ok3)) return fallback;
    return Rotation(roll, pitch, yaw);
}

static QString doubleListToString(const QList<double>& list) {
    QStringList parts;
    for (double v : list) parts << QString::number(v);
    return parts.join(", ");
}

static QList<double> stringToDoubleList(const QString& s) {
    QList<double> result;
    for (const QString& part : s.split(',', Qt::SkipEmptyParts)) {
        bool ok = false;
        double v = part.trimmed().toDouble(&ok);
        if (ok) result.append(v);
    }
    return result;
}

// Hides/shows a QFormLayout row given its field widget -- the label lives in
// the layout, not in the field, so both have to be toggled together.
static void setFormFieldVisible(QWidget* field, bool visible) {
    if (QWidget* parent = field->parentWidget()) {
        if (auto* form = qobject_cast<QFormLayout*>(parent->layout())) {
            if (QWidget* label = form->labelForField(field)) label->setVisible(visible);
        }
    }
    field->setVisible(visible);
}




// ---------------- reference helpers (schema 2 targets) ----------------

// Every id a target of `kind` can point to: physical objects (joint, tendon,
// site, geom) or devices (actuator, sensor, camera, display).
static QStringList idsForKind(const ComponentData& d, const QString& kind) {
    QStringList ids;
    if (kind == "joint") { for (const Edge& e : d.joints) ids << e.id; }
    else if (kind == "tendon") { for (const TendonDef& t : d.tendons) ids << t.id; }
    else if (kind == "site") { for (const Node& n : d.bodies) for (const Site& s : n.sites) ids << s.id; }
    else if (kind == "geom") { for (const Node& n : d.bodies) for (const Geom& g : n.geoms) ids << g.id; }
    else if (kind == "actuator") { for (const ActuatorDef& a : d.actuators) ids << a.id; }
    else if (kind == "sensor") { for (const SensorDef& s : d.sensors) ids << s.id; }
    else if (kind == "camera") { for (const CameraDef& cam : d.cameras) ids << cam.id; }
    else if (kind == "display") { for (const DisplayDef& x : d.displays) ids << x.id; }
    return ids;
}

// Description of the first thing that references (kind, id) -- a device target,
// a tendon term or an interface signal -- or an empty string if nothing does.
// Used to refuse removals, like the joint/connector checks always did.
static QString findTargetUser(const ComponentData& d, const QString& kind, const QString& id) {
    for (const ActuatorDef& a : d.actuators) if (a.target.kind == kind && a.target.id == id) return "actuator '" + a.id + "'";
    for (const SensorDef& s : d.sensors) if (s.target.kind == kind && s.target.id == id) return "sensor '" + s.id + "'";
    for (const CameraDef& cam : d.cameras) if (cam.target.kind == kind && cam.target.id == id) return "camera '" + cam.id + "'";
    for (const DisplayDef& x : d.displays) if (x.target.kind == kind && x.target.id == id) return "display '" + x.id + "'";
    if (kind == "joint") {
        for (const TendonDef& t : d.tendons) for (const TendonTerm& term : t.terms) if (term.joint == id) return "tendon '" + t.id + "'";
    }
    for (const InterfaceDef& s : d.interfaceInputs) if (s.target.kind == kind && s.target.id == id) return "input signal '" + s.name + "'";
    for (const InterfaceDef& s : d.interfaceOutputs) if (s.target.kind == kind && s.target.id == id) return "output signal '" + s.name + "'";
    return QString();
}

static bool deviceIdTaken(const ComponentData& d, const QString& id) {
    for (const QString& kind : {"actuator", "sensor", "camera", "display"}) {
        if (idsForKind(d, kind).contains(id)) return true;
    }
    return false;
}

static QString uniqueId(const QString& base, const std::function<bool(const QString&)>& taken) {
    int n = 1;
    QString id = QString("%1_%2").arg(base).arg(n);
    while (taken(id)) id = QString("%1_%2").arg(base).arg(++n);
    return id;
}

static QString resolutionToString(const QPair<int, int>& r) {
    return (r.first > 0 && r.second > 0) ? QString("%1, %2").arg(r.first).arg(r.second) : QString();
}

static QPair<int, int> stringToResolution(const QString& s, const QPair<int, int>& fallback) {
    QList<double> v = stringToDoubleList(s);
    if (v.size() != 2 || v[0] < 1 || v[1] < 1) return fallback;
    return qMakePair(static_cast<int>(v[0]), static_cast<int>(v[1]));
}




ComponentEditorWindow::ComponentEditorWindow(ComponentData source, QWidget* parent)
    : QWidget(parent, Qt::Window) {

    data = source;

    if(!data.modelId.isEmpty()){
        lockedComponent = true;
    }else{
        data.modelId = "new_component";
        data.meta.name = "New Component";
    }

    setWindowTitle("Component Editor");
    resize(1100, 720);

    QVBoxLayout* rootLayout = new QVBoxLayout(this);

    QSplitter* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);
    rootLayout->addWidget(splitter, 1);

    tabWidget = new QTabWidget(splitter);
    tabWidget->setTabPosition(QTabWidget::West);
    tabWidget->setMinimumWidth(360);

    auto makeTabPage = [this](QVBoxLayout*& outLayout) -> QScrollArea* {
        QScrollArea* scrollArea = new QScrollArea(tabWidget);
        scrollArea->setWidgetResizable(true);
        scrollArea->setFrameShape(QFrame::NoFrame);

        QWidget* content = new QWidget(scrollArea);
        outLayout = new QVBoxLayout(content);
        outLayout->setAlignment(Qt::AlignTop);
        outLayout->setSpacing(18);
        outLayout->setContentsMargins(14, 14, 14, 14);

        scrollArea->setWidget(content);
        return scrollArea;
    };

    tabWidget->addTab(makeTabPage(metaLayout), "Meta");
    tabWidget->addTab(makeTabPage(constructionLayout), "Construction");
    tabWidget->addTab(makeTabPage(devicesLayout), "Devices");
    tabWidget->addTab(makeTabPage(interfaceLayout), "Interface");
    tabWidget->addTab(makeTabPage(emulatorLayout), "Emulator");

    splitter->addWidget(tabWidget);

    preview = new ComponentPreviewViewport(splitter);
    splitter->addWidget(preview);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({420, 680});

    previewReloadTimer = new QTimer(this);
    previewReloadTimer->setSingleShot(true);
    previewReloadTimer->setInterval(500);
    connect(previewReloadTimer, &QTimer::timeout, this, [this]() {
        preview->loadComponentData(data);
    });

    QWidget* bottomBar = new QWidget(this);
    QHBoxLayout* bottomLayout = new QHBoxLayout(bottomBar);
    bottomLayout->addStretch();
    QPushButton* saveBtn = new QPushButton(lockedComponent? "Save":"Create", bottomBar);
    saveBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    bottomLayout->addWidget(saveBtn);
    rootLayout->addWidget(bottomBar);
    connect(saveBtn, &QPushButton::clicked, this, &ComponentEditorWindow::onSaveClicked);

    clearAndRebuild();
}




void ComponentEditorWindow::onSaveClicked() {
    bool ok = LibraryManager::getInstance().saveLocalComponent(data);
    if (ok) {
        Toast::showMessage(this, "Saved: " + data.modelId);
    } else {
        Toast::showMessage(this, "Save failed -- check that Id is set and not already in use.");
    }
}




/*
 * The rebuild is DEFERRED to the next event-loop turn, never run inline.
 * Nearly every caller is a lambda connected to a widget signal (a checkbox's
 * toggled, a line edit's editingFinished, a button's clicked) and the rebuild
 * deletes every widget -- including the one whose signal is still being
 * emitted. Qt keeps touching that widget after the signal returns
 * (QCheckBox::nextCheckState writes to its private data right after
 * toggled), so deleting it synchronously is a use-after-free crash.
 */
void ComponentEditorWindow::clearAndRebuild() {
    QTimer::singleShot(0, this, [this]() {
        auto clearLayout = [](QVBoxLayout* l) {
            QLayoutItem* item;
            while ((item = l->takeAt(0)) != nullptr) {
                if (item->widget()) delete item->widget();
                delete item;
            }
        };
        clearLayout(metaLayout);
        clearLayout(constructionLayout);
        clearLayout(devicesLayout);
        clearLayout(interfaceLayout);
        clearLayout(emulatorLayout);

        leftLayout = constructionLayout;
        build_resources();
        build_construction();
        build_connectors();
        constructionLayout->addStretch();

        leftLayout = devicesLayout;
        build_devices();
        devicesLayout->addStretch();

        leftLayout = interfaceLayout;
        build_interface();
        interfaceLayout->addStretch();

        leftLayout = emulatorLayout;
        build_emulator();
        emulatorLayout->addStretch();

        leftLayout = metaLayout;
        build_data();
        metaLayout->addStretch();

        // Scoped to the tab panel: the 3D preview's own HUD dropdown must keep
        // behaving normally.
        installScrollGuards(tabWidget);

        schedulePreviewReload();
    });
}




/*
 * Wheel events over these widgets never change their value -- they are
 * handed to the enclosing scroll area instead, so scrolling a long property
 * list can't silently edit whatever the cursor happens to pass over.
 *
 * (The earlier version only blocked the wheel while the widget was NOT
 * focused. That check could never be true: with Qt's default WheelFocus
 * policy, Qt gives the widget focus on the wheel event itself, before any
 * event filter runs -- so hasFocus() was always already true.)
 */
bool ComponentEditorWindow::eventFilter(QObject* obj, QEvent* event) {
    if (event->type() == QEvent::Wheel) {
        if (QWidget* widget = qobject_cast<QWidget*>(obj)) {
            event->ignore();
            if (widget->parentWidget()) {
                QCoreApplication::sendEvent(widget->parentWidget(), event);
            }
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}




void ComponentEditorWindow::installScrollGuards(QWidget* root) {
    auto guard = [this](QWidget* w) {
        // Tab/click focus only, so a wheel tick can't steal focus either.
        w->setFocusPolicy(Qt::StrongFocus);
        w->installEventFilter(this);
    };
    for (QComboBox* cb : root->findChildren<QComboBox*>()) guard(cb);
    for (QAbstractSpinBox* sb : root->findChildren<QAbstractSpinBox*>()) guard(sb); // spin + double spin
    for (QAbstractSlider* sl : root->findChildren<QAbstractSlider*>()) guard(sl);
}




void ComponentEditorWindow::schedulePreviewReload() {
    if (preview) preview->setOverlayData(data);
    if (previewReloadTimer) previewReloadTimer->start();
}




#pragma region shared helpers

ComponentEditorWindow::TransformFieldRefs ComponentEditorWindow::addTransformFields(QFormLayout* form, const Transform& t) {
    QLineEdit* posEdit = new QLineEdit(posToString(t.position), form->parentWidget());
    posEdit->setPlaceholderText("x, y, z (meters)");
    form->addRow("Position:", posEdit);

    QLineEdit* rotEdit = new QLineEdit(rotToString(t.rotation), form->parentWidget());
    rotEdit->setPlaceholderText("roll, pitch, yaw (degrees)");
    form->addRow("Rotation:", rotEdit);

    return {posEdit, rotEdit};
}




QWidget* ComponentEditorWindow::makeCollapsible(const QString& headerText, QWidget* content, QWidget* parent) {
    QWidget* wrapper = new QWidget(parent);
    QVBoxLayout* wrapperLayout = new QVBoxLayout(wrapper);
    wrapperLayout->setContentsMargins(0, 0, 0, 0);
    wrapperLayout->setSpacing(2);

    // Fold state lives in foldedSections, not in the widgets: every rebuild
    // recreates them, so a widget-only state would snap back on any edit.
    // Everything starts unfolded; only sections the USER folded are recorded.
    // Callers identify a section by setting a "foldKey" property on `content`.
    const QString foldKey = content->property("foldKey").toString();
    const bool startFolded = !foldKey.isEmpty() && foldedSections.contains(foldKey);

    QToolButton* toggle = new QToolButton(wrapper);
    toggle->setText((startFolded ? "▸ " : "▾ ") + headerText);
    toggle->setCheckable(true);
    toggle->setChecked(!startFolded);
    toggle->setStyleSheet("border: none; text-align: left; font-weight: bold;");
    toggle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    wrapperLayout->addWidget(toggle);

    content->setParent(wrapper);
    content->setVisible(!startFolded);
    wrapperLayout->addWidget(content);

    connect(toggle, &QToolButton::toggled, this, [this, toggle, content, headerText, foldKey](bool checked) {
        content->setVisible(checked);
        toggle->setText((checked ? "▾ " : "▸ ") + headerText);
        if (!foldKey.isEmpty()) {
            if (checked) foldedSections.remove(foldKey);
            else foldedSections.insert(foldKey);
        }
    });

    return wrapper;
}




#pragma region resources

void ComponentEditorWindow::build_resources() {
    QGroupBox* box = new QGroupBox("Resources", this);
    QVBoxLayout* layout = new QVBoxLayout(box);

    build_resource_list(layout, "Meshes", data.meshResources);
    build_resource_list(layout, "Materials", data.materialResources);

    leftLayout->addWidget(box);
}




void ComponentEditorWindow::build_resource_list(QVBoxLayout* parent, const QString& title, QMap<QString, QString>& target) {
    QGroupBox* box = new QGroupBox(title, parent->parentWidget());
    QVBoxLayout* layout = new QVBoxLayout(box);

    for (const QString& key : target.keys()) {
        QFrame* row = new QFrame(box);
        row->setFrameShape(QFrame::StyledPanel);
        QHBoxLayout* rowLayout = new QHBoxLayout(row);

        QLineEdit* keyEdit = new QLineEdit(key, row);
        keyEdit->setPlaceholderText("key (referenced by Geom.mesh/material)");
        rowLayout->addWidget(keyEdit, 1);

        QLabel* pathLabel = new QLabel(QFileInfo(target[key]).fileName(), row);
        pathLabel->setStyleSheet("color: #AAA;");
        rowLayout->addWidget(pathLabel, 2);

        QPushButton* removeBtn = new QPushButton("✖", row);
        removeBtn->setFixedSize(20, 20);
        rowLayout->addWidget(removeBtn);

        layout->addWidget(row);

        QString originalKey = key;
        connect(keyEdit, &QLineEdit::editingFinished, this, [this, &target, originalKey, keyEdit]() {
            QString newKey = keyEdit->text().trimmed();
            if (newKey.isEmpty() || newKey == originalKey) return;
            target[newKey] = target.take(originalKey);
            clearAndRebuild();
        });
        connect(removeBtn, &QPushButton::clicked, this, [this, &target, originalKey]() {
            target.remove(originalKey);
            clearAndRebuild();
        });
    }

    QPushButton* addBtn = new QPushButton("+ Import...", box);
    addBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    layout->addWidget(addBtn, 0, Qt::AlignLeft);

    QString filter = (title == "Meshes") ? "Meshes (*.obj)" : "Images (*.png *.jpg)";
    connect(addBtn, &QPushButton::clicked, this, [this, &target, filter]() {
        QString path = QFileDialog::getOpenFileName(this, "Import", QString(), filter,
                                                    nullptr, QFileDialog::DontUseNativeDialog);
        if (path.isEmpty()) return;
        QString key = QFileInfo(path).completeBaseName();
        target[key] = path;
        clearAndRebuild();
    });

    parent->addWidget(box);
}




#pragma region construction

void ComponentEditorWindow::build_construction() {
    QGroupBox* box = new QGroupBox("Construction", this);
    QVBoxLayout* layout = new QVBoxLayout(box);

    QWidget* defaultBodyRow = new QWidget(box);
    QHBoxLayout* defaultBodyLayout = new QHBoxLayout(defaultBodyRow);
    defaultBodyLayout->setContentsMargins(0, 0, 0, 0);
    QLabel* defaultBodyLabel = new QLabel("Default body:", defaultBodyRow);
    QComboBox* defaultBodyCombo = new QComboBox(defaultBodyRow);
    refreshBodyDropdown(defaultBodyCombo);
    defaultBodyCombo->setCurrentText(data.defaultBodyId);
    defaultBodyLayout->addWidget(defaultBodyLabel);
    defaultBodyLayout->addWidget(defaultBodyCombo, 1);
    layout->addWidget(defaultBodyRow);
    connect(defaultBodyCombo, &QComboBox::currentTextChanged, this, [this](const QString& id) {
        data.defaultBodyId = id;
        schedulePreviewReload();
    });

    build_bodies(layout);
    build_joints(layout);
    build_tendons(layout);

    leftLayout->addWidget(box);
}




void ComponentEditorWindow::build_bodies(QVBoxLayout* parent) {
    QGroupBox* box = new QGroupBox("Bodies", parent->parentWidget());
    QVBoxLayout* layout = new QVBoxLayout(box);

    for (const QString& nodeId : data.bodies.keys()) {
        const Node& node = data.bodies[nodeId];
        bodyGeomOverride[nodeId] = node.overrideGeom;   // the data is the source of truth

        QFrame* content = new QFrame(box);
        content->setFrameShape(QFrame::StyledPanel);
        QFormLayout* form = new QFormLayout(content);

        QLineEdit* idEdit = new QLineEdit(node.id, content);
        form->addRow("Id:", idEdit);

        QString oldNodeId = nodeId;
        connect(idEdit, &QLineEdit::editingFinished, this, [this, oldNodeId, idEdit]() {
            QString newId = idEdit->text().trimmed();
            if (newId.isEmpty() || newId == oldNodeId || data.bodies.contains(newId)) {
                idEdit->setText(oldNodeId);
                return;
            }
            Node n = data.bodies.take(oldNodeId);
            n.id = newId;
            data.bodies.insert(newId, n);

            if (data.defaultBodyId == oldNodeId) data.defaultBodyId = newId;

            // Migrate override flag key
            if (bodyGeomOverride.contains(oldNodeId)) {
                bodyGeomOverride.insert(newId, bodyGeomOverride.take(oldNodeId));
            }
            // Keep the user's fold choice attached to the renamed body
            if (foldedSections.remove("body:" + oldNodeId)) foldedSections.insert("body:" + newId);
            clearAndRebuild();
        });

        // One shared switch decides who owns mass/inertia for this body:
        //   checked   -> the body's own mass/inertia fields are live, and the
        //                per-geom mass fields are hidden;
        //   unchecked -> the geoms carry the mass, and the body's fields are
        //                greyed out.
        const bool bodyOwnsMass = bodyGeomOverride.value(nodeId, false);

        QWidget* massRow = new QWidget(content);
        QHBoxLayout* massLayout = new QHBoxLayout(massRow);
        massLayout->setContentsMargins(0, 0, 0, 0);

        QDoubleSpinBox* massSpin = new QDoubleSpinBox(massRow);
        massSpin->setRange(0.0, 1000.0);
        massSpin->setDecimals(4);
        massSpin->setValue(node.mass);
        massSpin->setEnabled(bodyOwnsMass);
        massLayout->addWidget(massSpin, 1);

        QCheckBox* overrideGeomsCheck = new QCheckBox("Override", massRow);
        overrideGeomsCheck->setChecked(bodyOwnsMass); // before the connect below, so nothing fires at build time
        massLayout->addWidget(overrideGeomsCheck);

        form->addRow("Mass (kg):", massRow);

        QDoubleSpinBox* inertiaSpin = new QDoubleSpinBox(content);
        inertiaSpin->setRange(0.0, 1000.0);
        inertiaSpin->setDecimals(6);
        inertiaSpin->setValue(node.inertia);
        inertiaSpin->setEnabled(bodyOwnsMass);
        form->addRow("Inertia:", inertiaSpin);

        connect(massSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, nodeId](double val) {
            if (data.bodies.contains(nodeId)) data.bodies[nodeId].mass = val;
            schedulePreviewReload();
        });

        connect(inertiaSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, nodeId](double val) {
            if (data.bodies.contains(nodeId)) data.bodies[nodeId].inertia = val;
            schedulePreviewReload();
        });

        // Applied in place -- no rebuild. The geoms' mass fields are tagged
        // "geomMassInertia" in build_geoms, so they can be found and hidden or
        // shown right here, on the fly.
        connect(overrideGeomsCheck, &QCheckBox::toggled, this, [this, nodeId, content, massSpin, inertiaSpin](bool checked) {
            data.bodies[nodeId].overrideGeom=checked;
            bodyGeomOverride[nodeId] = checked;
            massSpin->setEnabled(checked);
            inertiaSpin->setEnabled(checked);
            for (QWidget* w : content->findChildren<QWidget*>("geomMassInertia")) {
                setFormFieldVisible(w, !checked);
            }
        });

        TransformFieldRefs xform = addTransformFields(form, node.localTransform);
        connect(xform.posEdit, &QLineEdit::editingFinished, this, [this, nodeId, xform]() {
            if (!data.bodies.contains(nodeId)) return;
            data.bodies[nodeId].localTransform.position =
                stringToPos(xform.posEdit->text(), data.bodies[nodeId].localTransform.position);
            schedulePreviewReload();
        });
        connect(xform.rotEdit, &QLineEdit::editingFinished, this, [this, nodeId, xform]() {
            if (!data.bodies.contains(nodeId)) return;
            data.bodies[nodeId].localTransform.rotation =
                stringToRot(xform.rotEdit->text(), data.bodies[nodeId].localTransform.rotation);
            schedulePreviewReload();
        });

        build_geoms(form, nodeId);
        build_sites(form, nodeId);

        QPushButton* removeBtn = new QPushButton("Remove Body", content);
        form->addRow(removeBtn);
        connect(removeBtn, &QPushButton::clicked, this, [this, nodeId]() {
            for (const Edge& e : data.joints) {
                if (e.bodyA == nodeId || e.bodyB == nodeId) {
                    Toast::showMessage(this, "Can't remove: joint '" + e.id + "' references this body.");
                    return;
                }
            }
            for (const QString& key : data.connectors.keys()) {
                if (data.connectors[key].body == nodeId) {
                    Toast::showMessage(this, "Can't remove: connector '" + key + "' references this body.");
                    return;
                }
            }
            for (const Site& s : data.bodies.value(nodeId).sites) {
                QString user = findTargetUser(data, "site", s.id);
                if (!user.isEmpty()) {
                    Toast::showMessage(this, "Can't remove: " + user + " targets site '" + s.id + "' on this body.");
                    return;
                }
            }
            for (const Geom& g : data.bodies.value(nodeId).geoms) {
                QString user = findTargetUser(data, "geom", g.id);
                if (!user.isEmpty()) {
                    Toast::showMessage(this, "Can't remove: " + user + " targets geom '" + g.id + "' on this body.");
                    return;
                }
            }
            data.bodies.remove(nodeId);
            bodyGeomOverride.remove(nodeId);
            clearAndRebuild();
        });

        double totalMass = node.mass;
        if (!node.overrideGeom) { totalMass = 0.0; for (const Geom& g : node.geoms) totalMass += g.mass; }
        QString header = QString("%1  (mass %2 kg)").arg(node.id).arg(totalMass);
        content->setProperty("foldKey", "body:" + nodeId);
        layout->addWidget(makeCollapsible(header, content, box));
    }

    QPushButton* addBtn = new QPushButton("+ Add Body", box);
    addBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    layout->addWidget(addBtn, 0, Qt::AlignLeft);

    connect(addBtn, &QPushButton::clicked, this, [this]() {
        Node n;
        n.id = QString("body_%1").arg(data.bodies.size() + 1);
        data.bodies[n.id] = n;
        if (data.defaultBodyId.isEmpty()) data.defaultBodyId = n.id;
        clearAndRebuild();
    });

    parent->addWidget(box);
}




void ComponentEditorWindow::build_geoms(QFormLayout* bodyForm, const QString& bodyId) {
    QGroupBox* box = new QGroupBox("Geoms", bodyForm->parentWidget());
    QVBoxLayout* layout = new QVBoxLayout(box);

    const QList<Geom>& geoms = data.bodies.value(bodyId).geoms;
    for (int i = 0; i < geoms.size(); ++i) {
        const Geom& g = geoms[i];
        int geomIdx = i;

        QFrame* row = new QFrame(box);
        row->setFrameShape(QFrame::StyledPanel);
        QFormLayout* form = new QFormLayout(row);

        QLineEdit* geomIdEdit = new QLineEdit(g.id, row);
        form->addRow("Id:", geomIdEdit);
        QString oldGeomId = g.id;
        connect(geomIdEdit, &QLineEdit::editingFinished, this, [this, bodyId, geomIdx, oldGeomId, geomIdEdit]() {
            QString newId = geomIdEdit->text().trimmed();
            if (newId.isEmpty() || newId == oldGeomId || idsForKind(data, "geom").contains(newId)) {
                geomIdEdit->setText(oldGeomId);
                return;
            }
            if (!data.bodies.contains(bodyId) || geomIdx >= data.bodies[bodyId].geoms.size()) return;
            data.bodies[bodyId].geoms[geomIdx].id = newId;   // references are not rewritten (renames never cascade)
            clearAndRebuild();
        });

        QComboBox* typeCombo = new QComboBox(row);
        typeCombo->addItems({"mesh", "box", "sphere", "capsule", "cylinder"});
        typeCombo->setCurrentText(g.type);
        form->addRow("Type:", typeCombo);

        QComboBox* meshCombo = new QComboBox(row);
        meshCombo->addItem("(none)");
        for (const QString& key : data.meshResources.keys()) meshCombo->addItem(key);
        meshCombo->setCurrentText(g.mesh.isEmpty() ? "(none)" : g.mesh);
        form->addRow("Mesh:", meshCombo);

        QComboBox* materialCombo = new QComboBox(row);
        materialCombo->addItem("(none)");
        for (const QString& key : data.materialResources.keys()) materialCombo->addItem(key);
        materialCombo->setCurrentText(g.material.isEmpty() ? "(none)" : g.material);
        form->addRow("Material:", materialCombo);

        QDoubleSpinBox* gMassSpin = new QDoubleSpinBox(row);
        gMassSpin->setRange(0.0, 1000.0);
        gMassSpin->setDecimals(4);
        gMassSpin->setValue(g.mass);
        gMassSpin->setObjectName("geomMassInertia"); // found by the body's Override checkbox
        form->addRow("Mass (kg):", gMassSpin);
        if (bodyGeomOverride.value(bodyId, false)) setFormFieldVisible(gMassSpin, false);

        // QLineEdit* gInertiaEdit = new QLineEdit(g.intertia, row);
        // gInertiaEdit->setPlaceholderText("diaginertia (3 values)");
        // form->addRow("Inertia:", gInertiaEdit);

        // if (bodyGeomOverride.value(bodyId, false)) {
        //     form->labelForField(gMassSpin)->setVisible(false);
        //     gMassSpin->setVisible(false);
        //     form->labelForField(gInertiaEdit)->setVisible(false);
        //     gInertiaEdit->setVisible(false);
        // }

        QLineEdit* sizeEdit = new QLineEdit(doubleListToString(g.size), row);
        sizeEdit->setPlaceholderText("comma-separated, meaning depends on type");
        form->addRow("Size:", sizeEdit);

        QLineEdit* posEdit = new QLineEdit(posToString(g.pos), row);
        form->addRow("Position (x,y,z):", posEdit);

        QLineEdit* rotEdit = new QLineEdit(rotToString(g.rot), row);
        form->addRow("Rotation (x,y,z):", rotEdit);

        QLineEdit* colorEdit = new QLineEdit(doubleListToString(g.color), row);
        colorEdit->setPlaceholderText("r, g, b[, a] -- only used if Material is (none)");
        form->addRow("Color:", colorEdit);

        auto commit = [this, bodyId, geomIdx, typeCombo, meshCombo, materialCombo, sizeEdit, posEdit, rotEdit, colorEdit, gMassSpin/*, gInertiaEdit*/]() {
            if (!data.bodies.contains(bodyId) || geomIdx >= data.bodies[bodyId].geoms.size()) return;
            Geom& geom = data.bodies[bodyId].geoms[geomIdx];

            geom.type = typeCombo->currentText();
            geom.mesh = (meshCombo->currentText() == "(none)") ? "" : meshCombo->currentText();
            geom.material = (materialCombo->currentText() == "(none)") ? "" : materialCombo->currentText();
            geom.size = stringToDoubleList(sizeEdit->text());
            geom.pos = stringToPos(posEdit->text(), geom.pos);
            geom.rot = stringToRot(rotEdit->text(), geom.rot);
            geom.color = stringToDoubleList(colorEdit->text());
            geom.mass = gMassSpin->value();
            // geom.intertia = gInertiaEdit->text();

            schedulePreviewReload();
        };
        connect(typeCombo, &QComboBox::currentTextChanged, this, commit);
        connect(meshCombo, &QComboBox::currentTextChanged, this, commit);
        connect(materialCombo, &QComboBox::currentTextChanged, this, commit);
        connect(sizeEdit, &QLineEdit::editingFinished, this, commit);
        connect(posEdit, &QLineEdit::editingFinished, this, commit);
        connect(rotEdit, &QLineEdit::editingFinished, this, commit);
        connect(colorEdit, &QLineEdit::editingFinished, this, commit);
        connect(gMassSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, commit);
        // connect(gInertiaEdit, &QLineEdit::editingFinished, this, commit);

        QPushButton* removeBtn = new QPushButton("Remove Geom", row);
        form->addRow(removeBtn);
        connect(removeBtn, &QPushButton::clicked, this, [this, bodyId, geomIdx]() {
            if (data.bodies.contains(bodyId) && geomIdx < data.bodies[bodyId].geoms.size()) {
                QString user = findTargetUser(data, "geom", data.bodies[bodyId].geoms[geomIdx].id);
                if (!user.isEmpty()) {
                    Toast::showMessage(this, "Can't remove: " + user + " targets this geom.");
                    return;
                }
                data.bodies[bodyId].geoms.removeAt(geomIdx);
                clearAndRebuild();
            }
        });

        layout->addWidget(row);
    }

    QPushButton* addBtn = new QPushButton("+ Add Geom", box);
    addBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    layout->addWidget(addBtn, 0, Qt::AlignLeft);
    connect(addBtn, &QPushButton::clicked, this, [this, bodyId]() {
        if (!data.bodies.contains(bodyId)) return;
        Geom g;
        g.id = uniqueId("geom", [this](const QString& id) { return idsForKind(data, "geom").contains(id); });
        g.type = "box";
        g.size = {0.01, 0.01, 0.01};
        data.bodies[bodyId].geoms.append(g);
        clearAndRebuild();
    });

    bodyForm->addRow(box);
}




void ComponentEditorWindow::build_sites(QFormLayout* bodyForm, const QString& bodyId) {
    QGroupBox* box = new QGroupBox("Sites", bodyForm->parentWidget());
    QVBoxLayout* layout = new QVBoxLayout(box);

    const QList<Site>& sites = data.bodies.value(bodyId).sites;
    for (int i = 0; i < sites.size(); ++i) {
        const Site& s = sites[i];
        int siteIdx = i;

        QFrame* row = new QFrame(box);
        row->setFrameShape(QFrame::StyledPanel);
        QFormLayout* form = new QFormLayout(row);

        QLineEdit* idEdit = new QLineEdit(s.id, row);
        form->addRow("Id:", idEdit);

        QString oldSiteId = s.id;
        connect(idEdit, &QLineEdit::editingFinished, this, [this, bodyId, siteIdx, oldSiteId, idEdit]() {
            QString newId = idEdit->text().trimmed();
            if (newId.isEmpty() || newId == oldSiteId) {
                idEdit->setText(oldSiteId);
                return;
            }
            if (!data.bodies.contains(bodyId) || siteIdx >= data.bodies[bodyId].sites.size()) return;
            data.bodies[bodyId].sites[siteIdx].id = newId;
            clearAndRebuild();
        });

        TransformFieldRefs xform = addTransformFields(form, s.localTransform);

        auto commit = [this, bodyId, siteIdx, xform]() {
            if (!data.bodies.contains(bodyId) || siteIdx >= data.bodies[bodyId].sites.size()) return;
            Site& site = data.bodies[bodyId].sites[siteIdx];
            site.localTransform.position = stringToPos(xform.posEdit->text(), site.localTransform.position);
            site.localTransform.rotation = stringToRot(xform.rotEdit->text(), site.localTransform.rotation);
            schedulePreviewReload();
        };
        connect(xform.posEdit, &QLineEdit::editingFinished, this, commit);
        connect(xform.rotEdit, &QLineEdit::editingFinished, this, commit);

        QPushButton* removeBtn = new QPushButton("Remove Site", row);
        form->addRow(removeBtn);
        QString siteId = s.id;
        connect(removeBtn, &QPushButton::clicked, this, [this, bodyId, siteId]() {
            QString user = findTargetUser(data, "site", siteId);
            if (!user.isEmpty()) {
                Toast::showMessage(this, "Can't remove: " + user + " targets this site.");
                return;
            }
            if (data.bodies.contains(bodyId)) {
                QList<Site>& siteList = data.bodies[bodyId].sites;
                for (int j = 0; j < siteList.size(); ++j) {
                    if (siteList[j].id == siteId) { siteList.removeAt(j); break; }
                }
            }
            clearAndRebuild();
        });

        layout->addWidget(row);
    }

    QPushButton* addBtn = new QPushButton("+ Add Site", box);
    addBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    layout->addWidget(addBtn, 0, Qt::AlignLeft);
    connect(addBtn, &QPushButton::clicked, this, [this, bodyId]() {
        if (!data.bodies.contains(bodyId)) return;

        int totalSites = 0;
        for (const Node& n : data.bodies) totalSites += n.sites.size();

        Site s;
        s.id = QString("site_%1").arg(totalSites + 1);
        data.bodies[bodyId].sites.append(s);
        clearAndRebuild();
    });

    bodyForm->addRow(box);
}




void ComponentEditorWindow::build_joints(QVBoxLayout* parent) {
    QGroupBox* box = new QGroupBox("Joints", parent->parentWidget());
    QVBoxLayout* layout = new QVBoxLayout(box);

    for (const Edge& edge : data.joints) {
        QFrame* content = new QFrame(box);
        content->setFrameShape(QFrame::StyledPanel);
        QFormLayout* form = new QFormLayout(content);
        QString edgeId = edge.id;

        QLineEdit* idEdit = new QLineEdit(edge.id, content);
        form->addRow("Id:", idEdit);

        QString oldEdgeId = edge.id;
        connect(idEdit, &QLineEdit::editingFinished, this, [this, oldEdgeId, idEdit]() {
            QString newId = idEdit->text().trimmed();
            if (newId.isEmpty() || newId == oldEdgeId) {
                idEdit->setText(oldEdgeId);
                return;
            }
            for (int i = 0; i < data.joints.size(); ++i) {
                if (data.joints[i].id == oldEdgeId) {
                    data.joints[i].id = newId;
                    break;
                }
            }
            if (foldedSections.remove("joint:" + oldEdgeId)) foldedSections.insert("joint:" + newId);
            clearAndRebuild();
        });

        QComboBox* bodyACombo = new QComboBox(content);
        refreshBodyDropdown(bodyACombo);
        bodyACombo->setCurrentText(edge.bodyA);
        form->addRow("Body A:", bodyACombo);

        QComboBox* bodyBCombo = new QComboBox(content);
        refreshBodyDropdown(bodyBCombo);
        bodyBCombo->setCurrentText(edge.bodyB);
        form->addRow("Body B:", bodyBCombo);

        QComboBox* typeCombo = new QComboBox(content);
        typeCombo->addItems({"hinge", "slide", "ball", "free"});
        typeCombo->setCurrentText(edge.type);
        form->addRow("Type:", typeCombo);

        TransformFieldRefs xform = addTransformFields(form, edge.localTransform);

        QLineEdit* rangeEdit = new QLineEdit(doubleListToString(edge.range), content);
        rangeEdit->setPlaceholderText("min, max");
        form->addRow("Range:", rangeEdit);

        // ---- collapsible advanced section ----
        QToolButton* advToggle = new QToolButton(content);
        advToggle->setText("Advanced ▸");
        advToggle->setCheckable(true);
        advToggle->setStyleSheet("border: none; text-align: left;");
        form->addRow(advToggle);

        QWidget* advPanel = new QWidget(content);
        QFormLayout* advForm = new QFormLayout(advPanel);
        advPanel->setVisible(false);

        QDoubleSpinBox* dampingSpin = new QDoubleSpinBox(advPanel);
        dampingSpin->setValue(edge.damping);
        advForm->addRow("Damping:", dampingSpin);

        QDoubleSpinBox* armatureSpin = new QDoubleSpinBox(advPanel);
        armatureSpin->setValue(edge.armature);
        advForm->addRow("Armature:", armatureSpin);

        QDoubleSpinBox* frictionSpin = new QDoubleSpinBox(advPanel);
        frictionSpin->setValue(edge.frictionloss);
        advForm->addRow("Friction loss:", frictionSpin);

        QCheckBox* collisionCheck = new QCheckBox(advPanel);
        collisionCheck->setChecked(edge.collision);
        advForm->addRow("Collision:", collisionCheck);

        form->addRow(advPanel);
        connect(advToggle, &QToolButton::toggled, this, [advToggle, advPanel](bool checked) {
            advPanel->setVisible(checked);
            advToggle->setText(checked ? "Advanced ▾" : "Advanced ▸");
        });

        auto commit = [this, edgeId, bodyACombo, bodyBCombo, typeCombo, xform, rangeEdit,
                       dampingSpin, armatureSpin, frictionSpin, collisionCheck]() {
            Edge e = findEdge(data.joints, edgeId);
            e.bodyA = bodyACombo->currentText();
            e.bodyB = bodyBCombo->currentText();
            e.type = typeCombo->currentText();
            e.localTransform.position = stringToPos(xform.posEdit->text(), e.localTransform.position);
            e.localTransform.rotation = stringToRot(xform.rotEdit->text(), e.localTransform.rotation);
            e.range = stringToDoubleList(rangeEdit->text());
            e.damping = dampingSpin->value();
            e.armature = armatureSpin->value();
            e.frictionloss = frictionSpin->value();
            e.collision = collisionCheck->isChecked();
            upsertEdge(data.joints, e);
            schedulePreviewReload();
        };
        connect(bodyACombo, &QComboBox::currentTextChanged, this, commit);
        connect(bodyBCombo, &QComboBox::currentTextChanged, this, commit);
        connect(typeCombo, &QComboBox::currentTextChanged, this, commit);
        connect(xform.posEdit, &QLineEdit::editingFinished, this, commit);
        connect(xform.rotEdit, &QLineEdit::editingFinished, this, commit);
        connect(rangeEdit, &QLineEdit::editingFinished, this, commit);
        connect(dampingSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, commit);
        connect(armatureSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, commit);
        connect(frictionSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, commit);
        connect(collisionCheck, &QCheckBox::toggled, this, commit);

        QPushButton* removeBtn = new QPushButton("Remove Joint", content);
        form->addRow(removeBtn);
        connect(removeBtn, &QPushButton::clicked, this, [this, edgeId]() {
            QString user = findTargetUser(data, "joint", edgeId);
            if (!user.isEmpty()) {
                Toast::showMessage(this, "Can't remove: " + user + " targets this joint.");
                return;
            }
            removeEdgeById(data.joints, edgeId);
            clearAndRebuild();
        });

        QString header = QString("%1 — %2 (%3 → %4)").arg(edge.id, edge.type, edge.bodyA, edge.bodyB);
        content->setProperty("foldKey", "joint:" + edgeId);
        layout->addWidget(makeCollapsible(header, content, box));
    }

    QPushButton* addBtn = new QPushButton("+ Add Joint", box);
    addBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    layout->addWidget(addBtn, 0, Qt::AlignLeft);

    connect(addBtn, &QPushButton::clicked, this, [this]() {
        if (data.bodies.size() < 2) {
            Toast::showMessage(this, "Add at least two bodies before creating a joint.");
            return;
        }
        Edge e;
        e.id = QString("joint_%1").arg(data.joints.size() + 1);
        auto bodyIds = data.bodies.keys();
        e.bodyA = bodyIds.value(0);
        e.bodyB = bodyIds.value(1);
        e.type = "hinge";
        data.joints.append(e);
        clearAndRebuild();
    });

    parent->addWidget(box);
}




void ComponentEditorWindow::refreshBodyDropdown(QComboBox* combo) {
    combo->clear();
    for (const QString& id : data.bodies.keys()) {
        combo->addItem(id);
    }
}




void ComponentEditorWindow::refreshJointDropdown(QComboBox* combo) {
    combo->clear();
    combo->addItem("(none)");
    for (const Edge& e : data.joints) {
        combo->addItem(e.id);
    }
}




void ComponentEditorWindow::refreshSiteDropdown(QComboBox* combo) {
    combo->clear();
    combo->addItem("(none)");
    for (const Node& node : data.bodies) {
        for (const Site& s : node.sites) {
            combo->addItem(s.id);
        }
    }
}




#pragma region connectors

void ComponentEditorWindow::build_connectors() {
    QGroupBox* box = new QGroupBox("Connectors", this);
    QVBoxLayout* layout = new QVBoxLayout(box);

    for (const QString& key : data.connectors.keys()) {
        ConnectorDef def = data.connectors[key];

        QFrame* content = new QFrame(box);
        content->setFrameShape(QFrame::StyledPanel);
        QFormLayout* form = new QFormLayout(content);

        QLineEdit* idEdit = new QLineEdit(def.id, content);
        form->addRow("Id:", idEdit);

        QComboBox* bodyCombo = new QComboBox(content);
        refreshBodyDropdown(bodyCombo);
        bodyCombo->setCurrentText(def.body);
        form->addRow("Body:", bodyCombo);

        QLineEdit* descEdit = new QLineEdit(def.description, content);
        form->addRow("Description:", descEdit);

        TransformFieldRefs xform = addTransformFields(form, def.transform);

        QLineEdit* snapAnglesEdit = new QLineEdit(content);
        QStringList angleStrs;
        for (float a : def.mechanics.snapAngles) angleStrs << QString::number(a);
        snapAnglesEdit->setText(angleStrs.join(", "));
        snapAnglesEdit->setPlaceholderText("comma-separated degrees, blank = free angle");
        form->addRow("Snap angles:", snapAnglesEdit);

        QString connKey = key;

        connect(idEdit, &QLineEdit::editingFinished, this, [this, connKey, idEdit]() {
            QString newId = idEdit->text().trimmed();

            if (newId.isEmpty() || newId == connKey || data.connectors.contains(newId)) {
                idEdit->setText(connKey);
                return;
            }

            ConnectorDef c = data.connectors.take(connKey);
            c.id = newId;
            data.connectors.insert(newId, c);
            if (foldedSections.remove("connector:" + connKey)) foldedSections.insert("connector:" + newId);
            clearAndRebuild();
        });

        auto commit = [this, connKey, bodyCombo, descEdit, xform, snapAnglesEdit]() {
            if (!data.connectors.contains(connKey)) return;

            ConnectorDef c = data.connectors[connKey];
            c.body = bodyCombo->currentText();
            c.description = descEdit->text();
            c.transform.position = stringToPos(xform.posEdit->text(), c.transform.position);
            c.transform.rotation = stringToRot(xform.rotEdit->text(), c.transform.rotation);
            c.mechanics.snapAngles.clear();

            for (const QString& part : snapAnglesEdit->text().split(',', Qt::SkipEmptyParts)) {
                bool ok = false;
                float val = part.trimmed().toFloat(&ok);
                if (ok) c.mechanics.snapAngles.append(val);
            }

            data.connectors[connKey] = c;
            schedulePreviewReload();
        };

        connect(bodyCombo, &QComboBox::currentTextChanged, this, commit);
        connect(descEdit, &QLineEdit::editingFinished, this, commit);
        connect(xform.posEdit, &QLineEdit::editingFinished, this, commit);
        connect(xform.rotEdit, &QLineEdit::editingFinished, this, commit);
        connect(snapAnglesEdit, &QLineEdit::editingFinished, this, commit);

        QPushButton* removeBtn = new QPushButton("Remove Connector", content);
        form->addRow(removeBtn);
        connect(removeBtn, &QPushButton::clicked, this, [this, connKey]() {
            data.connectors.remove(connKey);
            clearAndRebuild();
        });

        QString header = QString("%1 on %2").arg(def.id, def.body.isEmpty() ? "(no body)" : def.body);
        content->setProperty("foldKey", "connector:" + connKey);
        layout->addWidget(makeCollapsible(header, content, box));
    }

    QPushButton* addBtn = new QPushButton("+ Add Connector", box);
    addBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    layout->addWidget(addBtn, 0, Qt::AlignLeft);

    connect(addBtn, &QPushButton::clicked, this, [this]() {
        if (data.bodies.isEmpty()) {
            Toast::showMessage(this, "Add a body before creating a connector.");
            return;
        }
        ConnectorDef def;
        def.id = QString("connector_%1").arg(data.connectors.size() + 1);

        int count = data.connectors.size() + 1;
        while (data.connectors.contains(def.id)) {
            def.id = QString("connector_%1").arg(++count);
        }

        def.body = data.bodies.contains(data.defaultBodyId) ? data.defaultBodyId
                                                            : data.bodies.firstKey();
        data.connectors[def.id] = def;
        clearAndRebuild();
    });

    leftLayout->addWidget(box);
}





#pragma region tendons

void ComponentEditorWindow::build_tendons(QVBoxLayout* parent) {
    QGroupBox* box = new QGroupBox("Tendons", parent->parentWidget());
    QVBoxLayout* layout = new QVBoxLayout(box);

    for (int i = 0; i < data.tendons.size(); ++i) {
        const TendonDef& tendon = data.tendons[i];
        const int tendonIdx = i;
        const QString tendonId = tendon.id;

        QFrame* content = new QFrame(box);
        content->setFrameShape(QFrame::StyledPanel);
        QFormLayout* form = new QFormLayout(content);

        QLineEdit* idEdit = new QLineEdit(tendon.id, content);
        form->addRow("Id:", idEdit);
        connect(idEdit, &QLineEdit::editingFinished, this, [this, tendonIdx, tendonId, idEdit]() {
            QString newId = idEdit->text().trimmed();
            if (newId.isEmpty() || newId == tendonId || idsForKind(data, "tendon").contains(newId)) {
                idEdit->setText(tendonId);
                return;
            }
            if (tendonIdx >= data.tendons.size()) return;
            data.tendons[tendonIdx].id = newId;   // references are not rewritten (renames never cascade)
            if (foldedSections.remove("tendon:" + tendonId)) foldedSections.insert("tendon:" + newId);
            clearAndRebuild();
        });

        form->addRow("Type:", new QLabel("fixed (sum of coef x joint value)", content));

        for (int k = 0; k < tendon.terms.size(); ++k) {
            const TendonTerm& term = tendon.terms[k];
            const int termIdx = k;

            QWidget* termRow = new QWidget(content);
            QHBoxLayout* termLayout = new QHBoxLayout(termRow);
            termLayout->setContentsMargins(0, 0, 0, 0);

            QComboBox* jointCombo = new QComboBox(termRow);
            jointCombo->addItems(idsForKind(data, "joint"));
            if (jointCombo->findText(term.joint) < 0) jointCombo->addItem(term.joint);   // keep a dangling reference visible
            jointCombo->setCurrentText(term.joint);
            termLayout->addWidget(jointCombo, 1);

            QDoubleSpinBox* coefSpin = new QDoubleSpinBox(termRow);
            coefSpin->setRange(-1000.0, 1000.0);
            coefSpin->setDecimals(4);
            coefSpin->setValue(term.coef);
            coefSpin->setToolTip("Written in the joint's logical direction (body_a -> body_b)");
            termLayout->addWidget(coefSpin);

            QPushButton* removeTermBtn = new QPushButton("✖", termRow);
            removeTermBtn->setFixedSize(20, 20);
            termLayout->addWidget(removeTermBtn);

            form->addRow(QString("Term %1:").arg(k + 1), termRow);

            auto commit = [this, tendonIdx, termIdx, jointCombo, coefSpin]() {
                if (tendonIdx >= data.tendons.size() || termIdx >= data.tendons[tendonIdx].terms.size()) return;
                TendonTerm& t = data.tendons[tendonIdx].terms[termIdx];
                t.joint = jointCombo->currentText();
                t.coef = coefSpin->value();
                schedulePreviewReload();
            };
            connect(jointCombo, &QComboBox::currentTextChanged, this, commit);
            connect(coefSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, commit);
            connect(removeTermBtn, &QPushButton::clicked, this, [this, tendonIdx, termIdx]() {
                if (tendonIdx >= data.tendons.size() || termIdx >= data.tendons[tendonIdx].terms.size()) return;
                if (data.tendons[tendonIdx].terms.size() == 1) {
                    Toast::showMessage(this, "A tendon needs at least one term.");
                    return;
                }
                data.tendons[tendonIdx].terms.removeAt(termIdx);
                clearAndRebuild();
            });
        }

        QPushButton* addTermBtn = new QPushButton("+ Add Term", content);
        addTermBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        form->addRow(addTermBtn);
        connect(addTermBtn, &QPushButton::clicked, this, [this, tendonIdx]() {
            if (tendonIdx >= data.tendons.size()) return;
            QStringList joints = idsForKind(data, "joint");
            if (joints.isEmpty()) {
                Toast::showMessage(this, "Add a joint first.");
                return;
            }
            TendonTerm t;
            t.joint = joints.first();
            t.coef = 1.0;
            data.tendons[tendonIdx].terms.append(t);
            clearAndRebuild();
        });

        QPushButton* removeBtn = new QPushButton("Remove Tendon", content);
        form->addRow(removeBtn);
        connect(removeBtn, &QPushButton::clicked, this, [this, tendonId]() {
            QString user = findTargetUser(data, "tendon", tendonId);
            if (!user.isEmpty()) {
                Toast::showMessage(this, "Can't remove: " + user + " targets this tendon.");
                return;
            }
            for (int j = 0; j < data.tendons.size(); ++j) {
                if (data.tendons[j].id == tendonId) { data.tendons.removeAt(j); break; }
            }
            clearAndRebuild();
        });

        content->setProperty("foldKey", "tendon:" + tendonId);
        layout->addWidget(makeCollapsible(QString("%1 — fixed (%2 terms)").arg(tendon.id).arg(tendon.terms.size()), content, box));
    }

    QPushButton* addBtn = new QPushButton("+ Add Tendon", box);
    addBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    layout->addWidget(addBtn, 0, Qt::AlignLeft);
    connect(addBtn, &QPushButton::clicked, this, [this]() {
        QStringList joints = idsForKind(data, "joint");
        if (joints.isEmpty()) {
            Toast::showMessage(this, "Add a joint before creating a tendon.");
            return;
        }
        TendonDef t;
        t.id = uniqueId("tendon", [this](const QString& id) { return idsForKind(data, "tendon").contains(id); });
        TendonTerm term;
        term.joint = joints.first();
        term.coef = 1.0;
        t.terms.append(term);
        data.tendons.append(t);
        clearAndRebuild();
    });

    parent->addWidget(box);
}




#pragma region devices

/*
 * One "target" editor for a device: a kind dropdown (limited to what the device
 * type allows -- DeviceTypes) and a target-id dropdown filtered by that kind.
 * A single allowed kind hides the kind dropdown. Changing the kind resets the id
 * and rebuilds; changing the id only stores it.
 */
struct ComponentEditorWindow::TargetRefs { QComboBox* kindCombo; QComboBox* idCombo; };

ComponentEditorWindow::TargetRefs ComponentEditorWindow::addTargetFields(QFormLayout* form, QWidget* parent, const TargetRef& target,
                                                                         const QStringList& allowedKinds) {
    QComboBox* kindCombo = new QComboBox(parent);
    kindCombo->addItems(allowedKinds);
    if (kindCombo->findText(target.kind) < 0) kindCombo->addItem(target.kind);
    kindCombo->setCurrentText(target.kind);
    if (allowedKinds.size() > 1) form->addRow("Target kind:", kindCombo);
    else kindCombo->setVisible(false);

    QComboBox* idCombo = new QComboBox(parent);
    idCombo->addItems(idsForKind(data, target.kind));
    if (idCombo->findText(target.id) < 0) idCombo->addItem(target.id);   // keep a dangling reference visible
    idCombo->setCurrentText(target.id);
    form->addRow(QString("Target %1:").arg(target.kind), idCombo);
    return {kindCombo, idCombo};
}


void ComponentEditorWindow::build_devices() {
    // ---------------- actuators ----------------
    {
        QGroupBox* box = new QGroupBox("Actuators", this);
        QVBoxLayout* layout = new QVBoxLayout(box);

        for (int i = 0; i < data.actuators.size(); ++i) {
            const ActuatorDef a = data.actuators[i];
            const int idx = i;

            QFrame* row = new QFrame(box);
            row->setFrameShape(QFrame::StyledPanel);
            QFormLayout* form = new QFormLayout(row);

            QLineEdit* idEdit = new QLineEdit(a.id, row);
            form->addRow("Id:", idEdit);
            const QString oldId = a.id;
            connect(idEdit, &QLineEdit::editingFinished, this, [this, idx, oldId, idEdit]() {
                QString newId = idEdit->text().trimmed();
                if (newId.isEmpty() || newId == oldId || deviceIdTaken(data, newId)) { idEdit->setText(oldId); return; }
                if (idx >= data.actuators.size()) return;
                data.actuators[idx].id = newId;   // references are not rewritten (renames never cascade)
                clearAndRebuild();
            });

            QComboBox* typeCombo = new QComboBox(row);
            typeCombo->addItems(DeviceTypes::typeNames(DeviceFamily::Actuator));
            typeCombo->setCurrentText(a.type);
            form->addRow("Type:", typeCombo);

            const DeviceTypeInfo* info = DeviceTypes::find(DeviceFamily::Actuator, a.type);
            TargetRefs target = addTargetFields(form, row, a.target, info ? info->allowedTargets : QStringList{a.target.kind});

            QDoubleSpinBox* kpSpin = new QDoubleSpinBox(row);
            kpSpin->setRange(0.0, 10000.0);
            kpSpin->setValue(a.kp);
            form->addRow("kp:", kpSpin);

            QDoubleSpinBox* kvSpin = new QDoubleSpinBox(row);
            kvSpin->setRange(0.0, 10000.0);
            kvSpin->setValue(a.kv);
            form->addRow("kv:", kvSpin);

            QLineEdit* ctrlRangeEdit = new QLineEdit(doubleListToString(a.ctrlrange), row);
            ctrlRangeEdit->setPlaceholderText("min, max");
            form->addRow("Ctrl range:", ctrlRangeEdit);

            QLineEdit* forceRangeEdit = new QLineEdit(doubleListToString(a.forceRange), row);
            forceRangeEdit->setPlaceholderText("min, max");
            form->addRow("Force range:", forceRangeEdit);

            auto commit = [this, idx, target, kpSpin, kvSpin, ctrlRangeEdit, forceRangeEdit]() {
                if (idx >= data.actuators.size()) return;
                ActuatorDef& d = data.actuators[idx];
                d.target.id = target.idCombo->currentText();
                d.kp = kpSpin->value();
                d.kv = kvSpin->value();
                d.ctrlrange = stringToDoubleList(ctrlRangeEdit->text());
                d.forceRange = stringToDoubleList(forceRangeEdit->text());
                schedulePreviewReload();
            };
            connect(target.idCombo, &QComboBox::currentTextChanged, this, commit);
            connect(kpSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, commit);
            connect(kvSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, commit);
            connect(ctrlRangeEdit, &QLineEdit::editingFinished, this, commit);
            connect(forceRangeEdit, &QLineEdit::editingFinished, this, commit);

            // A new type may not allow the current target kind: fall back to the first allowed one.
            connect(typeCombo, &QComboBox::currentTextChanged, this, [this, idx](const QString& type) {
                if (idx >= data.actuators.size()) return;
                ActuatorDef& d = data.actuators[idx];
                d.type = type;
                const DeviceTypeInfo* ti = DeviceTypes::find(DeviceFamily::Actuator, type);
                if (ti && !ti->allowedTargets.contains(d.target.kind)) {
                    d.target.kind = ti->allowedTargets.first();
                    d.target.id = idsForKind(data, d.target.kind).value(0);
                }
                clearAndRebuild();
            });
            connect(target.kindCombo, &QComboBox::currentTextChanged, this, [this, idx](const QString& kind) {
                if (idx >= data.actuators.size() || data.actuators[idx].target.kind == kind) return;
                data.actuators[idx].target.kind = kind;
                data.actuators[idx].target.id = idsForKind(data, kind).value(0);
                clearAndRebuild();
            });

            QPushButton* removeBtn = new QPushButton("Remove Actuator", row);
            form->addRow(removeBtn);
            connect(removeBtn, &QPushButton::clicked, this, [this, oldId]() {
                QString user = findTargetUser(data, "actuator", oldId);
                if (!user.isEmpty()) { Toast::showMessage(this, "Can't remove: " + user + " targets this actuator."); return; }
                for (int j = 0; j < data.actuators.size(); ++j) if (data.actuators[j].id == oldId) { data.actuators.removeAt(j); break; }
                clearAndRebuild();
            });

            layout->addWidget(row);
        }

        QPushButton* addBtn = new QPushButton("+ Add Actuator", box);
        addBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        layout->addWidget(addBtn, 0, Qt::AlignLeft);
        connect(addBtn, &QPushButton::clicked, this, [this]() {
            if (data.joints.isEmpty()) { Toast::showMessage(this, "Add a joint before creating an actuator."); return; }
            ActuatorDef a;
            a.id = uniqueId("actuator", [this](const QString& id) { return deviceIdTaken(data, id); });
            a.type = "position";
            a.target = {"joint", data.joints.first().id};
            data.actuators.append(a);
            clearAndRebuild();
        });
        leftLayout->addWidget(box);
    }

    // ---------------- sensors ----------------
    {
        QGroupBox* box = new QGroupBox("Sensors", this);
        QVBoxLayout* layout = new QVBoxLayout(box);

        for (int i = 0; i < data.sensors.size(); ++i) {
            const SensorDef s = data.sensors[i];
            const int idx = i;

            QFrame* row = new QFrame(box);
            row->setFrameShape(QFrame::StyledPanel);
            QFormLayout* form = new QFormLayout(row);

            QLineEdit* idEdit = new QLineEdit(s.id, row);
            form->addRow("Id:", idEdit);
            const QString oldId = s.id;
            connect(idEdit, &QLineEdit::editingFinished, this, [this, idx, oldId, idEdit]() {
                QString newId = idEdit->text().trimmed();
                if (newId.isEmpty() || newId == oldId || deviceIdTaken(data, newId)) { idEdit->setText(oldId); return; }
                if (idx >= data.sensors.size()) return;
                data.sensors[idx].id = newId;
                clearAndRebuild();
            });

            QComboBox* typeCombo = new QComboBox(row);
            typeCombo->addItems(DeviceTypes::typeNames(DeviceFamily::Sensor));
            typeCombo->setCurrentText(s.type);
            form->addRow("Type:", typeCombo);

            const DeviceTypeInfo* info = DeviceTypes::find(DeviceFamily::Sensor, s.type);
            if (info) form->addRow("Output:", new QLabel(QString("%1, dim %2").arg(DeviceTypes::shapeName(info->shape)).arg(info->dim), row));
            TargetRefs target = addTargetFields(form, row, s.target, info ? info->allowedTargets : QStringList{s.target.kind});

            connect(target.idCombo, &QComboBox::currentTextChanged, this, [this, idx, target]() {
                if (idx >= data.sensors.size()) return;
                data.sensors[idx].target.id = target.idCombo->currentText();
                schedulePreviewReload();
            });
            connect(typeCombo, &QComboBox::currentTextChanged, this, [this, idx](const QString& type) {
                if (idx >= data.sensors.size()) return;
                SensorDef& d = data.sensors[idx];
                d.type = type;
                const DeviceTypeInfo* ti = DeviceTypes::find(DeviceFamily::Sensor, type);
                if (ti && !ti->allowedTargets.contains(d.target.kind)) {
                    d.target.kind = ti->allowedTargets.first();
                    d.target.id = idsForKind(data, d.target.kind).value(0);
                }
                clearAndRebuild();
            });
            connect(target.kindCombo, &QComboBox::currentTextChanged, this, [this, idx](const QString& kind) {
                if (idx >= data.sensors.size() || data.sensors[idx].target.kind == kind) return;
                data.sensors[idx].target.kind = kind;
                data.sensors[idx].target.id = idsForKind(data, kind).value(0);
                clearAndRebuild();
            });

            QPushButton* removeBtn = new QPushButton("Remove Sensor", row);
            form->addRow(removeBtn);
            connect(removeBtn, &QPushButton::clicked, this, [this, oldId]() {
                QString user = findTargetUser(data, "sensor", oldId);
                if (!user.isEmpty()) { Toast::showMessage(this, "Can't remove: " + user + " targets this sensor."); return; }
                for (int j = 0; j < data.sensors.size(); ++j) if (data.sensors[j].id == oldId) { data.sensors.removeAt(j); break; }
                clearAndRebuild();
            });

            layout->addWidget(row);
        }

        QPushButton* addBtn = new QPushButton("+ Add Sensor", box);
        addBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        layout->addWidget(addBtn, 0, Qt::AlignLeft);
        connect(addBtn, &QPushButton::clicked, this, [this]() {
            if (data.joints.isEmpty()) { Toast::showMessage(this, "Add a joint before creating a sensor (or change its target afterwards)."); return; }
            SensorDef s;
            s.id = uniqueId("sensor", [this](const QString& id) { return deviceIdTaken(data, id); });
            s.type = "jointpos";
            s.target = {"joint", data.joints.first().id};
            data.sensors.append(s);
            clearAndRebuild();
        });
        leftLayout->addWidget(box);
    }

    // ---------------- cameras ----------------
    {
        QGroupBox* box = new QGroupBox("Cameras (loaded and saved; not simulated yet)", this);
        QVBoxLayout* layout = new QVBoxLayout(box);

        for (int i = 0; i < data.cameras.size(); ++i) {
            const CameraDef cam = data.cameras[i];
            const int idx = i;

            QFrame* row = new QFrame(box);
            row->setFrameShape(QFrame::StyledPanel);
            QFormLayout* form = new QFormLayout(row);

            QLineEdit* idEdit = new QLineEdit(cam.id, row);
            form->addRow("Id:", idEdit);
            const QString oldId = cam.id;
            connect(idEdit, &QLineEdit::editingFinished, this, [this, idx, oldId, idEdit]() {
                QString newId = idEdit->text().trimmed();
                if (newId.isEmpty() || newId == oldId || deviceIdTaken(data, newId)) { idEdit->setText(oldId); return; }
                if (idx >= data.cameras.size()) return;
                data.cameras[idx].id = newId;
                clearAndRebuild();
            });

            QComboBox* typeCombo = new QComboBox(row);
            typeCombo->addItems(DeviceTypes::typeNames(DeviceFamily::Camera));
            typeCombo->setCurrentText(cam.type);
            form->addRow("Type:", typeCombo);

            TargetRefs target = addTargetFields(form, row, cam.target, {"site"});

            QLineEdit* resEdit = new QLineEdit(resolutionToString(cam.resolution), row);
            resEdit->setPlaceholderText("width, height (pixels)");
            form->addRow("Resolution:", resEdit);

            QDoubleSpinBox* fovySpin = new QDoubleSpinBox(row);
            fovySpin->setRange(1.0, 179.0);
            fovySpin->setValue(cam.fovy > 0 ? cam.fovy : 60.0);
            form->addRow("Fovy (deg):", fovySpin);

            auto commit = [this, idx, typeCombo, target, resEdit, fovySpin]() {
                if (idx >= data.cameras.size()) return;
                CameraDef& d = data.cameras[idx];
                d.type = typeCombo->currentText();
                d.target.id = target.idCombo->currentText();
                d.resolution = stringToResolution(resEdit->text(), d.resolution);
                d.fovy = fovySpin->value();
            };
            connect(typeCombo, &QComboBox::currentTextChanged, this, commit);
            connect(target.idCombo, &QComboBox::currentTextChanged, this, commit);
            connect(resEdit, &QLineEdit::editingFinished, this, commit);
            connect(fovySpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, commit);

            QPushButton* removeBtn = new QPushButton("Remove Camera", row);
            form->addRow(removeBtn);
            connect(removeBtn, &QPushButton::clicked, this, [this, oldId]() {
                QString user = findTargetUser(data, "camera", oldId);
                if (!user.isEmpty()) { Toast::showMessage(this, "Can't remove: " + user + " targets this camera."); return; }
                for (int j = 0; j < data.cameras.size(); ++j) if (data.cameras[j].id == oldId) { data.cameras.removeAt(j); break; }
                clearAndRebuild();
            });

            layout->addWidget(row);
        }

        QPushButton* addBtn = new QPushButton("+ Add Camera", box);
        addBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        layout->addWidget(addBtn, 0, Qt::AlignLeft);
        connect(addBtn, &QPushButton::clicked, this, [this]() {
            QStringList sites = idsForKind(data, "site");
            if (sites.isEmpty()) { Toast::showMessage(this, "Add a site before creating a camera."); return; }
            CameraDef cam;
            cam.id = uniqueId("camera", [this](const QString& id) { return deviceIdTaken(data, id); });
            cam.type = "rgb";
            cam.target = {"site", sites.first()};
            cam.resolution = qMakePair(320, 240);
            cam.fovy = 60.0;
            data.cameras.append(cam);
            clearAndRebuild();
        });
        leftLayout->addWidget(box);
    }

    // ---------------- displays ----------------
    {
        QGroupBox* box = new QGroupBox("Displays (loaded and saved; not simulated yet)", this);
        QVBoxLayout* layout = new QVBoxLayout(box);

        for (int i = 0; i < data.displays.size(); ++i) {
            const DisplayDef disp = data.displays[i];
            const int idx = i;

            QFrame* row = new QFrame(box);
            row->setFrameShape(QFrame::StyledPanel);
            QFormLayout* form = new QFormLayout(row);

            QLineEdit* idEdit = new QLineEdit(disp.id, row);
            form->addRow("Id:", idEdit);
            const QString oldId = disp.id;
            connect(idEdit, &QLineEdit::editingFinished, this, [this, idx, oldId, idEdit]() {
                QString newId = idEdit->text().trimmed();
                if (newId.isEmpty() || newId == oldId || deviceIdTaken(data, newId)) { idEdit->setText(oldId); return; }
                if (idx >= data.displays.size()) return;
                data.displays[idx].id = newId;
                clearAndRebuild();
            });

            TargetRefs target = addTargetFields(form, row, disp.target, {"geom"});

            QLineEdit* resEdit = new QLineEdit(resolutionToString(disp.resolution), row);
            resEdit->setPlaceholderText("width, height (pixels)");
            form->addRow("Resolution:", resEdit);

            auto commit = [this, idx, target, resEdit]() {
                if (idx >= data.displays.size()) return;
                DisplayDef& d = data.displays[idx];
                d.target.id = target.idCombo->currentText();
                d.resolution = stringToResolution(resEdit->text(), d.resolution);
            };
            connect(target.idCombo, &QComboBox::currentTextChanged, this, commit);
            connect(resEdit, &QLineEdit::editingFinished, this, commit);

            QPushButton* removeBtn = new QPushButton("Remove Display", row);
            form->addRow(removeBtn);
            connect(removeBtn, &QPushButton::clicked, this, [this, oldId]() {
                QString user = findTargetUser(data, "display", oldId);
                if (!user.isEmpty()) { Toast::showMessage(this, "Can't remove: " + user + " targets this display."); return; }
                for (int j = 0; j < data.displays.size(); ++j) if (data.displays[j].id == oldId) { data.displays.removeAt(j); break; }
                clearAndRebuild();
            });

            layout->addWidget(row);
        }

        QPushButton* addBtn = new QPushButton("+ Add Display", box);
        addBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        layout->addWidget(addBtn, 0, Qt::AlignLeft);
        connect(addBtn, &QPushButton::clicked, this, [this]() {
            QStringList geoms = idsForKind(data, "geom");
            if (geoms.isEmpty()) { Toast::showMessage(this, "Add a geom before creating a display."); return; }
            DisplayDef d;
            d.id = uniqueId("display", [this](const QString& id) { return deviceIdTaken(data, id); });
            d.target = {"geom", geoms.first()};
            d.resolution = qMakePair(128, 64);
            data.displays.append(d);
            clearAndRebuild();
        });
        leftLayout->addWidget(box);
    }
}





#pragma region interface

void ComponentEditorWindow::build_interface() {
    build_interface_list(leftLayout, "Inputs", data.interfaceInputs, true);
    build_interface_list(leftLayout, "Outputs", data.interfaceOutputs, false);
}




/*
 * One entry per public signal. The kind dropdown is limited to what the
 * direction allows; the target-id dropdown is filtered by the chosen kind.
 * "emulator" signals have no id and state their own channel type and dim;
 * every other signal's shape is derived from its target, so it is only shown.
 */
void ComponentEditorWindow::build_interface_list(QVBoxLayout* parent, const QString& title, QMap<QString, InterfaceDef>& target, bool isInput) {
    QGroupBox* box = new QGroupBox(title, parent->parentWidget());
    QVBoxLayout* layout = new QVBoxLayout(box);

    const QStringList kinds = isInput ? QStringList{"joint", "display", "actuator", "emulator"}
                                      : QStringList{"sensor", "camera", "emulator"};

    for (const QString& key : target.keys()) {
        InterfaceDef def = target[key];

        QFrame* row = new QFrame(box);
        row->setFrameShape(QFrame::StyledPanel);
        QFormLayout* form = new QFormLayout(row);

        QLineEdit* nameEdit = new QLineEdit(def.name, row);
        form->addRow("Name:", nameEdit);
        connect(nameEdit, &QLineEdit::editingFinished, this, [this, &target, key, nameEdit]() {
            QString newName = nameEdit->text().trimmed();
            // signal names are unique across inputs AND outputs
            if (newName.isEmpty() || newName == key || data.interfaceInputs.contains(newName) || data.interfaceOutputs.contains(newName)) {
                nameEdit->setText(key);
                return;
            }
            InterfaceDef d = target.take(key);
            d.name = newName;
            target[newName] = d;
            clearAndRebuild();
        });

        QLineEdit* unitEdit = new QLineEdit(def.unit, row);
        form->addRow("Unit:", unitEdit);

        // Domain: type dropdown + one spin box per parameter of that type. The set of types
        // and their parameters mirrors the loader's domain registry (ComponentData.cpp).
        QComboBox* domainCombo = new QComboBox(row);
        domainCombo->addItems({"unbounded", "ranged"});
        domainCombo->setCurrentText(def.domain.type);
        form->addRow("Domain:", domainCombo);

        QDoubleSpinBox* minSpin = new QDoubleSpinBox(row);
        minSpin->setRange(-1e9, 1e9);
        minSpin->setDecimals(4);
        minSpin->setValue(def.domain.parameters.value("min", 0.0));
        form->addRow("Min:", minSpin);

        QDoubleSpinBox* maxSpin = new QDoubleSpinBox(row);
        maxSpin->setRange(-1e9, 1e9);
        maxSpin->setDecimals(4);
        maxSpin->setValue(def.domain.parameters.value("max", 0.0));
        form->addRow("Max:", maxSpin);
        setFormFieldVisible(minSpin, def.domain.type == "ranged");
        setFormFieldVisible(maxSpin, def.domain.type == "ranged");

        QCheckBox* physicalCheck = new QCheckBox(row);
        physicalCheck->setChecked(def.physical);
        form->addRow("Physical:", physicalCheck);

        QComboBox* kindCombo = new QComboBox(row);
        kindCombo->addItems(kinds);
        if (kindCombo->findText(def.target.kind) < 0) kindCombo->addItem(def.target.kind);
        kindCombo->setCurrentText(def.target.kind);
        form->addRow("Target kind:", kindCombo);

        const bool isEmulator = def.target.kind == "emulator";

        QComboBox* idCombo = new QComboBox(row);
        idCombo->addItems(idsForKind(data, def.target.kind));
        if (!isEmulator && idCombo->findText(def.target.id) < 0) idCombo->addItem(def.target.id);   // keep a dangling reference visible
        idCombo->setCurrentText(def.target.id);
        form->addRow("Target id:", idCombo);
        setFormFieldVisible(idCombo, !isEmulator);

        QComboBox* channelCombo = new QComboBox(row);
        channelCombo->addItems({"scalar", "vector"});
        channelCombo->setCurrentText(def.channelType);
        form->addRow("Channel type:", channelCombo);

        QSpinBox* dimSpin = new QSpinBox(row);
        dimSpin->setRange(1, 64);
        dimSpin->setValue(def.dim);
        form->addRow("Dim:", dimSpin);
        setFormFieldVisible(channelCombo, isEmulator);
        setFormFieldVisible(dimSpin, isEmulator);

        if (!isEmulator) {
            form->addRow("Shape:", new QLabel(QString("%1, dim %2 (derived from the target)")
                                              .arg(DeviceTypes::shapeName(data.interfaceShape(def))).arg(data.interfaceDim(def)), row));
        }

        QLineEdit* labelsEdit = new QLineEdit(def.componentLabels.join(", "), row);
        labelsEdit->setPlaceholderText("optional legend names, e.g. x, y, z");
        form->addRow("Labels:", labelsEdit);

        QString ioKey = key;
        auto commit = [this, &target, ioKey, unitEdit, domainCombo, minSpin, maxSpin, physicalCheck, idCombo, channelCombo, dimSpin, labelsEdit]() {
            if (!target.contains(ioKey)) return;
            InterfaceDef d = target[ioKey];
            d.unit = unitEdit->text();
            d.physical = physicalCheck->isChecked();

            d.domain.type = domainCombo->currentText();
            d.domain.parameters.clear();
            if (d.domain.type == "ranged") {
                d.domain.parameters["min"] = minSpin->value();
                d.domain.parameters["max"] = maxSpin->value();
            }
            setFormFieldVisible(minSpin, d.domain.type == "ranged");
            setFormFieldVisible(maxSpin, d.domain.type == "ranged");

            if (d.target.kind != "emulator") d.target.id = idCombo->currentText();
            else {
                d.channelType = channelCombo->currentText();
                d.dim = (d.channelType == "scalar") ? 1 : dimSpin->value();
                if (d.channelType == "scalar") dimSpin->setValue(1);
            }

            d.componentLabels.clear();
            for (const QString& part : labelsEdit->text().split(',', Qt::SkipEmptyParts)) d.componentLabels.append(part.trimmed());

            target[ioKey] = d;
        };
        connect(unitEdit, &QLineEdit::editingFinished, this, commit);
        connect(domainCombo, &QComboBox::currentTextChanged, this, commit);
        connect(minSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, commit);
        connect(maxSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, commit);
        connect(physicalCheck, &QCheckBox::toggled, this, commit);
        connect(idCombo, &QComboBox::currentTextChanged, this, commit);
        connect(channelCombo, &QComboBox::currentTextChanged, this, commit);
        connect(dimSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, commit);
        connect(labelsEdit, &QLineEdit::editingFinished, this, commit);

        // Kind change: reset the id to the first candidate of the new kind and rebuild
        // (the shape/emulator fields depend on it).
        connect(kindCombo, &QComboBox::currentTextChanged, this, [this, &target, ioKey](const QString& kind) {
            if (!target.contains(ioKey) || target[ioKey].target.kind == kind) return;
            InterfaceDef d = target[ioKey];
            d.target.kind = kind;
            d.target.id = (kind == "emulator") ? QString() : idsForKind(data, kind).value(0);
            target[ioKey] = d;
            clearAndRebuild();
        });

        QPushButton* removeBtn = new QPushButton("Remove", row);
        form->addRow(removeBtn);
        connect(removeBtn, &QPushButton::clicked, this, [this, &target, ioKey]() {
            target.remove(ioKey);
            clearAndRebuild();
        });

        layout->addWidget(row);
    }

    QPushButton* addBtn = new QPushButton("+ Add " + title.chopped(title.endsWith('s') ? 1 : 0), box);
    addBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    layout->addWidget(addBtn, 0, Qt::AlignLeft);

    connect(addBtn, &QPushButton::clicked, this, [this, &target, isInput]() {
        InterfaceDef def;
        int n = data.interfaceInputs.size() + data.interfaceOutputs.size() + 1;
        def.name = QString("channel_%1").arg(n);
        while (data.interfaceInputs.contains(def.name) || data.interfaceOutputs.contains(def.name)) def.name = QString("channel_%1").arg(++n);
        def.target.kind = isInput ? "joint" : "sensor";
        def.target.id = idsForKind(data, def.target.kind).value(0);
        target[def.name] = def;
        clearAndRebuild();
    });

    parent->addWidget(box);
}




#pragma region emulator

void ComponentEditorWindow::build_emulator() {
    QGroupBox* box = new QGroupBox("Emulator", this);
    QFormLayout* form = new QFormLayout(box);

    QLineEdit* typeEdit = new QLineEdit(data.emulatorDef.type, box);
    typeEdit->setPlaceholderText("e.g. servo, dc_gear, imu, stepper, default");
    form->addRow("Type:", typeEdit);
    connect(typeEdit, &QLineEdit::editingFinished, this, [this, typeEdit]() {
        data.emulatorDef.type = typeEdit->text();
    });

    QLabel* paramsLabel = new QLabel("Parameters (freeform -- see the component's chosen template for expected keys):", box);
    paramsLabel->setWordWrap(true);
    form->addRow(paramsLabel);

    KeyValueListWidget* paramsWidget = new KeyValueListWidget(box);
    paramsWidget->setValue(variantMapToJson(data.emulatorDef.parameters));
    form->addRow(paramsWidget);
    connect(paramsWidget, &KeyValueListWidget::changed, this, [this, paramsWidget]() {
        data.emulatorDef.parameters = jsonToVariantMap(paramsWidget->value());
    });

    leftLayout->addWidget(box);
}




#pragma region data

void ComponentEditorWindow::build_data() {
    QGroupBox* metaBox = new QGroupBox("Meta", this);
    QFormLayout* metaForm = new QFormLayout(metaBox);

    QLineEdit* idEdit = new QLineEdit(data.modelId, metaBox);
    idEdit->setPlaceholderText("unique id -- also the save folder/file name");
    if (lockedComponent) {
        idEdit->setReadOnly(true);
        idEdit->setToolTip("Id is locked while editing an existing component -- use \"Save As New Component\" to save under a different id.");
    }
    metaForm->addRow("Id:", idEdit);
    connect(idEdit, &QLineEdit::editingFinished, this, [this, idEdit]() { data.modelId = idEdit->text().trimmed(); });

    QLineEdit* nameEdit = new QLineEdit(data.meta.name, metaBox);
    metaForm->addRow("Name:", nameEdit);
    connect(nameEdit, &QLineEdit::editingFinished, this, [this, nameEdit]() { data.meta.name = nameEdit->text(); });

    QLineEdit* versionEdit = new QLineEdit(data.meta.version, metaBox);
    metaForm->addRow("Version:", versionEdit);
    connect(versionEdit, &QLineEdit::editingFinished, this, [this, versionEdit]() { data.meta.version = versionEdit->text(); });

    QLineEdit* authorEdit = new QLineEdit(data.meta.author, metaBox);
    metaForm->addRow("Author:", authorEdit);
    connect(authorEdit, &QLineEdit::editingFinished, this, [this, authorEdit]() { data.meta.author = authorEdit->text(); });

    QWidget* iconRow = new QWidget(metaBox);
    QHBoxLayout* iconLayout = new QHBoxLayout(iconRow);
    iconLayout->setContentsMargins(0, 0, 0, 0);
    QLineEdit* iconEdit = new QLineEdit(data.meta.iconPath, iconRow);
    QPushButton* iconBrowseBtn = new QPushButton("Browse...", iconRow);
    iconLayout->addWidget(iconEdit);
    iconLayout->addWidget(iconBrowseBtn);
    metaForm->addRow("Icon:", iconRow);
    connect(iconBrowseBtn, &QPushButton::clicked, this, [this, iconEdit]() {
        QString path = QFileDialog::getOpenFileName(this, "Select Icon", QString(), "Images (*.png *.jpg)",
                                                    nullptr, QFileDialog::DontUseNativeDialog);
        if (!path.isEmpty()) { iconEdit->setText(path); data.meta.iconPath = path; }
    });
    connect(iconEdit, &QLineEdit::editingFinished, this, [this, iconEdit]() { data.meta.iconPath = iconEdit->text(); });

    leftLayout->addWidget(metaBox);

    // ---- specs ----
    QGroupBox* specsBox = new QGroupBox("Specs (freeform)", this);
    QVBoxLayout* specsLayout = new QVBoxLayout(specsBox);
    KeyValueListWidget* specsWidget = new KeyValueListWidget(specsBox);
    specsWidget->setValue(data.specs);
    specsLayout->addWidget(specsWidget);
    connect(specsWidget, &KeyValueListWidget::changed, this, [this, specsWidget]() { data.specs = specsWidget->value(); });
    leftLayout->addWidget(specsBox);

    // ---- pins ----
    QGroupBox* pinsBox = new QGroupBox("Pins", this);
    QVBoxLayout* pinsLayout = new QVBoxLayout(pinsBox);

    for (int i = 0; i < data.pins.size(); ++i) {
        const PinDef& pin = data.pins[i];
        int pinIdx = i;

        QFrame* row = new QFrame(pinsBox);
        row->setFrameShape(QFrame::StyledPanel);
        QFormLayout* form = new QFormLayout(row);

        QLineEdit* idEdit = new QLineEdit(pin.id, row);
        form->addRow("Id:", idEdit);

        QLineEdit* descEdit = new QLineEdit(pin.description, row);
        form->addRow("Description:", descEdit);

        QLineEdit* voltageEdit = new QLineEdit(
            (pin.voltageRange.first != 0.0f || pin.voltageRange.second != 0.0f)
                ? QString("%1, %2").arg(pin.voltageRange.first).arg(pin.voltageRange.second)
                : QString(), row);
        voltageEdit->setPlaceholderText("min, max volts -- blank if unspecified");
        form->addRow("Voltage range:", voltageEdit);

        auto commit = [this, pinIdx, idEdit, descEdit, voltageEdit]() {
            if (pinIdx >= data.pins.size()) return;
            PinDef& p = data.pins[pinIdx];
            p.id = idEdit->text().trimmed();
            p.description = descEdit->text();
            QList<double> range = stringToDoubleList(voltageEdit->text());
            p.voltageRange = (range.size() == 2)
                ? qMakePair(static_cast<float>(range[0]), static_cast<float>(range[1]))
                : qMakePair(0.0f, 0.0f);
        };
        connect(idEdit, &QLineEdit::editingFinished, this, commit);
        connect(descEdit, &QLineEdit::editingFinished, this, commit);
        connect(voltageEdit, &QLineEdit::editingFinished, this, commit);

        QPushButton* removeBtn = new QPushButton("Remove Pin", row);
        form->addRow(removeBtn);
        connect(removeBtn, &QPushButton::clicked, this, [this, pinIdx]() {
            if (pinIdx < data.pins.size()) {
                data.pins.removeAt(pinIdx);
                clearAndRebuild();
            }
        });

        pinsLayout->addWidget(row);
    }

    QPushButton* addPinBtn = new QPushButton("+ Add Pin", pinsBox);
    addPinBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    pinsLayout->addWidget(addPinBtn, 0, Qt::AlignLeft);
    connect(addPinBtn, &QPushButton::clicked, this, [this]() {
        PinDef pin;
        pin.id = QString("pin_%1").arg(data.pins.size() + 1);
        data.pins.append(pin);
        clearAndRebuild();
    });

    leftLayout->addWidget(pinsBox);
}
