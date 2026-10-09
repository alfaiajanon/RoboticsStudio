#include "StagePanel.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QColorDialog>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTextStream>
#include <QUndoStack>
#include <QVBoxLayout>

#include "Application/Application.h"
#include "Commands/GenericCommand.h"
#include "Simulation/SimulationManager.h"
#include "View/Widgets/Toast.h"



static const char* STAGE_SCRIPT_TEMPLATE = R"(// Stage script: drives the controlled props, next to (not inside) the robot.
// Globals:
//   prop_<id>  one per prop whose motion is "controlled"
//     setVelocity(vx, vy)        world frame, m/s
//     move(forward, strafe)      prop frame, m/s
//     setYawRate(degPerSec)
//     stop()
//     teleport(x, y, z, yawDeg)
//     getPosition() -> {x, y, z}, getVelocity() -> {x, y, z}, getYaw() -> deg
//   robot      read-only robot pose
//     getPosition() -> {x, y, z}, getRotation() -> {roll, pitch, yaw} (deg)

function loop() {
    delay(20);
}
)";



StagePanel::StagePanel(QWidget* parent) : QWidget(parent) {
    setupUI();
}



#pragma region ui

void StagePanel::setupUI() {
    QVBoxLayout* main = new QVBoxLayout(this);
    main->setContentsMargins(0, 0, 0, 0);
    main->setSpacing(0);
    main->addWidget(buildToolbar());

    QWidget* content = new QWidget();
    // Row 0: Environment | Floor, equal widths, same height (no alignment, so
    // both fill the row). Row 1: Props across the full width.
    QGridLayout* grid = new QGridLayout(content);
    grid->setContentsMargins(6, 6, 6, 6);
    grid->addWidget(buildEnvironment(), 0, 0);
    grid->addWidget(buildFloor(), 0, 1);
    grid->addWidget(buildProps(), 1, 0, 1, 2);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    grid->setRowStretch(2, 1);   // spare height goes below, not into the groups

    QScrollArea* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(content);
    main->addWidget(scroll);
}


QWidget* StagePanel::buildToolbar() {
    QWidget* toolbar = new QWidget();
    toolbar->setStyleSheet("background-color: #2d2d2d; border-bottom: 1px solid #1e1e1e;");
    QHBoxLayout* layout = new QHBoxLayout(toolbar);
    layout->setContentsMargins(8, 4, 8, 4);

    scriptLabel = new QLabel("Stage script: none");
    scriptLabel->setStyleSheet("color: #cccccc;");

    QPushButton* editBtn = new QPushButton("Edit Script");
    QPushButton* chooseBtn = new QPushButton("Choose...");
    QPushButton* clearBtn = new QPushButton("Clear");
    editBtn->setToolTip("Open the stage script in the Script tab (creates stage.js if there is none)");

    connect(editBtn, &QPushButton::clicked, this, &StagePanel::editStageScript);
    connect(chooseBtn, &QPushButton::clicked, this, [this]() {
        QString path = QFileDialog::getOpenFileName(this, "Choose Stage Script",
            Application::getInstance()->getProject()->getProjectDirectory(), "JavaScript (*.js)");
        if (path.isEmpty()) return;
        QString rel = importIntoProject(path);
        if (!rel.isEmpty()) commit([rel](Stage::Data& d) { d.script = rel; });
    });
    connect(clearBtn, &QPushButton::clicked, this, [this]() {
        commit([](Stage::Data& d) { d.script.clear(); });
    });

    layout->addWidget(scriptLabel);
    layout->addStretch();
    layout->addWidget(editBtn);
    layout->addWidget(chooseBtn);
    layout->addWidget(clearBtn);
    return toolbar;
}


