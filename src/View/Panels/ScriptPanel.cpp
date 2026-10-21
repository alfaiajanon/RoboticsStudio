#include "ScriptPanel.h"
#include "Document/Components/ComponentInstance.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFileSystemWatcher>
#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QScrollBar>
#include <QTimer>
#include <QSplitter>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QHeaderView>
#include <QMenu>
#include <QToolButton>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QMessageBox>
#include <QPainter>
#include <QDir>
#include <QIcon>
#include <QPixmap>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>

#include "Utils/Log.h"
#include "Application/Application.h"
#include "Commands/CommandUtils.h"
#include "View/Widgets/Toast.h"
#include "View/Widgets/CodeEditor.h"
#include "View/Widgets/PanelCorners.h"
#include "FirmwareTemplate.h"



namespace {

enum class ScriptKind { Mcu, Stage };
constexpr int ROLE_KIND = Qt::UserRole;       // ScriptKind as int
constexpr int ROLE_PATH = Qt::UserRole + 1;   // path relative to the project dir (a unit: its .ino)
constexpr int ROLE_UNIT = Qt::UserRole + 2;   // true on a sketch unit row

const char* STAGE_SCRIPT_TEMPLATE = R"(// Stage script: drives the controlled props, next to (not inside) the robot.
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

// Logo-style badge: a colored rounded square with a white letter.
QIcon badge(const QColor& color, const QString& letter) {
    const qreal dpr = 2.0;
    QPixmap pm(QSize(18, 18) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawRoundedRect(QRectF(1, 1, 16, 16), 4, 4);

    QFont f = p.font();
    f.setBold(true);
    f.setPixelSize(12);
    p.setFont(f);
    p.setPen(Qt::white);
    p.drawText(QRectF(1, 1, 16, 16), Qt::AlignCenter, letter);
    return QIcon(pm);
}

QIcon kindBadge(ScriptKind kind) {
    return kind == ScriptKind::Mcu ? badge(QColor("#1E88E5"), "M") : badge(QColor("#43A047"), "S");
}

// A robot script that no longer runs (an old project's .js).
QIcon staleBadge() {
    return badge(QColor("#E53935"), "!");
}

// A sketch unit: a small chip (body + pins), painted so it needs no SVG support.
QIcon unitIcon() {
    const qreal dpr = 2.0;
    QPixmap pm(QSize(18, 18) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor blue("#6FA8F5");
    p.setPen(QPen(blue, 1.4, Qt::SolidLine, Qt::RoundCap));
    for (qreal t : {6.5, 9.0, 11.5}) {
        p.drawLine(QPointF(t, 1.5), QPointF(t, 4.0));
        p.drawLine(QPointF(t, 14.0), QPointF(t, 16.5));
        p.drawLine(QPointF(1.5, t), QPointF(4.0, t));
        p.drawLine(QPointF(14.0, t), QPointF(16.5, t));
    }
    p.setBrush(QColor("#1E88E5"));
    p.drawRoundedRect(QRectF(4.0, 4.0, 10.0, 10.0), 2, 2);
    return QIcon(pm);
}

// A file inside a sketch unit, by its extension.
QIcon fileBadge(const QString& path) {
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext == "ino") return badge(QColor("#1E88E5"), "M");
    if (ext == "cpp") return badge(QColor("#00897B"), "C");
    return badge(QColor("#7E8792"), "H");
}

const char* kToolButtonStyle = R"(
    QToolButton { background: transparent; color: #a6a8ae; border: none; border-radius: 3px;
                  font-size: 16px; font-weight: bold; min-width: 24px; min-height: 24px; }
    QToolButton:hover { background-color: #353639; color: #e6e7e9; }
    QToolButton:disabled { color: #55575d; }
)";

// A file name the user typed for a sketch's .h or .cpp: letters, digits, '_', '-'.
QString fileStem(const QString& typed) {
    QString n = typed.trimmed();
    n.remove(QRegularExpression("\\.(h|hpp|cpp|ino)$", QRegularExpression::CaseInsensitiveOption));
    n.replace(QRegularExpression("[^A-Za-z0-9_-]+"), "_");
    n.remove(QRegularExpression("^[^A-Za-z0-9_]+"));
    return n;
}

}

ScriptPanel::ScriptPanel(QWidget* parent)
    : QWidget(parent), isInternalSave(false), unsavedChanges(false) {

    fileWatcher = new QFileSystemWatcher(this);
    setupUI();

    connect(fileWatcher, &QFileSystemWatcher::fileChanged, this, &ScriptPanel::onExternalFileChanged);
}

ScriptPanel::~ScriptPanel() {}

void ScriptPanel::setupUI() {
    QHBoxLayout* rootLayout = new QHBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);

    QSplitter* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);
    rootLayout->addWidget(splitter);

