// src/View/Windows/ComponentEditorWindow.cpp
#include "ComponentEditorWindow.h"
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




// ---------------- QList<Edge> helpers ----------------
// ComponentData deliberately does not own a KinematicGraph (that's derived/
// cached state, built once by ComponentBlueprint) -- joints are a plain
// QList<Edge> here, so these small free functions stand in for the
// find/update/remove-by-id operations KinematicGraph used to provide.

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
// EmulatorDef::parameters is still QMap<QString,QVariant>; KeyValueListWidget
// only speaks QJsonObject. Converting at this UI boundary rather than
// changing the shared struct.

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
// Same "smart-enough freeform text" spirit as the rest of this editor
// (size/color/snap-angles already work this way) -- not a full vector
// widget, just parse-or-fall-back-to-previous-value.

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




ComponentEditorWindow::ComponentEditorWindow(const QString& rsdefPath, bool overwriteInPlace, QWidget* parent)
    : QWidget(parent, Qt::Window), overwriteInPlace(overwriteInPlace) {

    setWindowTitle("Component Editor");
    resize(1100, 720);

    QVBoxLayout* rootLayout = new QVBoxLayout(this);
    QHBoxLayout* body = new QHBoxLayout();
    rootLayout->addLayout(body, 1);

    // ---- left: scrollable section stack ----
    QScrollArea* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setMinimumWidth(420);
    scrollArea->setMaximumWidth(480);

    QWidget* leftContent = new QWidget(scrollArea);
    leftLayout = new QVBoxLayout(leftContent);
    leftLayout->setAlignment(Qt::AlignTop);
    scrollArea->setWidget(leftContent);
    body->addWidget(scrollArea);

    // ---- right: preview viewport ----
    // TODO: swap for ComponentPreviewViewport once built.
    viewportPlaceholder = new QWidget(this);
    viewportPlaceholder->setStyleSheet("background-color: #1a1a1a;");
    QVBoxLayout* vpLayout = new QVBoxLayout(viewportPlaceholder);
    QLabel* vpLabel = new QLabel("3D preview (not yet implemented)", viewportPlaceholder);
    vpLabel->setAlignment(Qt::AlignCenter);
    vpLabel->setStyleSheet("color: gray; font-style: italic;");
    vpLayout->addWidget(vpLabel);
    body->addWidget(viewportPlaceholder, 1);

    // ---- bottom: pinned Save bar, outside the scroll area ----
    QWidget* bottomBar = new QWidget(this);
    QHBoxLayout* bottomLayout = new QHBoxLayout(bottomBar);
    bottomLayout->addStretch();
    QPushButton* saveBtn = new QPushButton(overwriteInPlace ? "Save" : "Save As New Component", bottomBar);
    saveBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    bottomLayout->addWidget(saveBtn);
    rootLayout->addWidget(bottomBar);
    connect(saveBtn, &QPushButton::clicked, this, &ComponentEditorWindow::onSaveClicked);

    // ---- load ----
    QFile file(rsdefPath);
    if (file.open(QFile::ReadOnly)) {
        QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
        data = ComponentData::fromJson(root, QFileInfo(rsdefPath).absolutePath());
    } else {
        Toast::showMessage(this, "Could not open: " + rsdefPath);
    }

    if (!overwriteInPlace) {
        data.modelId = "new_component";
        data.meta.name = "New Component";
    }

    clearAndRebuild();
}




void ComponentEditorWindow::onSaveClicked() {
    bool isNewCatalogEntry = !overwriteInPlace;
    bool ok = LibraryManager::getInstance().saveComponent(data, isNewCatalogEntry);
    if (ok) {
        Toast::showMessage(this, "Saved: " + data.modelId);
    } else {
        Toast::showMessage(this, "Save failed -- check that Id is set and not already in use.");
    }
}




