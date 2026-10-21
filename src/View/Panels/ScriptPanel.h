#pragma once

#include <QWidget>
#include <QString>
#include <QHash>
#include <QList>
#include "View/Widgets/CodeEditor.h"

class CodeEditor;
struct LineMarker;
class QLabel;
class QPushButton;
class QFileSystemWatcher;
class QTreeWidget;
class QTreeWidgetItem;
class QToolButton;

/*
 * Provides an integrated code editor (C++ for firmware sketches, JavaScript
 * for stage scripts) with line numbers and compiler markers.
 * Includes a file system watcher to automatically reload the script 
 * if it is modified externally (e.g., via VS Code).
 * Left of the editor (resizable splitter) is the project's script tree:
 *   - a firmware sketch is a unit (chip icon) holding its files: the one
 *     .ino (the root, what a board runs) and any .h / .cpp next to it
 *     (script/<name>/, an Arduino sketch folder); its own "+" adds a .h or
 *     .cpp. An older project's sketch at the project root is a unit with
 *     just its .ino (no "+").
 *   - a stage script is one file (green "S"); an old project's .js robot
 *     script, which no longer runs, has a red "!".
 * The active sketch / stage script is bold. The header's "+" creates a new
 * sketch (its folder) or stage script.
 */
class ScriptPanel : public QWidget {
    Q_OBJECT

public:
    explicit ScriptPanel(QWidget* parent = nullptr);
    ~ScriptPanel();

    void loadScript(const QString& path);
    void unloadScript();   // empty editor, no file (e.g. its script was deleted)
    bool saveScript();     // false = no file open or the write failed
    bool hasUnsavedChanges() const { return unsavedChanges; }
    QString currentPath() const { return currentFilePath; }
    // Save/Discard/Cancel prompt when the open script has unsaved edits; false = cancelled.
    bool confirmLeaveUnsaved();
    // Compiler messages of the last build, per file (absolute path), shown as
    // gutter markers on whichever of those files is open.
    void setCompileMarkers(const QHash<QString, QList<LineMarker>>& byPath);
    void goToFirstError();

    // Rebuilds the script list from the project (MCU + stage scripts).
    void refresh();
    // Locks script creation while the simulation runs.
    void setCreationEnabled(bool enabled);
    // The "New Script" dialog: a firmware sketch (its own folder) or a stage script.
    void createScript();
    // A sketch's "+": a new .h or .cpp in its folder.
    void addFileToSketch(const QString& sketchRel);

signals:
    void modificationChanged(bool unsaved);

private slots:
    void onExternalFileChanged(const QString& path);
    void onEditorTextChanged();
    void onSaveClicked();

private:
    void setupUI();
    QWidget* buildScriptList();
    void onScriptItemClicked(QTreeWidgetItem* item);
    void showFileMenu(const QPoint& pos);
    void syncListSelection();
    void setUnsaved(bool unsaved);   // flag + Save button + modificationChanged

    CodeEditor* textEditor; // <-- Changed type
    QLabel* statusLabel;
    QPushButton* saveButton;
    QTreeWidget* scriptList;
    QHash<QString, QList<LineMarker>> compileMarkers;   // cleaned absolute path -> markers
    QToolButton* addButton;
    
    QFileSystemWatcher* fileWatcher;
    QString currentFilePath;
    
    bool isInternalSave; 
    bool unsavedChanges;
};