    splitter->addWidget(buildScriptList());

    QWidget* editorSide = new QWidget(splitter);
    QVBoxLayout* mainLayout = new QVBoxLayout(editorSide);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Top Toolbar
    QWidget* toolbar = new QWidget(this);
    toolbar->setObjectName("PanelToolbar");
    QHBoxLayout* toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(8, 4, 8, 4);

    statusLabel = new QLabel("No script loaded", toolbar);
    statusLabel->setStyleSheet("font-style: italic;");

    saveButton = new QPushButton("Save", toolbar);
    saveButton->setEnabled(false);
    saveButton->setFixedWidth(60);

    connect(saveButton, &QPushButton::clicked, this, &ScriptPanel::onSaveClicked);

    toolbarLayout->addWidget(statusLabel);
    toolbarLayout->addStretch();
    toolbarLayout->addWidget(saveButton);

    // Instantiating the custom CodeEditor
    textEditor = new CodeEditor(this);
    connect(textEditor, &QPlainTextEdit::textChanged, this, &ScriptPanel::onEditorTextChanged);

    mainLayout->addWidget(toolbar);
    mainLayout->addWidget(textEditor);

    splitter->addWidget(editorSide);
    // script list and editor read as two panels on either side of the splitter gap
    PanelCorners::install(splitter->widget(0));
    PanelCorners::install(editorSide);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({250, 9999});
}



QWidget* ScriptPanel::buildScriptList() {
    QWidget* side = new QWidget();
    side->setMinimumWidth(160);
    QVBoxLayout* layout = new QVBoxLayout(side);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    QWidget* header = new QWidget(side);
    header->setObjectName("PanelToolbar");
    QHBoxLayout* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(8, 4, 4, 4);

    QLabel* title = new QLabel("Scripts", header);
    title->setStyleSheet("color: #e6e7e9; font-weight: bold;");

    addButton = new QToolButton(header);
    addButton->setText("+");
    addButton->setToolTip("New firmware sketch (its own unit) or stage script");
    addButton->setCursor(Qt::PointingHandCursor);
    addButton->setStyleSheet(kToolButtonStyle);
    connect(addButton, &QToolButton::clicked, this, &ScriptPanel::createScript);

    headerLayout->addWidget(title);
    headerLayout->addStretch();
    headerLayout->addWidget(addButton);

    scriptList = new QTreeWidget(side);
    scriptList->setFrameShape(QFrame::NoFrame);
    scriptList->setIconSize(QSize(18, 18));
    scriptList->setColumnCount(2);
    scriptList->setHeaderHidden(true);
    scriptList->setRootIsDecorated(true);
    scriptList->setIndentation(14);
    scriptList->header()->setStretchLastSection(false);
    scriptList->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    scriptList->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    scriptList->header()->resizeSection(1, 28);
    scriptList->setContextMenuPolicy(Qt::CustomContextMenu);
    // Selected file: subtle row + accent bar on the left (not a full blue row)
    scriptList->setStyleSheet(
        "QTreeWidget::item { padding: 3px 2px; border-left: 3px solid transparent; }"
        "QTreeWidget::item:hover { background-color: #232427; }"
        "QTreeWidget::item:selected { background-color: #26272a; border-left: 3px solid #4a8fe7; color: #e6e7e9; }");
    connect(scriptList, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item, int) { onScriptItemClicked(item); });
    connect(scriptList, &QTreeWidget::customContextMenuRequested, this, &ScriptPanel::showFileMenu);

    layout->addWidget(header);
    layout->addWidget(scriptList);
    return side;
}



