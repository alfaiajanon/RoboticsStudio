#include "OutputPanel.h"
#include <QVBoxLayout>

OutputPanel::OutputPanel(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    textOutput = new QPlainTextEdit(this);
    textOutput->setReadOnly(true);
    textOutput->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    
    QFont font("Consolas");
    font.setStyleHint(QFont::Monospace);
    textOutput->setFont(font);

    layout->addWidget(textOutput);
}




void OutputPanel::appendMessage(const QString& message) {
    textOutput->appendHtml(message);
    textOutput->ensureCursorVisible(); 
}




void OutputPanel::clearConsole() {
    textOutput->clear();
}