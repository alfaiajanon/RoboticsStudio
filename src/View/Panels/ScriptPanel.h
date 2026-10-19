#pragma once

#include <QWidget>
#include <QString>

class CodeEditor;
struct LineMarker;
class QLabel;
class QPushButton;
class QFileSystemWatcher;
class QListWidget;
class QListWidgetItem;
class QToolButton;

/*
 * Provides an integrated code editor (C++ for firmware sketches, JavaScript
 * for stage scripts) with line numbers and compiler markers.
 * Includes a file system watcher to automatically reload the script 
 * if it is modified externally (e.g., via VS Code).
 * Left of the editor (resizable splitter) lists every project script:
 * firmware sketches with a blue "M" badge, stage scripts with a green "S"
 * (an old project's .js robot script, which no longer runs, with a red "!");
 * the active one of each kind is bold. "+" creates a new script.
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
    // Compiler messages for the open script, as gutter markers.
    void showDiagnostics(const QList<LineMarker>& markers, bool goToFirstError);

    // Rebuilds the script list from the project (MCU + stage scripts).
    void refresh();
    // Locks script creation while the simulation runs.
    void setCreationEnabled(bool enabled);
    // The "New Script" dialog (firmware sketch, JS script or stage script).
    void createScript();

signals:
    void modificationChanged(bool unsaved);

private slots:
    void onExternalFileChanged(const QString& path);
    void onEditorTextChanged();
    void onSaveClicked();

private:
    void setupUI();
    QWidget* buildScriptList();
    void onScriptItemClicked(QListWidgetItem* item);
    void syncListSelection();
    void setUnsaved(bool unsaved);   // flag + Save button + modificationChanged

    CodeEditor* textEditor; // <-- Changed type
    QLabel* statusLabel;
    QPushButton* saveButton;
    QListWidget* scriptList;
    QToolButton* addButton;
    
    QFileSystemWatcher* fileWatcher;
    QString currentFilePath;
    
    bool isInternalSave; 
    bool unsavedChanges;
};