void ScriptPanel::refresh() {
    scriptList->clear();
    Application* app = Application::getInstance();
    if (!app) return;
    Project* project = app->getProject();
    if (!project) return;
    const QString root = project->getProjectDirectory();

    // At the Real level a sketch runs on the boards whose slot holds it.
    const bool real = project->getLevel() == ProjectLevel::Real;
    QHash<QString, QStringList> boardsOf;
    if (real) for (ComponentInstance* b : project->boards()) if (!b->firmware.isEmpty()) boardsOf[b->firmware] << b->name;

    auto bold = [](QTreeWidgetItem* item) {
        QFont f = item->font(0);
        f.setBold(true);
        item->setFont(0, f);
    };
    auto fileItem = [](QTreeWidgetItem* parent, QTreeWidget* tree, const QIcon& icon, const QString& text, ScriptKind kind,
                       const QString& rel) {
        QTreeWidgetItem* item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree);
        item->setIcon(0, icon);
        item->setText(0, text);
        item->setData(0, ROLE_KIND, static_cast<int>(kind));
        item->setData(0, ROLE_PATH, rel);
        return item;
    };

    const QJsonArray mcuPaths = project->getScriptPaths();
    const int mcuCurrent = project->getCurrentScriptIdx();
    for (int i = 0; i < mcuPaths.size(); ++i) {
        const QString rel = mcuPaths[i].toString();
        const bool active = real ? rel == project->activeSketchPath() : i == mcuCurrent;
        if (rel.endsWith(".js", Qt::CaseInsensitive)) {
            // An old project's JavaScript robot script: no longer runs.
            QTreeWidgetItem* item = fileItem(nullptr, scriptList, staleBadge(), rel, ScriptKind::Mcu, rel);
            item->setToolTip(0, "JavaScript robot scripts were removed: this script does not run.\n"
                                "Port it to a firmware sketch (.ino), see docs/user/firmware.md.");
            continue;
        }
        // The sketch unit: its .ino first, then the folder's headers and sources.
        const bool folder = Project::inSketchFolder(rel);
        const QFileInfo info(root + "/" + rel);
        const QStringList boards = boardsOf.value(rel);
        const QString unitName = folder ? info.dir().dirName() : info.completeBaseName();
        QTreeWidgetItem* unit = fileItem(nullptr, scriptList, unitIcon(),
                                         boards.isEmpty() ? unitName : unitName + "  → " + boards.join(", "), ScriptKind::Mcu, rel);
        unit->setData(0, ROLE_UNIT, true);
        QString tip = folder ? QString("Firmware sketch %1/ (an Arduino sketch folder): the .ino, plus its .h and .cpp files").arg(QFileInfo(rel).path())
                             : QString("Firmware sketch %1 (a single file at the project root; new sketches get a folder of their own)").arg(rel);
        if (real) tip += boards.isEmpty() ? "\nNo board runs it: pick it in a board's Inspector." : "\nRuns on " + boards.join(", ");
        else if (active) tip += "\nActive: runs on Play";
        unit->setToolTip(0, tip);
        if (active) bold(unit);
        fileItem(unit, nullptr, fileBadge(rel), info.fileName(), ScriptKind::Mcu, rel)->setToolTip(0, "The sketch: setup() and loop()");
        if (folder) {
            const QString dirRel = QFileInfo(rel).path();
            for (const QString& f : info.dir().entryList({"*.h", "*.hpp", "*.cpp"}, QDir::Files, QDir::Name))
                fileItem(unit, nullptr, fileBadge(f), f, ScriptKind::Mcu, dirRel + "/" + f)
                    ->setToolTip(0, f.endsWith(".cpp") ? "Compiled on its own and linked with the sketch (include <Arduino.h> here yourself)"
                                                       : "A header of the sketch: #include \"" + f + "\"");
            QToolButton* plus = new QToolButton(scriptList);
            plus->setText("+");
            plus->setToolTip("Add a .h or .cpp file to this sketch");
            plus->setCursor(Qt::PointingHandCursor);
            plus->setStyleSheet(kToolButtonStyle);
            plus->setEnabled(addButton->isEnabled());
            connect(plus, &QToolButton::clicked, this, [this, rel]() { addFileToSketch(rel); });
            scriptList->setItemWidget(unit, 1, plus);
        }
        unit->setExpanded(true);
    }

    const Stage::Data& stage = project->getStage()->getData();
    for (int i = 0; i < stage.scripts.size(); ++i) {
        QTreeWidgetItem* item = fileItem(nullptr, scriptList, kindBadge(ScriptKind::Stage), QFileInfo(stage.scripts[i]).fileName(),
                                         ScriptKind::Stage, stage.scripts[i]);
        item->setToolTip(0, i == stage.currentScript ? "Stage script (active, runs on Play)" : "Stage script");
        if (i == stage.currentScript) bold(item);
    }

    syncListSelection();
}



