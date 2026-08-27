#include "OutputPanel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QFrame>
#include <qlineedit.h>
#include <qpushbutton.h>

OutputPanel::OutputPanel(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // 1. Setup Text Area
    textOutput = new QPlainTextEdit(this);
    textOutput->setReadOnly(true);
    textOutput->setLineWrapMode(QPlainTextEdit::NoWrap);
    textOutput->setMaximumBlockCount(maxHistory);

    QFont font("Consolas");
    font.setStyleHint(QFont::Monospace);
    textOutput->setFont(font);

    mainLayout->addWidget(textOutput);

    // 2. Setup Text Search Box
    searchBox = new QLineEdit(this);
    searchBox->setPlaceholderText("Filter text...");
    searchBox->setMinimumWidth(150);
    connect(searchBox, &QLineEdit::textChanged, this, &OutputPanel::applyFilters);

    QPushButton* btnClear = new QPushButton("Clear", this);
    connect(btnClear, &QPushButton::clicked, this, &OutputPanel::clearConsole);

    // 3. Setup Filter Bar (Bottom) wrapped in a QFrame for styling
    QFrame* filterFrame = new QFrame(this);
    // Darken the background slightly and add a subtle top border
    filterFrame->setStyleSheet("QFrame { background-color: rgba(0, 0, 0, 0.15); border-top: 1px solid rgba(128, 128, 128, 0.2); } "
                               "QCheckBox { background-color: transparent; border: none; } ");

    QHBoxLayout* filterLayout = new QHBoxLayout(filterFrame);
    filterLayout->setContentsMargins(5, 5, 5, 5);

    chkDebug = new QCheckBox("Debug", this);
    chkInfo = new QCheckBox("Info", this);
    chkWarning = new QCheckBox("Warning", this);
    chkError = new QCheckBox("Error", this);
    chkCritical = new QCheckBox("Critical", this);

    chkDebug->setChecked(true);
    chkInfo->setChecked(true);
    chkWarning->setChecked(true);
    chkError->setChecked(true);
    chkCritical->setChecked(true);

    connect(chkDebug, &QCheckBox::toggled, this, &OutputPanel::applyFilters);
    connect(chkInfo, &QCheckBox::toggled, this, &OutputPanel::applyFilters);
    connect(chkWarning, &QCheckBox::toggled, this, &OutputPanel::applyFilters);
    connect(chkError, &QCheckBox::toggled, this, &OutputPanel::applyFilters);
    connect(chkCritical, &QCheckBox::toggled, this, &OutputPanel::applyFilters);


    filterLayout->addWidget(searchBox);
    filterLayout->addWidget(btnClear);
    filterLayout->addSpacing(10);
    filterLayout->addStretch();
    filterLayout->addWidget(chkDebug);
    filterLayout->addWidget(chkInfo);
    filterLayout->addWidget(chkWarning);
    filterLayout->addWidget(chkError);
    filterLayout->addWidget(chkCritical);

    mainLayout->addWidget(filterFrame);
}

void OutputPanel::appendMessage(LogLevel level, const QString& category,
                                 const QString& plainMessage, const QString& htmlMessage) {
    Q_UNUSED(category);

    logHistory.append({level, htmlMessage, plainMessage});
    if (logHistory.size() > maxHistory) {
        logHistory.removeFirst();
    }

    // Append to UI only if it passes both the level filter and the text search
    if (isLevelVisible(level)) {
        if (searchBox->text().isEmpty() || plainMessage.contains(searchBox->text(), Qt::CaseInsensitive)) {
            textOutput->appendHtml(htmlMessage);
            textOutput->ensureCursorVisible();
        }
    }
}

void OutputPanel::applyFilters() {
    textOutput->clear();
    QString searchText = searchBox->text();

    for (const LogEntry& entry : logHistory) {
        if (isLevelVisible(entry.level)) {
            if (searchText.isEmpty() || entry.plainMessage.contains(searchText, Qt::CaseInsensitive)) {
                textOutput->appendHtml(entry.htmlMessage);
            }
        }
    }
    textOutput->ensureCursorVisible();
}

bool OutputPanel::isLevelVisible(LogLevel level) const {
    switch (level) {
        case LogLevel::Debug:    return chkDebug->isChecked();
        case LogLevel::Info:     return chkInfo->isChecked();
        case LogLevel::Warning:  return chkWarning->isChecked();
        case LogLevel::Error:    return chkError->isChecked();
        case LogLevel::Critical: return chkCritical->isChecked();
    }
    return true;
}

void OutputPanel::clearConsole() {
    logHistory.clear();
    textOutput->clear();
}
