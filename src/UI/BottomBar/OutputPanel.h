#pragma once

#include <QWidget>
#include <QPlainTextEdit>

class OutputPanel : public QWidget {
    Q_OBJECT

    public:
        explicit OutputPanel(QWidget* parent = nullptr);

    public slots:
        void appendMessage(const QString& message);
        void clearConsole();

    private:
        QPlainTextEdit* textOutput;
};