void ScriptPanel::setCreationEnabled(bool enabled) {
    addButton->setEnabled(enabled);
    for (QTreeWidgetItemIterator it(scriptList); *it; ++it)
        if (QWidget* w = scriptList->itemWidget(*it, 1)) w->setEnabled(enabled);
}



// Highlights the row of the file open in the editor.
void ScriptPanel::syncListSelection() {
    Project* project = Application::getInstance() ? Application::getInstance()->getProject() : nullptr;
    QTreeWidgetItem* match = nullptr;
    if (project && !currentFilePath.isEmpty()) {
        const QString open = QDir::cleanPath(currentFilePath);
        for (QTreeWidgetItemIterator it(scriptList); *it && !match; ++it) {
            if ((*it)->data(0, ROLE_UNIT).toBool()) continue;   // the unit's .ino row is the match
            if (QDir::cleanPath(project->getProjectDirectory() + "/" + (*it)->data(0, ROLE_PATH).toString()) == open) match = *it;
        }
    }
    QSignalBlocker block(scriptList);
    scriptList->setCurrentItem(match);
    if (!match) scriptList->clearSelection();
}



void ScriptPanel::onScriptItemClicked(QTreeWidgetItem* item) {
    Project* project = Application::getInstance()->getProject();
    QString path = project->getProjectDirectory() + "/" + item->data(0, ROLE_PATH).toString();
    if (QDir::cleanPath(path) == QDir::cleanPath(currentFilePath)) return;

    if (!confirmLeaveUnsaved()) {
        syncListSelection();
        return;
    }
    loadScript(path);
    syncListSelection();
}