QWidget* StagePanel::buildEnvironment() {
    QGroupBox* box = new QGroupBox("Environment");
    QFormLayout* f = envForm = new QFormLayout(box);
    using D = Stage::Data;

    addColor(f, "Sky top",    Field<Vec3>([](D& d) { return &d.env.skyTop; }));
    addColor(f, "Sky bottom", Field<Vec3>([](D& d) { return &d.env.skyBottom; }));
    addColor(f, "Ambient",    Field<Vec3>([](D& d) { return &d.env.ambient; }));
    addColor(f, "Headlight",  Field<Vec3>([](D& d) { return &d.env.headlight; }));
    addColor(f, "Light",      Field<Vec3>([](D& d) { return &d.env.lightDiffuse; }));
    addVec3(f, "Light dir", [](D& d) { return &d.env.lightDir; }, -1, 1, 0.1, 2, "lightDir");
    addBool(f, "Shadows", [](D& d) { return &d.env.castShadow; });
    addInt(f, "Shadow size", [](D& d) { return &d.env.shadowSize; }, 256, 16384, "shadowSize");
    addBool(f, "Fog", [](D& d) { return &d.env.fog; });
    rows["fogStart"] = addDouble(f, "Fog start", [](D& d) { return &d.env.fogStart; }, 0, 100, 0.5, 1, "fogStart");
    rows["fogEnd"]   = addDouble(f, "Fog end",   [](D& d) { return &d.env.fogEnd; }, 0, 100, 0.5, 1, "fogEnd");
    rows["fogColor"] = addColor(f, "Fog color", Field<Vec3>([](D& d) { return &d.env.fogColor; }));
    addVec3(f, "Gravity", [](D& d) { return &d.env.gravity; }, -100, 100, 0.1, 2, "gravity");
    addDouble(f, "Timestep", [](D& d) { return &d.env.timestep; }, 0.0001, 0.02, 0.0005, 4, "timestep");
    return box;
}


QWidget* StagePanel::buildFloor() {
    QGroupBox* box = new QGroupBox("Floor");
    QFormLayout* f = floorForm = new QFormLayout(box);
    using D = Stage::Data;

    addChoice(f, "Type", [](D& d) { return &d.floor.type; }, {"plane", "mesh", "heightfield", "none"});
    rows["floor.pattern"] = addChoice(f, "Pattern", [](D& d) { return &d.floor.pattern; }, {"checker", "solid"});
    rows["floor.color1"] = addColor(f, "Color", Field<Vec3>([](D& d) { return &d.floor.color1; }));
    rows["floor.color2"] = addColor(f, "Color 2", Field<Vec3>([](D& d) { return &d.floor.color2; }));
    rows["floor.texRepeat"] = addDouble(f, "Tex repeat", [](D& d) { return &d.floor.texRepeat; }, 0.1, 1000, 1, 1, "texRepeat");
    rows["floor.file"] = addFile(f, "File", [](D& d) { return &d.floor.file; },
                                 "Mesh or heightfield (*.stl *.obj *.msh *.png)");
    rows["floor.file"]->setToolTip("Mesh floors collide as their convex hull; use a heightfield PNG for terrain.");
    rows["floor.scale"] = addVec3(f, "Scale", [](D& d) { return &d.floor.scale; }, 0.001, 1000, 0.1, 3, "floorScale");
    rows["floor.scale"]->setToolTip("Mesh: scale. Heightfield: half size x, half size y, max height (m).");
    rows["floor.pos"] = addVec3(f, "Position", [](D& d) { return &d.floor.pos; }, -100, 100, 0.01, 3, "floorPos");
    rows["floor.euler"] = addVec3(f, "Rotation", [](D& d) { return &d.floor.euler; }, -360, 360, 5, 1, "floorEuler");
    rows["floor.friction"] = addVec3(f, "Friction", [](D& d) { return &d.floor.friction; }, 0, 10, 0.05, 4, "floorFriction");
    addVec3(f, "Robot spawn", [](D& d) { return &d.robotSpawn; }, -100, 100, 0.01, 3, "robotSpawn");
    return box;
}


