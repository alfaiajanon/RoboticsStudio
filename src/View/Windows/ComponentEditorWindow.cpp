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
    tabWidget->addTab(makeTabPage(ioLayout), "IO");
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
        clearLayout(ioLayout);
        clearLayout(emulatorLayout);

        leftLayout = constructionLayout;
        build_resources();
        build_construction();
        build_connectors();
        constructionLayout->addStretch();

        leftLayout = ioLayout;
        build_io();
        ioLayout->addStretch();

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
                for (const QString& key : data.outputDefs.keys()) {
                    if (data.outputDefs[key].targetSite == s.id) {
                        Toast::showMessage(this, "Can't remove: output '" + key + "' targets site '" + s.id + "' on this body.");
                        return;
                    }
                }
                for (const QString& key : data.inputDefs.keys()) {
                    if (data.inputDefs[key].targetSite == s.id) {
                        Toast::showMessage(this, "Can't remove: input '" + key + "' targets site '" + s.id + "' on this body.");
                        return;
                    }
                }
            }
            data.bodies.remove(nodeId);
            bodyGeomOverride.remove(nodeId);
            clearAndRebuild();
        });

        QString header = QString("%1  (mass %2 kg)").arg(node.id).arg(node.mass);
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
        g.mass = 0.0; // Geom::mass has no default initializer
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

        QComboBox* sensorTypeCombo = new QComboBox(row);
        sensorTypeCombo->addItems({"(none)", "accelerometer", "gyro", "magnetometer",
                                    "velocimeter", "force", "torque", "framepos",
                                    "framequat", "rangefinder"});
        sensorTypeCombo->setCurrentText(s.sensor.type.isEmpty() ? "(none)" : s.sensor.type);
        form->addRow("Sensor type:", sensorTypeCombo);

        auto commit = [this, bodyId, siteIdx, xform, sensorTypeCombo]() {
            if (!data.bodies.contains(bodyId) || siteIdx >= data.bodies[bodyId].sites.size()) return;
            Site& site = data.bodies[bodyId].sites[siteIdx];
            site.localTransform.position = stringToPos(xform.posEdit->text(), site.localTransform.position);
            site.localTransform.rotation = stringToRot(xform.rotEdit->text(), site.localTransform.rotation);
            site.sensor.type = (sensorTypeCombo->currentText() == "(none)") ? "" : sensorTypeCombo->currentText();
            schedulePreviewReload();
        };
        connect(xform.posEdit, &QLineEdit::editingFinished, this, commit);
        connect(xform.rotEdit, &QLineEdit::editingFinished, this, commit);
        connect(sensorTypeCombo, &QComboBox::currentTextChanged, this, commit);

        QPushButton* removeBtn = new QPushButton("Remove Site", row);
        form->addRow(removeBtn);
        QString siteId = s.id;
        connect(removeBtn, &QPushButton::clicked, this, [this, bodyId, siteId]() {
            for (const QString& key : data.outputDefs.keys()) {
                if (data.outputDefs[key].targetSite == siteId) {
                    Toast::showMessage(this, "Can't remove: output '" + key + "' targets this site.");
                    return;
                }
            }
            for (const QString& key : data.inputDefs.keys()) {
                if (data.inputDefs[key].targetSite == siteId) {
                    Toast::showMessage(this, "Can't remove: input '" + key + "' targets this site.");
                    return;
                }
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





#pragma region io

void ComponentEditorWindow::build_io() {
    build_io_list(leftLayout, "Inputs", data.inputDefs);
    build_io_list(leftLayout, "Outputs", data.outputDefs);
}




void ComponentEditorWindow::build_io_list(QVBoxLayout* parent, const QString& title, QMap<QString, IODef>& target) {
    QGroupBox* box = new QGroupBox(title, parent->parentWidget());
    QVBoxLayout* layout = new QVBoxLayout(box);

    const bool isInput = (title == "Inputs");

    for (const QString& key : target.keys()) {
        IODef def = target[key];

        QFrame* row = new QFrame(box);
        row->setFrameShape(QFrame::StyledPanel);
        QFormLayout* form = new QFormLayout(row);

        QLineEdit* nameEdit = new QLineEdit(def.name, row);
        form->addRow("Name:", nameEdit);
        connect(nameEdit, &QLineEdit::editingFinished, this, [this, &target, key, nameEdit]() {
            QString newName = nameEdit->text().trimmed();
            if (newName.isEmpty() || newName == key || target.contains(newName)) return;
            IODef d = target.take(key);
            d.name = newName;
            target[newName] = d;
            clearAndRebuild();
        });

        QLineEdit* unitEdit = new QLineEdit(def.unit, row);
        form->addRow("Unit:", unitEdit);

        QComboBox* rangeModeCombo = nullptr;
        QLineEdit* rangeEdit = nullptr;
        if (isInput) {
            rangeModeCombo = new QComboBox(row);
            rangeModeCombo->addItems({"ranged", "unranged"});
            rangeModeCombo->setCurrentText(def.ranged ? "ranged" : "unranged");
            form->addRow("Range mode:", rangeModeCombo);

            rangeEdit = new QLineEdit(QString("%1, %2").arg(def.range.first).arg(def.range.second), row);
            rangeEdit->setPlaceholderText("min, max");
            rangeEdit->setEnabled(def.ranged);
            form->addRow("Range:", rangeEdit);
        }

        QComboBox* channelTypeCombo = new QComboBox(row);
        channelTypeCombo->addItems({"scalar", "vector", "image"});
        channelTypeCombo->setCurrentText(def.channelType.isEmpty() ? "scalar" : def.channelType);
        form->addRow("Channel type:", channelTypeCombo);

        QCheckBox* physicalCheck = new QCheckBox(row);
        physicalCheck->setChecked(def.physical);
        form->addRow("Physical:", physicalCheck);

        QComboBox* jointCombo = new QComboBox(row);
        refreshJointDropdown(jointCombo);
        jointCombo->setCurrentText(def.targetJoint.isEmpty() ? "(none)" : def.targetJoint);
        form->addRow("Target joint:", jointCombo);

        QComboBox* siteCombo = new QComboBox(row);
        refreshSiteDropdown(siteCombo);
        siteCombo->setCurrentText(def.targetSite.isEmpty() ? "(none)" : def.targetSite);
        form->addRow("Target site:", siteCombo);

        QString ioKey = key;
        auto commit = [this, &target, ioKey, unitEdit, rangeModeCombo, rangeEdit, channelTypeCombo,
                       physicalCheck, jointCombo, siteCombo]() {
            IODef d = target[ioKey];
            d.unit = unitEdit->text();
            d.channelType = channelTypeCombo->currentText();
            d.physical = physicalCheck->isChecked();

            if (rangeModeCombo && rangeEdit) {
                d.ranged = (rangeModeCombo->currentText() == "ranged");
                rangeEdit->setEnabled(d.ranged);
                QList<double> r = stringToDoubleList(rangeEdit->text());
                if (r.size() == 2) {
                    d.range = qMakePair(static_cast<float>(r[0]), static_cast<float>(r[1]));
                }
            }

            QString siteSel = siteCombo->currentText();
            QString jointSel = jointCombo->currentText();
            if (siteSel != "(none)") {
                d.targetSite = siteSel;
                d.targetJoint = "";
            } else if (jointSel != "(none)") {
                d.targetJoint = jointSel;
                d.targetSite = "";
            } else {
                d.targetJoint = "";
                d.targetSite = "";
            }

            target[ioKey] = d;
        };
        connect(unitEdit, &QLineEdit::editingFinished, this, commit);
        if (rangeModeCombo) connect(rangeModeCombo, &QComboBox::currentTextChanged, this, commit);
        if (rangeEdit) connect(rangeEdit, &QLineEdit::editingFinished, this, commit);
        connect(channelTypeCombo, &QComboBox::currentTextChanged, this, commit);
        connect(physicalCheck, &QCheckBox::toggled, this, commit);
        connect(jointCombo, &QComboBox::currentTextChanged, this, commit);
        connect(siteCombo, &QComboBox::currentTextChanged, this, commit);

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