void ComponentEditorWindow::clearAndRebuild() {
    QLayoutItem* item;
    while ((item = leftLayout->takeAt(0)) != nullptr) {
        if (item->widget()) delete item->widget();
        delete item;
    }

    build_resources();
    build_construction();
    build_connectors();
    build_io();
    build_emulator();
    build_data();
    leftLayout->addStretch();
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




/*
 * Wraps content in a toggleable header, collapsed by default -- this is
 * what keeps Bodies/Joints/Connectors navigable once there's more than a
 * couple of each; expanding one at a time beats scrolling past a full form
 * per item just to find the one you want.
 */
QWidget* ComponentEditorWindow::makeCollapsible(const QString& headerText, QWidget* content, QWidget* parent) {
    QWidget* wrapper = new QWidget(parent);
    QVBoxLayout* wrapperLayout = new QVBoxLayout(wrapper);
    wrapperLayout->setContentsMargins(0, 0, 0, 0);
    wrapperLayout->setSpacing(2);

    QToolButton* toggle = new QToolButton(wrapper);
    toggle->setText("▸ " + headerText);
    toggle->setCheckable(true);
    toggle->setChecked(false);
    toggle->setStyleSheet("border: none; text-align: left; font-weight: bold;");
    toggle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    wrapperLayout->addWidget(toggle);

    content->setParent(wrapper);
    content->setVisible(false);
    wrapperLayout->addWidget(content);

    connect(toggle, &QToolButton::toggled, this, [toggle, content, headerText](bool checked) {
        content->setVisible(checked);
        toggle->setText((checked ? "▾ " : "▸ ") + headerText);
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
        QString path = QFileDialog::getOpenFileName(this, "Import", QString(), filter);
        if (path.isEmpty()) return;
        QString key = QFileInfo(path).completeBaseName();
        target[key] = path; // absolute source path -- copied at Save time
        clearAndRebuild();
    });

    parent->addWidget(box);
}




#pragma region construction

void ComponentEditorWindow::build_construction() {
    QGroupBox* box = new QGroupBox("Construction", this);
    QVBoxLayout* layout = new QVBoxLayout(box);

    build_bodies(layout);
    build_joints(layout);

    leftLayout->addWidget(box);
}




void ComponentEditorWindow::build_bodies(QVBoxLayout* parent) {
    QGroupBox* box = new QGroupBox("Bodies", parent->parentWidget());
    QVBoxLayout* layout = new QVBoxLayout(box);

    for (const QString& nodeId : data.bodies.keys()) {
        const Node& node = data.bodies[nodeId];

        QFrame* content = new QFrame(box);
        content->setFrameShape(QFrame::StyledPanel);
        QFormLayout* form = new QFormLayout(content);

        QLineEdit* idEdit = new QLineEdit(node.id, content);
        // Body id is referenced by joints/connectors/io elsewhere -- renaming
        // in place would require cascading the update. Left read-only for
        // this first pass; delete-and-recreate is the current workaround.
        idEdit->setReadOnly(true);
        form->addRow("Id:", idEdit);

        QDoubleSpinBox* massSpin = new QDoubleSpinBox(content);
        massSpin->setRange(0.0, 1000.0);
        massSpin->setDecimals(4);
        massSpin->setValue(node.mass);
        form->addRow("Mass (kg):", massSpin);
        connect(massSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, nodeId](double val) {
            if (data.bodies.contains(nodeId)) data.bodies[nodeId].mass = val;
        });

        TransformFieldRefs xform = addTransformFields(form, node.localTransform);
        connect(xform.posEdit, &QLineEdit::editingFinished, this, [this, nodeId, xform]() {
            if (!data.bodies.contains(nodeId)) return;
            data.bodies[nodeId].localTransform.position =
                stringToPos(xform.posEdit->text(), data.bodies[nodeId].localTransform.position);
        });
        connect(xform.rotEdit, &QLineEdit::editingFinished, this, [this, nodeId, xform]() {
            if (!data.bodies.contains(nodeId)) return;
            data.bodies[nodeId].localTransform.rotation =
                stringToRot(xform.rotEdit->text(), data.bodies[nodeId].localTransform.rotation);
        });

        build_geoms(form, nodeId);

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
            data.bodies.remove(nodeId);
            clearAndRebuild();
        });

        QString header = QString("%1  (mass %2 kg)").arg(node.id).arg(node.mass);
        layout->addWidget(makeCollapsible(header, content, box));
    }

    QPushButton* addBtn = new QPushButton("+ Add Body", box);
    addBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    layout->addWidget(addBtn, 0, Qt::AlignLeft);

    connect(addBtn, &QPushButton::clicked, this, [this]() {
        Node n;
        n.id = QString("body_%1").arg(data.bodies.size() + 1);
        data.bodies[n.id] = n;
        clearAndRebuild();
    });

    parent->addWidget(box);
}