// Right click on a sketch's .h / .cpp: delete it (the .ino goes with its sketch,
// from the Inspector's script list).
void ScriptPanel::showFileMenu(const QPoint& pos) {
    QTreeWidgetItem* item = scriptList->itemAt(pos);
    Project* project = Application::getInstance()->getProject();
    if (!item || !project || item->data(0, ROLE_UNIT).toBool() || !addButton->isEnabled()) return;
    const QString rel = item->data(0, ROLE_PATH).toString();
    const QString ext = QFileInfo(rel).suffix().toLower();
    if (ext != "h" && ext != "hpp" && ext != "cpp") return;
    QMenu menu(this);
    QAction* del = menu.addAction("Delete " + QFileInfo(rel).fileName() + "...");
    if (menu.exec(scriptList->viewport()->mapToGlobal(pos)) != del) return;
    const QString full = project->getProjectDirectory() + "/" + rel;
    if (QMessageBox::question(this, "Delete File", "Delete " + rel + " from disk? This can't be undone.",
                              QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) return;
    if (QDir::cleanPath(full) == QDir::cleanPath(currentFilePath)) unloadScript();
    if (!QFile::remove(full)) Log::error("Could not delete " + full);
    refresh();
}



bool ScriptPanel::confirmLeaveUnsaved() {
    if (!unsavedChanges) return true;
    auto choice = QMessageBox::question(this, "Unsaved Changes",
        QFileInfo(currentFilePath).fileName() + " has unsaved changes. Save them first?",
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (choice == QMessageBox::Cancel) return false;
    if (choice == QMessageBox::Save) return saveScript();
    return true;
}



/*
 * "+" dialog: name + kind. A firmware sketch gets a folder of its own,
 * script/<name>/<name>.ino (an Arduino sketch folder), from the starter
 * template; a stage script is script/<name>.js. Registered in the project:
 * sketches in "script", stage scripts in "stage.script". Either becomes its
 * kind's active script when that kind has none.
 */
void ScriptPanel::createScript() {
    Application* app = Application::getInstance();
    Project* project = app ? app->getProject() : nullptr;
    if (!project || project->getProjectDirectory().isEmpty()) return;

    QDialog dialog(this);
    dialog.setWindowTitle("New Script");
    dialog.setMinimumWidth(360);
    QFormLayout* form = new QFormLayout(&dialog);

    QComboBox* kindCombo = new QComboBox(&dialog);
    kindCombo->addItem(kindBadge(ScriptKind::Mcu), "Firmware sketch (.ino, controls the robot)", static_cast<int>(ScriptKind::Mcu));
    kindCombo->addItem(kindBadge(ScriptKind::Stage), "Stage script (JavaScript, drives props)", static_cast<int>(ScriptKind::Stage));
    QLineEdit* nameEdit = new QLineEdit(&dialog);
    QLabel* hint = new QLabel(&dialog);
    hint->setWordWrap(true);
    hint->setStyleSheet("color: #8a8d93;");
    auto isMcu = [kindCombo]() { return static_cast<ScriptKind>(kindCombo->currentData().toInt()) == ScriptKind::Mcu; };
    auto relOf = [isMcu](const QString& n) {
        return isMcu() ? QString("%1/%2/%2.ino").arg(Project::kScriptDir, n) : QString("%1/%2.js").arg(Project::kScriptDir, n);
    };
    auto update = [nameEdit, hint, isMcu, relOf]() {
        nameEdit->setPlaceholderText(isMcu() ? "e.g. walker" : "e.g. traffic");
        const QString n = isMcu() ? Project::sketchName(nameEdit->text()) : fileStem(nameEdit->text());
        hint->setText(n.isEmpty() ? QString() : "Creates " + relOf(n) + (isMcu() ? " (a sketch folder: add .h / .cpp files with its \"+\")" : ""));
    };
    update();
    connect(kindCombo, &QComboBox::currentIndexChanged, &dialog, update);
    connect(nameEdit, &QLineEdit::textChanged, &dialog, update);
    form->addRow("Type:", kindCombo);
    form->addRow("Name:", nameEdit);
    form->addRow(hint);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    QString rel;
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        const QString n = isMcu() ? Project::sketchName(nameEdit->text()) : fileStem(nameEdit->text());
        if (n.isEmpty()) return;
        rel = relOf(n);
        const QString full = project->getProjectDirectory() + "/" + rel;
        if (QFileInfo::exists(isMcu() ? QFileInfo(full).path() : full)) {
            QMessageBox::warning(&dialog, "New Script", rel + (isMcu() ? ": that sketch folder already exists." : " already exists."));
            return;
        }
        dialog.accept();
    });

    if (dialog.exec() != QDialog::Accepted) return;
    const ScriptKind kind = static_cast<ScriptKind>(kindCombo->currentData().toInt());

    const QString fullPath = project->getProjectDirectory() + "/" + rel;
    QDir().mkpath(QFileInfo(fullPath).path());
    {
        QFile file(fullPath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            Log::error("ScriptPanel could not create " + fullPath);
            return;
        }
        QTextStream(&file) << (kind == ScriptKind::Mcu ? firmwareTemplate(project) : QString(STAGE_SCRIPT_TEMPLATE));
    }

    if (kind == ScriptKind::Mcu) {
        // undoable; its editor refresh updates the inspector's script combo + this list
        QJsonArray paths = project->getScriptPaths();
        int current = project->getCurrentScriptIdx();
        bool hadActive = current >= 0 && current < paths.size();
        paths.append(rel);
        Commands::setMcuScripts(paths, hadActive ? current : paths.size() - 1, "Add script");
    } else {
        // undoable stage edit; its reload refreshes the editor (and this list)
        app->getEditor()->stagePanel->addScript(rel);
    }

    if (!confirmLeaveUnsaved()) return;
    loadScript(fullPath);
    refresh();
}



