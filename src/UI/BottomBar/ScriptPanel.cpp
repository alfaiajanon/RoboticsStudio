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
#include "Utils/CodeEditor.h" 
#include "Core/Log.h"
#include "UI/Toast/Toast.h"

ScriptPanel::ScriptPanel(QWidget* parent) 
    : QWidget(parent), isInternalSave(false), hasUnsavedChanges(false) {
    
    fileWatcher = new QFileSystemWatcher(this);
    setupUI();

    connect(fileWatcher, &QFileSystemWatcher::fileChanged, this, &ScriptPanel::onExternalFileChanged);
}

ScriptPanel::~ScriptPanel() {}

void ScriptPanel::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
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

    textEditor->blockSignals(true);
    textEditor->setPlainText(content);
    textEditor->blockSignals(false);

    currentFilePath = fullpath;
    fileWatcher->addPath(currentFilePath);
    
    hasUnsavedChanges = false;
    saveButton->setEnabled(false);
    
    QFileInfo fileInfo(fullpath);
    statusLabel->setText(fileInfo.fileName() + " (External Editor Preferred)");
}

void ScriptPanel::saveScript() {
    if (currentFilePath.isEmpty()) return;

    isInternalSave = true;

    QFile file(currentFilePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out << textEditor->toPlainText();
        file.close();

        hasUnsavedChanges = false;
        saveButton->setEnabled(false);
        
        QFileInfo fileInfo(currentFilePath);
        statusLabel->setText(fileInfo.fileName() + " (External Editor Preferred)");
        
        Toast::showMessage(this->window(), "Script Saved");
    } else {
        Log::error("ScriptPanel failed to save file: " + currentFilePath);
    }

    QTimer::singleShot(100, this, [this]() {
        isInternalSave = false;
    });
}

void ScriptPanel::onSaveClicked() {
    saveScript();
}

void ScriptPanel::onEditorTextChanged() {
    if (!hasUnsavedChanges && !currentFilePath.isEmpty()) {
        hasUnsavedChanges = true;
        saveButton->setEnabled(true);
        
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

        textEditor->blockSignals(true);
        
        int scrollPos = textEditor->verticalScrollBar()->value();
        textEditor->setPlainText(content);
        textEditor->verticalScrollBar()->setValue(scrollPos);
        
        textEditor->blockSignals(false);

        hasUnsavedChanges = false;
        saveButton->setEnabled(false);
        
        QFileInfo fileInfo(fullpath);
        statusLabel->setText(fileInfo.fileName() + " (External Editor Preferred)");

        Toast::showMessage(this->window(), "Reloaded external changes");
        Log::info("Script externally modified and reloaded: " + fullpath);
    }
}