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
#include <QListWidget>
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
constexpr int ROLE_PATH = Qt::UserRole + 1;   // path relative to the project dir

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
    splitter->setSizes({220, 9999});
}



QWidget* ScriptPanel::buildScriptList() {
    QWidget* side = new QWidget();
    side->setMinimumWidth(140);
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
    addButton->setToolTip("New script (MCU or stage)");
    addButton->setCursor(Qt::PointingHandCursor);
    addButton->setStyleSheet(R"(
        QToolButton { background: transparent; color: #a6a8ae; border: none; border-radius: 3px;
                      font-size: 16px; font-weight: bold; min-width: 24px; min-height: 24px; }
        QToolButton:hover { background-color: #353639; color: #e6e7e9; }
        QToolButton:disabled { color: #55575d; }
    )");
    connect(addButton, &QToolButton::clicked, this, &ScriptPanel::createScript);

    headerLayout->addWidget(title);
    headerLayout->addStretch();
    headerLayout->addWidget(addButton);

    scriptList = new QListWidget(side);
    scriptList->setFrameShape(QFrame::NoFrame);
    scriptList->setIconSize(QSize(18, 18));
    // Selected script: subtle row + accent bar on the left (not a full blue row)
    scriptList->setStyleSheet(
        "QListWidget::item { padding: 4px 2px; border-left: 3px solid transparent; }"
        "QListWidget::item:hover { background-color: #232427; }"
        "QListWidget::item:selected { background-color: #26272a; border-left: 3px solid #4a8fe7; color: #e6e7e9; }");
    connect(scriptList, &QListWidget::itemClicked, this, &ScriptPanel::onScriptItemClicked);

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

    // At the Real level a sketch runs on the boards whose slot holds it.
    const bool real = project->getLevel() == ProjectLevel::Real;
    QHash<QString, QStringList> boardsOf;
    if (real) for (ComponentInstance* b : project->boards()) if (!b->firmware.isEmpty()) boardsOf[b->firmware] << b->name;

    auto addItem = [this, real, &boardsOf](ScriptKind kind, const QString& rel, bool active) {
        const QStringList boards = kind == ScriptKind::Mcu ? boardsOf.value(rel) : QStringList();
        QListWidgetItem* item = new QListWidgetItem(kindBadge(kind), boards.isEmpty() ? rel : rel + "  → " + boards.join(", "), scriptList);
        item->setData(ROLE_KIND, static_cast<int>(kind));
        item->setData(ROLE_PATH, rel);
        QString kindName = kind == ScriptKind::Mcu ? "Firmware sketch" : "Stage script";
        if (kind == ScriptKind::Mcu && real)
            item->setToolTip(boards.isEmpty() ? kindName + " (no board runs it: pick it in a board's Inspector)"
                                              : kindName + " (runs on " + boards.join(", ") + ")");
        else
            item->setToolTip(active ? kindName + " (active, runs on Play)" : kindName);
        if (kind == ScriptKind::Mcu && rel.endsWith(".js", Qt::CaseInsensitive)) {
            // An old project's JavaScript robot script: no longer runs.
            item->setIcon(staleBadge());
            item->setToolTip("JavaScript robot scripts were removed: this script does not run.\n"
                             "Port it to a firmware sketch (.ino), see docs/user/firmware.md.");
        }
        if (active) {
            QFont f = item->font();
            f.setBold(true);
            item->setFont(f);
        }
    };

    QJsonArray mcuPaths = project->getScriptPaths();
    int mcuCurrent = project->getCurrentScriptIdx();
    for (int i = 0; i < mcuPaths.size(); ++i) {
        const QString rel = mcuPaths[i].toString();
        addItem(ScriptKind::Mcu, rel, real ? rel == project->activeSketchPath() : i == mcuCurrent);
    }

    const Stage::Data& stage = project->getStage()->getData();
    for (int i = 0; i < stage.scripts.size(); ++i)
        addItem(ScriptKind::Stage, stage.scripts[i], i == stage.currentScript);

    syncListSelection();
}



void ScriptPanel::setCreationEnabled(bool enabled) {
    addButton->setEnabled(enabled);
}



// Highlights the list row of the file open in the editor.
void ScriptPanel::syncListSelection() {
    Project* project = Application::getInstance() ? Application::getInstance()->getProject() : nullptr;
    QListWidgetItem* match = nullptr;
    if (project && !currentFilePath.isEmpty()) {
        QString open = QDir::cleanPath(currentFilePath);
        for (int i = 0; i < scriptList->count(); ++i) {
            QListWidgetItem* item = scriptList->item(i);
            if (QDir::cleanPath(project->getProjectDirectory() + "/" + item->data(ROLE_PATH).toString()) == open) {
                match = item;
                break;
            }
        }
    }
    QSignalBlocker block(scriptList);
    scriptList->setCurrentItem(match);
    if (!match) scriptList->clearSelection();
}



void ScriptPanel::onScriptItemClicked(QListWidgetItem* item) {
    Project* project = Application::getInstance()->getProject();
    QString path = project->getProjectDirectory() + "/" + item->data(ROLE_PATH).toString();
    if (QDir::cleanPath(path) == QDir::cleanPath(currentFilePath)) return;

    if (!confirmLeaveUnsaved()) {
        syncListSelection();
        return;
    }
    loadScript(path);
    syncListSelection();
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
 * "+" dialog: file name + kind. The file is created from the kind's template
 * (an existing file of that name is reused, not overwritten) and registered
 * in the project: firmware sketches in "script", stage scripts in "stage.script".
 * Either becomes its kind's active script when that kind has none.
 */
void ScriptPanel::createScript() {
    Application* app = Application::getInstance();
    Project* project = app ? app->getProject() : nullptr;
    if (!project || project->getProjectDirectory().isEmpty()) return;

    QDialog dialog(this);
    dialog.setWindowTitle("New Script");
    dialog.setMinimumWidth(340);
    QFormLayout* form = new QFormLayout(&dialog);

    QComboBox* kindCombo = new QComboBox(&dialog);
    kindCombo->addItem(kindBadge(ScriptKind::Mcu), "Firmware sketch (.ino, controls the robot)", static_cast<int>(ScriptKind::Mcu));
    kindCombo->addItem(kindBadge(ScriptKind::Stage), "Stage script (JavaScript, drives props)", static_cast<int>(ScriptKind::Stage));
    QLineEdit* nameEdit = new QLineEdit(&dialog);
    auto extensionOf = [kindCombo]() {
        return static_cast<ScriptKind>(kindCombo->currentData().toInt()) == ScriptKind::Mcu ? QString(".ino") : QString(".js");
    };
    auto updatePlaceholder = [nameEdit, extensionOf]() { nameEdit->setPlaceholderText("e.g. walker" + extensionOf()); };
    updatePlaceholder();
    connect(kindCombo, &QComboBox::currentIndexChanged, &dialog, updatePlaceholder);
    form->addRow("Type:", kindCombo);
    form->addRow("File name:", nameEdit);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    const QJsonArray mcuPaths = project->getScriptPaths();
    const QStringList stagePaths = project->getStage()->getData().scripts;
    QString name;
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        name = nameEdit->text().trimmed();
        if (name.isEmpty()) return;
        // A firmware .cpp stays .cpp; any other extension becomes the kind's own.
        const bool keepCpp = extensionOf() == ".ino" && name.endsWith(".cpp", Qt::CaseInsensitive);
        if (!keepCpp && !name.endsWith(extensionOf(), Qt::CaseInsensitive)) {
            name.remove(QRegularExpression("\\.(js|ino|cpp)$", QRegularExpression::CaseInsensitiveOption));
            name += extensionOf();
        }
        if (mcuPaths.contains(name) || stagePaths.contains(name)) {
            QMessageBox::warning(&dialog, "New Script", name + " is already a project script.");
            return;
        }
        dialog.accept();
    });

    if (dialog.exec() != QDialog::Accepted) return;
    const ScriptKind kind = static_cast<ScriptKind>(kindCombo->currentData().toInt());

    QString fullPath = project->getProjectDirectory() + "/" + name;
    if (!QFile::exists(fullPath)) {
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
        paths.append(name);
        Commands::setMcuScripts(paths, hadActive ? current : paths.size() - 1, "Add script");
    } else {
        // undoable stage edit; its reload refreshes the editor (and this list)
        app->getEditor()->stagePanel->addScript(name);
    }

    if (!confirmLeaveUnsaved()) return;
    loadScript(fullPath);
    refresh();
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
    textEditor->horizontalScrollBar()->setValue(0);
    textEditor->verticalScrollBar()->setValue(0);
    connect(textEditor, &QPlainTextEdit::textChanged, this, &ScriptPanel::onEditorTextChanged);

    currentFilePath = fullpath;
    fileWatcher->addPath(currentFilePath);

    setUnsaved(false);

    QFileInfo fileInfo(fullpath);
    statusLabel->setText(fileInfo.fileName() + " (External Editor Preferred)");
}

void ScriptPanel::showDiagnostics(const QList<LineMarker>& markers, bool goToFirstError) {
    textEditor->setLineMarkers(markers);
    if (!goToFirstError) return;
    for (const LineMarker& m : markers) {
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