QWidget* StagePanel::buildProps() {
    QGroupBox* box = new QGroupBox("Props");
    QHBoxLayout* layout = new QHBoxLayout(box);

    // list + buttons
    QVBoxLayout* listCol = new QVBoxLayout();
    propList = new QListWidget();
    propList->setMinimumWidth(140);
    listCol->addWidget(propList);

    QHBoxLayout* btns = new QHBoxLayout();
    QPushButton* addBtn = new QPushButton("Add");
    QPushButton* dupBtn = new QPushButton("Duplicate");
    QPushButton* removeBtn = new QPushButton("Remove");
    btns->addWidget(addBtn);
    btns->addWidget(dupBtn);
    btns->addWidget(removeBtn);
    listCol->addLayout(btns);
    layout->addLayout(listCol);

    QMenu* shapeMenu = new QMenu(addBtn);
    for (const QString& shape : {"box", "sphere", "cylinder", "capsule", "mesh"}) {
        shapeMenu->addAction(shape, this, [this, shape]() {
            Stage::Prop p;
            p.shape = shape;
            if (shape == "mesh") {
                QString path = QFileDialog::getOpenFileName(this, "Import Prop Mesh",
                    Application::getInstance()->getProject()->getProjectDirectory(), "Meshes (*.stl *.obj *.msh)");
                if (path.isEmpty()) return;
                p.mesh = importIntoProject(path);
                if (p.mesh.isEmpty()) return;
                p.size = {1, 1, 1};
            } else if (shape == "sphere") {
                p.size = {0.05, 0, 0};
            } else if (shape == "cylinder" || shape == "capsule") {
                p.size = {0.03, 0.05, 0};
            }

            const Stage::Data& cur = Application::getInstance()->getProject()->getStage()->getData();
            QSet<QString> taken;
            for (const Stage::Prop& q : cur.props) taken.insert(q.id);
            int n = 1;
            while (taken.contains(shape + QString::number(n))) ++n;
            p.id = shape + QString::number(n);
            p.pos = {0.3, 0.15 * cur.props.size(), 0.2};

            currentProp = cur.props.size();
            commit([p](Stage::Data& d) { d.props.append(p); });
        });
    }
    addBtn->setMenu(shapeMenu);

    connect(dupBtn, &QPushButton::clicked, this, [this]() {
        const Stage::Data& cur = Application::getInstance()->getProject()->getStage()->getData();
        if (currentProp < 0 || currentProp >= cur.props.size()) return;
        Stage::Prop p = cur.props[currentProp];
        QSet<QString> taken;
        for (const Stage::Prop& q : cur.props) taken.insert(q.id);
        QString base = p.id;
        int n = 2;
        while (taken.contains(base + "_" + QString::number(n))) ++n;
        p.id = base + "_" + QString::number(n);
        p.pos[1] += 0.15;
        currentProp = cur.props.size();
        commit([p](Stage::Data& d) { d.props.append(p); });
    });

    connect(removeBtn, &QPushButton::clicked, this, [this]() {
        int idx = currentProp;
        if (idx < 0) return;
        commit([idx](Stage::Data& d) { if (idx < d.props.size()) d.props.removeAt(idx); });
    });

    connect(propList, &QListWidget::currentRowChanged, this, [this](int row) {
        if (updating) return;
        currentProp = row;
        refresh();
    });

    // selected prop editor
    propEditor = new QWidget();
    QFormLayout* f = propForm = new QFormLayout(propEditor);
    f->setContentsMargins(0, 0, 0, 0);
    using D = Stage::Data;

    QLineEdit* idEdit = new QLineEdit();
    idEdit->setToolTip("Script name: prop_<id>. Letters, digits and '_'.");
    connect(idEdit, &QLineEdit::editingFinished, this, [this, idEdit]() {
        QString id = idEdit->text().trimmed();
        commit([this, id](D& d) { if (Stage::Prop* p = selectedProp(d)) p->id = id; });
    });
    loaders.append([this, idEdit](D& d) {
        Stage::Prop* p = selectedProp(d);
        idEdit->setText(p ? p->id : QString());
    });
    f->addRow("Id", idEdit);

    addChoice(f, "Shape", [this](D& d) { Stage::Prop* p = selectedProp(d); return p ? &p->shape : nullptr; },
              {"box", "sphere", "cylinder", "capsule", "mesh"});
    QWidget* size = addVec3(f, "Size", [this](D& d) { Stage::Prop* p = selectedProp(d); return p ? &p->size : nullptr; },
                            0, 100, 0.005, 3, "prop.size");
    size->setToolTip("MuJoCo geom size: box half extents; sphere radius; cylinder/capsule radius, half length; mesh scale.");
    rows["prop.mesh"] = addFile(f, "Mesh", [this](D& d) { Stage::Prop* p = selectedProp(d); return p ? &p->mesh : nullptr; },
                                "Meshes (*.stl *.obj *.msh)");
    addVec3(f, "Position", [this](D& d) { Stage::Prop* p = selectedProp(d); return p ? &p->pos : nullptr; },
            -100, 100, 0.01, 3, "prop.pos");
    addVec3(f, "Rotation", [this](D& d) { Stage::Prop* p = selectedProp(d); return p ? &p->euler : nullptr; },
            -360, 360, 5, 1, "prop.euler");
    addColor(f, "Color", Field<Vec4>([this](D& d) { Stage::Prop* p = selectedProp(d); return p ? &p->rgba : nullptr; }));
    addVec3(f, "Friction", [this](D& d) { Stage::Prop* p = selectedProp(d); return p ? &p->friction : nullptr; },
            0, 10, 0.05, 4, "prop.friction");
    addChoice(f, "Motion", [this](D& d) { Stage::Prop* p = selectedProp(d); return p ? &p->motion : nullptr; },
              {"static", "dynamic", "controlled"});
    rows["prop.mass"] = addDouble(f, "Mass (kg)", [this](D& d) { Stage::Prop* p = selectedProp(d); return p ? &p->mass : nullptr; },
                                  0.001, 1000, 0.05, 3, "prop.mass");
    rows["prop.maxSpeed"] = addDouble(f, "Max speed (m/s)", [this](D& d) { Stage::Prop* p = selectedProp(d); return p ? &p->maxSpeed : nullptr; },
                                      0, 100, 0.1, 2, "prop.maxSpeed");
    rows["prop.maxYawRate"] = addDouble(f, "Max yaw (deg/s)", [this](D& d) { Stage::Prop* p = selectedProp(d); return p ? &p->maxYawRate : nullptr; },
                                        0, 3600, 10, 0, "prop.maxYawRate");
    rows["prop.response"] = addDouble(f, "Response (1/s)", [this](D& d) { Stage::Prop* p = selectedProp(d); return p ? &p->response : nullptr; },
                                      1, 400, 5, 0, "prop.response");
    rows["prop.response"]->setToolTip("How hard the prop tracks its commanded velocity. Keep response x timestep below 2.");

    layout->addWidget(propEditor, 1);
    return box;
}