// A sketch unit's "+": a header or a source file in the sketch's folder. A
// .cpp is compiled on its own and linked with the sketch, so it includes
// <Arduino.h> (and its own header, if there is one) itself.
void ScriptPanel::addFileToSketch(const QString& sketchRel) {
    Project* project = Application::getInstance()->getProject();
    if (!project || !Project::inSketchFolder(sketchRel)) return;
    const QDir folder = QFileInfo(project->getProjectDirectory() + "/" + sketchRel).dir();

    QDialog dialog(this);
    dialog.setWindowTitle("Add File to " + folder.dirName());
    dialog.setMinimumWidth(340);
    QFormLayout* form = new QFormLayout(&dialog);
    QComboBox* typeCombo = new QComboBox(&dialog);
    typeCombo->addItem(fileBadge("x.h"), "Header (.h)", ".h");
    typeCombo->addItem(fileBadge("x.cpp"), "Source (.cpp, compiled and linked with the sketch)", ".cpp");
    QLineEdit* nameEdit = new QLineEdit(&dialog);
    nameEdit->setPlaceholderText("e.g. kinematics");
    form->addRow("Type:", typeCombo);
    form->addRow("Name:", nameEdit);
    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QString fileName;
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        const QString stem = fileStem(nameEdit->text());
        if (stem.isEmpty()) return;
        fileName = stem + typeCombo->currentData().toString();
        if (stem == folder.dirName() && fileName.endsWith(".cpp")) {
            QMessageBox::warning(&dialog, "Add File", "That name is the sketch's own: pick another.");
            return;
        }
        if (folder.exists(fileName)) {
            QMessageBox::warning(&dialog, "Add File", fileName + " already exists in this sketch.");
            return;
        }
        dialog.accept();
    });
    if (dialog.exec() != QDialog::Accepted) return;

    const QString stem = QFileInfo(fileName).completeBaseName();
    QString text;
    if (fileName.endsWith(".h")) {
        text = QString("// %1: part of the %2 sketch. Include it with #include \"%1\".\n#pragma once\n\n#include <Arduino.h>\n\n")
                   .arg(fileName, folder.dirName());
    } else {
        text = QString("// %1: part of the %2 sketch, compiled on its own and linked with it.\n#include <Arduino.h>\n")
                   .arg(fileName, folder.dirName());
        if (folder.exists(stem + ".h")) text += QString("#include \"%1.h\"\n").arg(stem);
        text += "\n";
    }
    const QString full = folder.filePath(fileName);
    QFile file(full);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        Log::error("ScriptPanel could not create " + full);
        return;
    }
    file.write(text.toUtf8());
    file.close();
    refresh();
    if (!confirmLeaveUnsaved()) return;
    loadScript(full);
    syncListSelection();
}

void ScriptPanel::loadScript(const QString& fullpath) {
    if (fullpath.isEmpty()) return;

    if (!currentFilePath.isEmpty()) {
        fileWatcher->removePath(currentFilePath);
    }

    QFile file(fullpath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        Log::error("ScriptPanel could not open file: " + fullpath);
        return;
    }

    QTextStream in(&file);
    QString content = in.readAll();
    file.close();

    // Disconnect our specific slot to prevent state changes, allowing CodeEditor to process internal sizing signals
    disconnect(textEditor, &QPlainTextEdit::textChanged, this, &ScriptPanel::onEditorTextChanged);
    const QString suffix = QFileInfo(fullpath).suffix().toLower();
    textEditor->setLanguage(suffix == "ino" || suffix == "cpp" || suffix == "h" ? CodeLanguage::Cpp : CodeLanguage::JavaScript);
    textEditor->clearLineMarkers();
    textEditor->setPlainText(content);
    if (compileMarkers.contains(QDir::cleanPath(fullpath))) textEditor->setLineMarkers(compileMarkers.value(QDir::cleanPath(fullpath)));
    textEditor->horizontalScrollBar()->setValue(0);
    textEditor->verticalScrollBar()->setValue(0);
    connect(textEditor, &QPlainTextEdit::textChanged, this, &ScriptPanel::onEditorTextChanged);

    currentFilePath = fullpath;
    fileWatcher->addPath(currentFilePath);

    setUnsaved(false);

    QFileInfo fileInfo(fullpath);
    statusLabel->setText(fileInfo.fileName() + " (External Editor Preferred)");
}

