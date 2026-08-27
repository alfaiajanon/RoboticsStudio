#pragma once

#include <QWidget>
#include <QPlainTextEdit>
#include <qcheckbox.h>
#include <qlineedit.h>
#include "Utils/Log.h"

struct LogEntry {
    LogLevel level;
    QString htmlMessage;
    QString plainMessage;
};

class OutputPanel : public QWidget {
    Q_OBJECT

    public:
        explicit OutputPanel(QWidget* parent = nullptr);

    public slots:
        void appendMessage(LogLevel level, const QString& category, const QString& plainMessage, const QString& htmlMessage);
        void clearConsole();

    private slots:
        void applyFilters();

    private:
        bool isLevelVisible(LogLevel level) const;

        QPlainTextEdit* textOutput;

        // Filter UI
        QCheckBox* chkDebug;
        QCheckBox* chkInfo;
        QCheckBox* chkWarning;
        QCheckBox* chkError;
        QCheckBox* chkCritical;
        QLineEdit* searchBox;

        // State
        QList<LogEntry> logHistory;
        const int maxHistory = 5000; // Prevent memory overflow on long sessions
    };