#pragma region binders

// Same as InspectorPanel: an unfocused spinbox/combo hands the wheel to its
// parent, so scrolling the panel never changes a value by accident.
bool StagePanel::eventFilter(QObject* obj, QEvent* event) {
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


static void paintColorButton(QPushButton* btn, const QColor& c) {
    btn->setStyleSheet(QString("background-color: %1; border: 1px solid #555; min-width: 40px; min-height: 16px;").arg(c.name()));
    btn->setProperty("color", c);
}


QWidget* StagePanel::addDouble(QFormLayout* form, const QString& label, Field<double> field,
                               double min, double max, double step, int decimals, const QString& key) {
    QDoubleSpinBox* sb = new QDoubleSpinBox();
    sb->setRange(min, max);
    sb->setSingleStep(step);
    sb->setDecimals(decimals);
    sb->setKeyboardTracking(false);
    sb->setFocusPolicy(Qt::StrongFocus);
    sb->installEventFilter(this);
    connect(sb, &QDoubleSpinBox::valueChanged, this, [this, field, key](double v) {
        commit([field, v](Stage::Data& d) { if (double* p = field(d)) *p = v; }, key);
    });
    loaders.append([sb, field](Stage::Data& d) { if (double* p = field(d)) sb->setValue(*p); });
    form->addRow(label, sb);
    return sb;
}


QWidget* StagePanel::addVec3(QFormLayout* form, const QString& label, Field<Vec3> field,
                             double min, double max, double step, int decimals, const QString& key) {
    QWidget* row = new QWidget();
    QHBoxLayout* h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(2);
    for (int i = 0; i < 3; ++i) {
        QDoubleSpinBox* sb = new QDoubleSpinBox();
        sb->setRange(min, max);
        sb->setSingleStep(step);
        sb->setDecimals(decimals);
        sb->setKeyboardTracking(false);
        sb->setFocusPolicy(Qt::StrongFocus);
        sb->installEventFilter(this);
        connect(sb, &QDoubleSpinBox::valueChanged, this, [this, field, key, i](double v) {
            commit([field, v, i](Stage::Data& d) { if (Vec3* p = field(d)) (*p)[i] = v; }, key + QString::number(i));
        });
        loaders.append([sb, field, i](Stage::Data& d) { if (Vec3* p = field(d)) sb->setValue((*p)[i]); });
        h->addWidget(sb);
    }
    form->addRow(label, row);
    return row;
}


QWidget* StagePanel::addColor(QFormLayout* form, const QString& label, Field<Vec3> field) {
    QPushButton* btn = new QPushButton();
    connect(btn, &QPushButton::clicked, this, [this, btn, field, label]() {
        QColor c = QColorDialog::getColor(btn->property("color").value<QColor>(), this, label,
                                          QColorDialog::DontUseNativeDialog);
        if (!c.isValid()) return;
        Vec3 v{c.redF(), c.greenF(), c.blueF()};
        commit([field, v](Stage::Data& d) { if (Vec3* p = field(d)) *p = v; });
    });
    loaders.append([btn, field](Stage::Data& d) {
        if (Vec3* p = field(d)) paintColorButton(btn, QColor::fromRgbF((*p)[0], (*p)[1], (*p)[2]));
    });
    form->addRow(label, btn);
    return btn;
}


QWidget* StagePanel::addColor(QFormLayout* form, const QString& label, Field<Vec4> field) {
    QPushButton* btn = new QPushButton();
    connect(btn, &QPushButton::clicked, this, [this, btn, field, label]() {
        QColor c = QColorDialog::getColor(btn->property("color").value<QColor>(), this, label,
                                          QColorDialog::ShowAlphaChannel | QColorDialog::DontUseNativeDialog);
        if (!c.isValid()) return;
        Vec4 v{c.redF(), c.greenF(), c.blueF(), c.alphaF()};
        commit([field, v](Stage::Data& d) { if (Vec4* p = field(d)) *p = v; });
    });
    loaders.append([btn, field](Stage::Data& d) {
        if (Vec4* p = field(d)) paintColorButton(btn, QColor::fromRgbF((*p)[0], (*p)[1], (*p)[2], (*p)[3]));
    });
    form->addRow(label, btn);
    return btn;
}


QWidget* StagePanel::addBool(QFormLayout* form, const QString& label, Field<bool> field) {
    QCheckBox* cb = new QCheckBox();
    connect(cb, &QCheckBox::toggled, this, [this, field](bool v) {
        commit([field, v](Stage::Data& d) { if (bool* p = field(d)) *p = v; });
    });
    loaders.append([cb, field](Stage::Data& d) { if (bool* p = field(d)) cb->setChecked(*p); });
    form->addRow(label, cb);
    return cb;
}


QWidget* StagePanel::addInt(QFormLayout* form, const QString& label, Field<int> field, int min, int max, const QString& key) {
    QSpinBox* sb = new QSpinBox();
    sb->setRange(min, max);
    sb->setKeyboardTracking(false);
    sb->setFocusPolicy(Qt::StrongFocus);
    sb->installEventFilter(this);
    connect(sb, &QSpinBox::valueChanged, this, [this, field, key](int v) {
        commit([field, v](Stage::Data& d) { if (int* p = field(d)) *p = v; }, key);
    });
    loaders.append([sb, field](Stage::Data& d) { if (int* p = field(d)) sb->setValue(*p); });
    form->addRow(label, sb);
    return sb;
}


QWidget* StagePanel::addChoice(QFormLayout* form, const QString& label, Field<QString> field, const QStringList& choices) {
    QComboBox* combo = new QComboBox();
    combo->addItems(choices);
    combo->setFocusPolicy(Qt::StrongFocus);
    combo->installEventFilter(this);
    connect(combo, &QComboBox::currentTextChanged, this, [this, field](const QString& v) {
        commit([field, v](Stage::Data& d) { if (QString* p = field(d)) *p = v; });
    });
    loaders.append([combo, field](Stage::Data& d) { if (QString* p = field(d)) combo->setCurrentText(*p); });
    form->addRow(label, combo);
    return combo;
}


QWidget* StagePanel::addFile(QFormLayout* form, const QString& label, Field<QString> field, const QString& filter) {
    QWidget* row = new QWidget();
    QHBoxLayout* h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    QLineEdit* path = new QLineEdit();
    path->setReadOnly(true);
    QPushButton* browse = new QPushButton("Import...");
    h->addWidget(path, 1);
    h->addWidget(browse);

    connect(browse, &QPushButton::clicked, this, [this, field, filter, label]() {
        QString file = QFileDialog::getOpenFileName(this, "Import " + label,
            Application::getInstance()->getProject()->getProjectDirectory(), filter);
        if (file.isEmpty()) return;
        QString rel = importIntoProject(file);
        if (rel.isEmpty()) return;
        commit([field, rel](Stage::Data& d) { if (QString* p = field(d)) *p = rel; });
    });
    loaders.append([path, field](Stage::Data& d) { if (QString* p = field(d)) path->setText(*p); });
    form->addRow(label, row);
    return row;
}







#pragma region logic

Stage::Prop* StagePanel::selectedProp(Stage::Data& d) {
    if (currentProp < 0 || currentProp >= d.props.size()) return nullptr;
    return &d.props[currentProp];
}


void StagePanel::commit(const std::function<void(Stage::Data&)>& edit, const QString& mergeKey) {
    if (updating) return;
    Application* app = Application::getInstance();
    if (!app) return;

    Stage::Data before = app->getProject()->getStage()->getData();
    Stage::Data after = before;
    edit(after);

    QString err = Stage::validate(after);
    if (!err.isEmpty()) {
        Toast::showMessage(window(), err, 3000);
        refresh();
        return;
    }

    // Prop fields merge per prop, so editing two props stays two undo steps.
    QString key = mergeKey;
    if (key.startsWith("prop.")) key += "#" + QString::number(currentProp);

    auto apply = [](const Stage::Data& d) {
        Application* app = Application::getInstance();
        {
            // the physics thread reads the prop handles
            std::lock_guard<std::mutex> lock(app->getSimulationManager()->physicsMutex);
            app->getProject()->getStage()->setData(d);
        }
        app->reloadSimulation();   // also schedules the editor refresh -> refresh()
    };
    app->getUndoStack()->push(new GenericCommand(
        [apply, after]() { apply(after); },
        [apply, before]() { apply(before); },
        "Edit stage", key.isEmpty() ? QString() : "stage:" + key));
}


void StagePanel::refresh() {
    Application* app = Application::getInstance();
    if (!app) return;

    Stage::Data d = app->getProject()->getStage()->getData();
    updating = true;

    {
        QSignalBlocker block(propList);
        propList->clear();
        for (const Stage::Prop& p : d.props) propList->addItem(p.id + "  (" + p.motion + ")");
        if (currentProp >= d.props.size()) currentProp = d.props.size() - 1;
        propList->setCurrentRow(currentProp);
    }
    propEditor->setEnabled(currentProp >= 0);

    for (const auto& load : loaders) load(d);
    updateVisibility(d);

    scriptLabel->setText("Stage script: " + (d.script.isEmpty() ? QString("none") : d.script));
    updating = false;
}


void StagePanel::updateVisibility(const Stage::Data& d) {
    const Stage::Floor& fl = d.floor;
    bool plane = fl.type == "plane", none = fl.type == "none";
    bool fileFloor = fl.type == "mesh" || fl.type == "heightfield";
    floorForm->setRowVisible(rows["floor.pattern"], plane);
    floorForm->setRowVisible(rows["floor.color1"], !none);
    floorForm->setRowVisible(rows["floor.color2"], plane && fl.pattern == "checker");
    floorForm->setRowVisible(rows["floor.texRepeat"], plane && fl.pattern == "checker");
    floorForm->setRowVisible(rows["floor.file"], fileFloor);
    floorForm->setRowVisible(rows["floor.scale"], fileFloor);
    floorForm->setRowVisible(rows["floor.pos"], !none);
    floorForm->setRowVisible(rows["floor.euler"], !none);
    floorForm->setRowVisible(rows["floor.friction"], !none);

    for (const char* k : {"fogStart", "fogEnd", "fogColor"}) envForm->setRowVisible(rows[k], d.env.fog);

    const Stage::Prop* p = (currentProp >= 0 && currentProp < d.props.size()) ? &d.props[currentProp] : nullptr;
    bool controlled = p && p->motion == "controlled";
    propForm->setRowVisible(rows["prop.mesh"], p && p->shape == "mesh");
    propForm->setRowVisible(rows["prop.mass"], !p || p->motion != "static");
    for (const char* k : {"prop.maxSpeed", "prop.maxYawRate", "prop.response"}) propForm->setRowVisible(rows[k], controlled);
}


/*
 * Files outside the project are copied into <project>/stage/ so the project
 * stays self-contained; files already inside it are referenced in place.
 */
QString StagePanel::importIntoProject(const QString& path) {
    QDir projDir(Application::getInstance()->getProject()->getProjectDirectory());
    QString rel = projDir.relativeFilePath(path);
    if (!rel.startsWith("..") && !QDir::isAbsolutePath(rel)) return rel;

    projDir.mkpath("stage");
    QFileInfo info(path);
    QString target = "stage/" + info.fileName();
    for (int n = 1; QFile::exists(projDir.filePath(target)); ++n) {
        target = QString("stage/%1_%2.%3").arg(info.completeBaseName()).arg(n).arg(info.suffix());
    }
    if (!QFile::copy(path, projDir.filePath(target))) {
        Log::error("Stage: could not copy " + path + " into the project");
        return {};
    }
    Log::info("Stage: imported " + info.fileName() + " as " + target);
    return target;
}


void StagePanel::editStageScript() {
    Application* app = Application::getInstance();
    Project* project = app->getProject();
    QString rel = project->getStage()->getData().script;
    if (rel.isEmpty()) {
        rel = "stage.js";
        commit([rel](Stage::Data& d) { d.script = rel; });
    }

    QString path = project->getProjectDirectory() + "/" + rel;
    if (!QFile::exists(path)) {
        QFile file(path);
        if (!file.open(QFile::WriteOnly | QFile::Text)) {
            Log::error("Stage: could not create " + path);
            return;
        }
        QTextStream(&file) << STAGE_SCRIPT_TEMPLATE;
    }

    EditorWindow* editor = app->getEditor();
    editor->scriptPanel->loadScript(path);
    editor->bottomTabs->setCurrentWidget(editor->scriptPanel);
}