void ScriptPanel::setCompileMarkers(const QHash<QString, QList<LineMarker>>& byPath) {
    compileMarkers.clear();
    for (auto it = byPath.begin(); it != byPath.end(); ++it) compileMarkers.insert(QDir::cleanPath(it.key()), it.value());
    textEditor->setLineMarkers(compileMarkers.value(QDir::cleanPath(currentFilePath)));
}

void ScriptPanel::goToFirstError() {
    for (const LineMarker& m : compileMarkers.value(QDir::cleanPath(currentFilePath))) {
        if (m.error) { textEditor->goToLine(m.line); return; }
    }
}

void ScriptPanel::unloadScript() {
    if (!currentFilePath.isEmpty()) fileWatcher->removePath(currentFilePath);
    currentFilePath.clear();

    disconnect(textEditor, &QPlainTextEdit::textChanged, this, &ScriptPanel::onEditorTextChanged);
    textEditor->clear();
    connect(textEditor, &QPlainTextEdit::textChanged, this, &ScriptPanel::onEditorTextChanged);

    setUnsaved(false);
    statusLabel->setText("No script loaded");
    syncListSelection();
}

void ScriptPanel::setUnsaved(bool unsaved) {
    saveButton->setEnabled(unsaved);
    if (unsaved == unsavedChanges) return;
    unsavedChanges = unsaved;
    emit modificationChanged(unsaved);
}

bool ScriptPanel::saveScript() {
    if (currentFilePath.isEmpty()) return false;

    isInternalSave = true;
    bool saved = false;

    QFile file(currentFilePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out << textEditor->toPlainText();
        file.close();
        saved = true;

        setUnsaved(false);

        QFileInfo fileInfo(currentFilePath);
        statusLabel->setText(fileInfo.fileName() + " (External Editor Preferred)");

        Toast::showMessage(this->window(), "Script Saved");
    } else {
        Log::error("ScriptPanel failed to save file: " + currentFilePath);
    }

    QTimer::singleShot(100, this, [this]() {
        isInternalSave = false;
    });
    return saved;
}

void ScriptPanel::onSaveClicked() {
    saveScript();
}

void ScriptPanel::onEditorTextChanged() {
    if (!unsavedChanges && !currentFilePath.isEmpty()) {
        setUnsaved(true);

        QFileInfo fileInfo(currentFilePath);
        statusLabel->setText(fileInfo.fileName() + " *");
    }
}

void ScriptPanel::onExternalFileChanged(const QString& fullpath) {
    if (isInternalSave) return;

    if (!fileWatcher->files().contains(fullpath)) {
        fileWatcher->addPath(fullpath);
    }

    QFile file(fullpath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        QString content = in.readAll();
        file.close();

        // Disconnect our specific slot to prevent state changes, allowing CodeEditor to process internal sizing signals
        disconnect(textEditor, &QPlainTextEdit::textChanged, this, &ScriptPanel::onEditorTextChanged);

        int scrollPos = textEditor->verticalScrollBar()->value();
        int hScrollPos = textEditor->horizontalScrollBar()->value();

        textEditor->setPlainText(content);

        textEditor->verticalScrollBar()->setValue(scrollPos);
        textEditor->horizontalScrollBar()->setValue(hScrollPos);

        connect(textEditor, &QPlainTextEdit::textChanged, this, &ScriptPanel::onEditorTextChanged);

        setUnsaved(false);

        QFileInfo fileInfo(fullpath);
        statusLabel->setText(fileInfo.fileName() + " (External Editor Preferred)");

        Toast::showMessage(this->window(), "Reloaded external changes");
        Log::info("Script externally modified and reloaded: " + fullpath);
    }
}