/*
 * Per-body geom list. Mesh/material are dropdowns sourced from this
 * component's own meshResources/materialResources keys -- a geom can only
 * reference a resource that's already been imported in the Resources
 * section, never free text.
 */
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

        QLineEdit* sizeEdit = new QLineEdit(doubleListToString(g.size), row);
        sizeEdit->setPlaceholderText("comma-separated, meaning depends on type");
        form->addRow("Size:", sizeEdit);

        QLineEdit* posEdit = new QLineEdit(posToString(g.pos), row);
        form->addRow("Pos (x,y,z):", posEdit);

        QLineEdit* colorEdit = new QLineEdit(doubleListToString(g.color), row);
        colorEdit->setPlaceholderText("r, g, b[, a] -- only used if Material is (none)");
        form->addRow("Color:", colorEdit);

        auto commit = [this, bodyId, geomIdx, typeCombo, meshCombo, materialCombo, sizeEdit, posEdit, colorEdit]() {
            if (!data.bodies.contains(bodyId) || geomIdx >= data.bodies[bodyId].geoms.size()) return;
            Geom& geom = data.bodies[bodyId].geoms[geomIdx];

            geom.type = typeCombo->currentText();
            geom.mesh = (meshCombo->currentText() == "(none)") ? "" : meshCombo->currentText();
            geom.material = (materialCombo->currentText() == "(none)") ? "" : materialCombo->currentText();
            geom.size = stringToDoubleList(sizeEdit->text());
            geom.pos = stringToPos(posEdit->text(), geom.pos);
            geom.color = stringToDoubleList(colorEdit->text());
        };
        connect(typeCombo, &QComboBox::currentTextChanged, this, commit);
        connect(meshCombo, &QComboBox::currentTextChanged, this, commit);
        connect(materialCombo, &QComboBox::currentTextChanged, this, commit);
        connect(sizeEdit, &QLineEdit::editingFinished, this, commit);
        connect(posEdit, &QLineEdit::editingFinished, this, commit);
        connect(colorEdit, &QLineEdit::editingFinished, this, commit);

        QPushButton* removeBtn = new QPushButton("Remove Geom", row);
        form->addRow(removeBtn);
        connect(removeBtn, &QPushButton::clicked, this, [this, bodyId, geomIdx]() {
            if (data.bodies.contains(bodyId) && geomIdx < data.bodies[bodyId].geoms.size()) {
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
        g.type = "box";
        g.size = {0.01, 0.01, 0.01};
        data.bodies[bodyId].geoms.append(g);
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
        idEdit->setReadOnly(true); // same rename hazard as body id
        form->addRow("Id:", idEdit);

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

        QComboBox* actuatorTypeCombo = new QComboBox(advPanel);
        actuatorTypeCombo->addItems({"(none)", "position", "velocity", "motor"});
        actuatorTypeCombo->setCurrentText(edge.actuator.type.isEmpty() ? "(none)" : edge.actuator.type);
        advForm->addRow("Actuator type:", actuatorTypeCombo);

        QDoubleSpinBox* kpSpin = new QDoubleSpinBox(advPanel);
        kpSpin->setRange(0.0, 10000.0);
        kpSpin->setValue(edge.actuator.kp);
        advForm->addRow("kp:", kpSpin);

        QDoubleSpinBox* kvSpin = new QDoubleSpinBox(advPanel);
        kvSpin->setRange(0.0, 10000.0);
        kvSpin->setValue(edge.actuator.kv);
        advForm->addRow("kv:", kvSpin);

        QLineEdit* ctrlRangeEdit = new QLineEdit(doubleListToString(edge.actuator.ctrlrange), advPanel);
        ctrlRangeEdit->setPlaceholderText("min, max");
        advForm->addRow("Ctrl range:", ctrlRangeEdit);

        QLineEdit* forceRangeEdit = new QLineEdit(doubleListToString(edge.actuator.forceRange), advPanel);
        forceRangeEdit->setPlaceholderText("min, max");
        advForm->addRow("Force range:", forceRangeEdit);

        QComboBox* sensorTypeCombo = new QComboBox(advPanel);
        sensorTypeCombo->addItems({"(none)", "jointpos", "jointvel"});
        sensorTypeCombo->setCurrentText(edge.sensor.type.isEmpty() ? "(none)" : edge.sensor.type);
        advForm->addRow("Sensor type:", sensorTypeCombo);

        form->addRow(advPanel);
        connect(advToggle, &QToolButton::toggled, this, [advToggle, advPanel](bool checked) {
            advPanel->setVisible(checked);
            advToggle->setText(checked ? "Advanced ▾" : "Advanced ▸");
        });

        // All fields on one Edge -- commit together.
        auto commit = [this, edgeId, bodyACombo, bodyBCombo, typeCombo, xform, rangeEdit,
                       dampingSpin, armatureSpin, frictionSpin, collisionCheck, actuatorTypeCombo,
                       kpSpin, kvSpin, ctrlRangeEdit, forceRangeEdit, sensorTypeCombo]() {
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
            e.actuator.type = (actuatorTypeCombo->currentText() == "(none)") ? "" : actuatorTypeCombo->currentText();
            e.actuator.kp = kpSpin->value();
            e.actuator.kv = kvSpin->value();
            e.actuator.ctrlrange = stringToDoubleList(ctrlRangeEdit->text());
            e.actuator.forceRange = stringToDoubleList(forceRangeEdit->text());
            e.sensor.type = (sensorTypeCombo->currentText() == "(none)") ? "" : sensorTypeCombo->currentText();
            upsertEdge(data.joints, e);
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
        connect(actuatorTypeCombo, &QComboBox::currentTextChanged, this, commit);
        connect(kpSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, commit);
        connect(kvSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, commit);
        connect(ctrlRangeEdit, &QLineEdit::editingFinished, this, commit);
        connect(forceRangeEdit, &QLineEdit::editingFinished, this, commit);
        connect(sensorTypeCombo, &QComboBox::currentTextChanged, this, commit);

        QPushButton* removeBtn = new QPushButton("Remove Joint", content);
        form->addRow(removeBtn);
        connect(removeBtn, &QPushButton::clicked, this, [this, edgeId]() {
            for (const QString& key : data.inputDefs.keys()) {
                if (data.inputDefs[key].targetJoint == edgeId) {
                    Toast::showMessage(this, "Can't remove: input '" + key + "' targets this joint.");
                    return;
                }
            }
            for (const QString& key : data.outputDefs.keys()) {
                if (data.outputDefs[key].targetJoint == edgeId) {
                    Toast::showMessage(this, "Can't remove: output '" + key + "' targets this joint.");
                    return;
                }
            }
            removeEdgeById(data.joints, edgeId);
            clearAndRebuild();
        });

        QString header = QString("%1 — %2 (%3 → %4)").arg(edge.id, edge.type, edge.bodyA, edge.bodyB);
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
    for (const Edge& e : data.joints) {
        combo->addItem(e.id);
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
        auto commit = [this, connKey, bodyCombo, descEdit, xform, snapAnglesEdit]() {
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
        layout->addWidget(makeCollapsible(header, content, box));
    }

    QPushButton* addBtn = new QPushButton("+ Add Connector", box);
    addBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    layout->addWidget(addBtn, 0, Qt::AlignLeft);

    connect(addBtn, &QPushButton::clicked, this, [this]() {
        ConnectorDef def;
        def.id = QString("connector_%1").arg(data.connectors.size() + 1);
        data.connectors[def.id] = def;
        clearAndRebuild();
    });

    leftLayout->addWidget(box);
}




#pragma region io

void ComponentEditorWindow::build_io() {
    QGroupBox* box = new QGroupBox("IO (control-surface channels)", this);
    QVBoxLayout* layout = new QVBoxLayout(box);

    build_io_list(layout, "Inputs", data.inputDefs);
    build_io_list(layout, "Outputs", data.outputDefs);

    leftLayout->addWidget(box);
}




void ComponentEditorWindow::build_io_list(QVBoxLayout* parent, const QString& title, QMap<QString, IODef>& target) {
    QGroupBox* box = new QGroupBox(title, parent->parentWidget());
    QVBoxLayout* layout = new QVBoxLayout(box);

    for (const QString& key : target.keys()) {
        IODef def = target[key];

        QFrame* row = new QFrame(box);
        row->setFrameShape(QFrame::StyledPanel);
        QFormLayout* form = new QFormLayout(row);

        QLineEdit* nameEdit = new QLineEdit(def.name, row);
        form->addRow("Name:", nameEdit);

        QLineEdit* unitEdit = new QLineEdit(def.unit, row);
        form->addRow("Unit:", unitEdit);

        QComboBox* channelTypeCombo = new QComboBox(row);
        channelTypeCombo->addItems({"scalar", "vector", "image"});
        channelTypeCombo->setCurrentText(def.channelType.isEmpty() ? "scalar" : def.channelType);
        form->addRow("Channel type:", channelTypeCombo);

        QCheckBox* physicalCheck = new QCheckBox(row);
        physicalCheck->setChecked(def.physical);
        form->addRow("Physical:", physicalCheck);

        QComboBox* jointCombo = new QComboBox(row);
        refreshJointDropdown(jointCombo);
        jointCombo->setCurrentText(def.targetJoint);
        form->addRow("Target joint:", jointCombo);

        QString ioKey = key;
        auto commit = [this, &target, ioKey, unitEdit, channelTypeCombo, physicalCheck, jointCombo]() {
            IODef d = target[ioKey];
            d.unit = unitEdit->text();
            d.channelType = channelTypeCombo->currentText();
            d.physical = physicalCheck->isChecked();
            d.targetJoint = jointCombo->currentText();
            target[ioKey] = d;
        };
        connect(unitEdit, &QLineEdit::editingFinished, this, commit);
        connect(channelTypeCombo, &QComboBox::currentTextChanged, this, commit);
        connect(physicalCheck, &QCheckBox::toggled, this, commit);
        connect(jointCombo, &QComboBox::currentTextChanged, this, commit);

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

    connect(addBtn, &QPushButton::clicked, this, [this, &target]() {
        IODef def;
        def.name = QString("channel_%1").arg(target.size() + 1);
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
    QGroupBox* box = new QGroupBox("Data", this);
    QVBoxLayout* layout = new QVBoxLayout(box);

    // ---- meta ----
    QGroupBox* metaBox = new QGroupBox("Meta", box);
    QFormLayout* metaForm = new QFormLayout(metaBox);

    QLineEdit* idEdit = new QLineEdit(data.modelId, metaBox);
    idEdit->setPlaceholderText("unique id -- also the save folder/file name");
    if (overwriteInPlace) {
        // Locked: the save destination is derived as basePath + modelId +
        // ".rsdef" with no separate fixed path to fall back on, so
        // changing id here would silently redirect Save to a different,
        // uncataloged location instead of updating the file being edited.
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
        QString path = QFileDialog::getOpenFileName(this, "Select Icon", QString(), "Images (*.png *.jpg)");
        if (!path.isEmpty()) { iconEdit->setText(path); data.meta.iconPath = path; }
    });
    connect(iconEdit, &QLineEdit::editingFinished, this, [this, iconEdit]() { data.meta.iconPath = iconEdit->text(); });

    layout->addWidget(metaBox);

    // ---- specs ----
    QGroupBox* specsBox = new QGroupBox("Specs (freeform)", box);
    QVBoxLayout* specsLayout = new QVBoxLayout(specsBox);
    KeyValueListWidget* specsWidget = new KeyValueListWidget(specsBox);
    specsWidget->setValue(data.specs);
    specsLayout->addWidget(specsWidget);
    connect(specsWidget, &KeyValueListWidget::changed, this, [this, specsWidget]() { data.specs = specsWidget->value(); });
    layout->addWidget(specsBox);

    // ---- pins ----
    // Freeform for now, per the earlier decision to defer the structured
    // Pin form until vision-sensor work forces a more robust shape.
    QGroupBox* pinsBox = new QGroupBox("Pins (freeform for now)", box);
    QVBoxLayout* pinsLayout = new QVBoxLayout(pinsBox);
    KeyValueListWidget* pinsWidget = new KeyValueListWidget(pinsBox);
    pinsWidget->setValue(data.pins);
    pinsLayout->addWidget(pinsWidget);
    connect(pinsWidget, &KeyValueListWidget::changed, this, [this, pinsWidget]() { data.pins = pinsWidget->value(); });
    layout->addWidget(pinsBox);

    leftLayout->addWidget(box);
}
