#include "ScriptPanel.h"

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

#include "Utils/Log.h"
#include "Application/Application.h"
#include "Commands/CommandUtils.h"
#include "View/Widgets/Toast.h"
#include "View/Widgets/CodeEditor.h"



namespace {

enum class ScriptKind { Mcu, Stage };
constexpr int ROLE_KIND = Qt::UserRole;       // ScriptKind as int
constexpr int ROLE_PATH = Qt::UserRole + 1;   // path relative to the project dir

const char* MCU_SCRIPT_TEMPLATE = R"(// MCU script: controls the robot through its components (comp_<uid>).

function setup() {
    // Called once when the simulation starts
}

function loop() {
    // Called repeatedly while the simulation runs
    delay(20);
}
)";

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
QIcon kindBadge(ScriptKind kind) {
    const bool mcu = kind == ScriptKind::Mcu;
    const qreal dpr = 2.0;
    QPixmap pm(QSize(18, 18) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(mcu ? QColor("#1E88E5") : QColor("#43A047"));
    p.drawRoundedRect(QRectF(1, 1, 16, 16), 4, 4);

    QFont f = p.font();
    f.setBold(true);
    f.setPixelSize(12);
    p.setFont(f);
    p.setPen(Qt::white);
    p.drawText(QRectF(1, 1, 16, 16), Qt::AlignCenter, mcu ? "M" : "S");
    return QIcon(pm);
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
    toolbar->setStyleSheet("background-color: #2d2d2d; border-bottom: 1px solid #1e1e1e;");
    QHBoxLayout* toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(8, 4, 8, 4);

    statusLabel = new QLabel("No script loaded", toolbar);
    statusLabel->setStyleSheet("color: #cccccc; font-style: italic;");

    saveButton = new QPushButton("Save", toolbar);
    saveButton->setEnabled(false);
    saveButton->setFixedWidth(60);
    saveButton->setStyleSheet(R"(
        QPushButton { background-color: #0e639c; color: white; border-radius: 2px; padding: 4px; }
        QPushButton:disabled { background-color: #4d4d4d; color: #888888; }
        QPushButton:hover { background-color: #1177bb; }
    )");

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
    header->setStyleSheet("background-color: #2d2d2d; border-bottom: 1px solid #1e1e1e;");
    QHBoxLayout* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(8, 4, 4, 4);

    QLabel* title = new QLabel("Scripts", header);
    title->setStyleSheet("color: #cccccc; font-weight: bold;");

    addButton = new QToolButton(header);
    addButton->setText("+");
    addButton->setToolTip("New script (MCU or stage)");
    addButton->setCursor(Qt::PointingHandCursor);
    addButton->setStyleSheet(R"(
        QToolButton { background: transparent; color: #cccccc; border: none; border-radius: 3px;
                      font-size: 16px; font-weight: bold; min-width: 24px; min-height: 24px; }
        QToolButton:hover { background-color: #3e3e3e; color: white; }
        QToolButton:disabled { color: #666666; }
    )");
    connect(addButton, &QToolButton::clicked, this, &ScriptPanel::createScript);

    headerLayout->addWidget(title);
    headerLayout->addStretch();
    headerLayout->addWidget(addButton);

    scriptList = new QListWidget(side);
    scriptList->setFrameShape(QFrame::NoFrame);
    scriptList->setIconSize(QSize(18, 18));
    scriptList->setStyleSheet("QListWidget::item { padding: 4px 2px; }");
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

    auto addItem = [this](ScriptKind kind, const QString& rel, bool active) {
        QListWidgetItem* item = new QListWidgetItem(kindBadge(kind), rel, scriptList);
        item->setData(ROLE_KIND, static_cast<int>(kind));
        item->setData(ROLE_PATH, rel);
        QString kindName = kind == ScriptKind::Mcu ? "MCU script" : "Stage script";
        item->setToolTip(active ? kindName + " (active, runs on Play)" : kindName);
        if (active) {
            QFont f = item->font();
            f.setBold(true);
            item->setFont(f);
        }
    };

    QJsonArray mcuPaths = project->getScriptPaths();
    int mcuCurrent = project->getCurrentScriptIdx();
    for (int i = 0; i < mcuPaths.size(); ++i)
        addItem(ScriptKind::Mcu, mcuPaths[i].toString(), i == mcuCurrent);

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
 * in the project: MCU scripts in "script", stage scripts in "stage.script".
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
    kindCombo->addItem(kindBadge(ScriptKind::Mcu), "MCU script (controls the robot)", static_cast<int>(ScriptKind::Mcu));
    kindCombo->addItem(kindBadge(ScriptKind::Stage), "Stage script (drives props)", static_cast<int>(ScriptKind::Stage));
    QLineEdit* nameEdit = new QLineEdit(&dialog);
    nameEdit->setPlaceholderText("e.g. walker.js");
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
        if (!name.endsWith(".js")) name += ".js";
        if (mcuPaths.contains(name) || stagePaths.contains(name)) {
            QMessageBox::warning(&dialog, "New Script", name + " is already a project script.");
            return;
        }
        dialog.accept();
    });

    if (dialog.exec() != QDialog::Accepted) return;
    auto kind = static_cast<ScriptKind>(kindCombo->currentData().toInt());

    QString fullPath = project->getProjectDirectory() + "/" + name;
    if (!QFile::exists(fullPath)) {
        QFile file(fullPath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            Log::error("ScriptPanel could not create " + fullPath);
            return;
        }
        QTextStream(&file) << (kind == ScriptKind::Mcu ? MCU_SCRIPT_TEMPLATE : STAGE_SCRIPT_TEMPLATE);
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
