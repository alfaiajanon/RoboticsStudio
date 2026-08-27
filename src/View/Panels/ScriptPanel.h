#pragma once

#include <QWidget>
#include <QString>

class CodeEditor; // <-- Updated forward declaration
class QLabel;
class QPushButton;
class QFileSystemWatcher;

/*
 * Provides an integrated JavaScript code editor with line numbers.
 * Includes a file system watcher to automatically reload the script 
 * if it is modified externally (e.g., via VS Code).
 */
class ScriptPanel : public QWidget {
    Q_OBJECT

public:
    explicit ScriptPanel(QWidget* parent = nullptr);
    ~ScriptPanel();

    void loadScript(const QString& path);
    void saveScript();

private slots:
    void onExternalFileChanged(const QString& path);
    void onEditorTextChanged();
    void onSaveClicked();

private:
    void setupUI();

    CodeEditor* textEditor; // <-- Changed type
    QLabel* statusLabel;
    QPushButton* saveButton;
    
    QFileSystemWatcher* fileWatcher;
    QString currentFilePath;
    
    bool isInternalSave; 
    bool hasUnsavedChanges;
};