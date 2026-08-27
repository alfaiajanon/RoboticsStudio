#include "Log.h"
#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QTextStream>
#include <QThread>
#include <QMutexLocker>
#include <iostream>

LogDispatcher* LogDispatcher::instance() {
    static LogDispatcher instance;
    return &instance;
}


bool Log::fileLoggingEnabled = false;
bool Log::inAppLoggingEnabled = true;
bool Log::consoleLoggingEnabled = true;
LogLevel Log::minLevel = LogLevel::Debug;
QString Log::logFilePath;
QFile Log::logFile;
qint64 Log::maxFileSizeBytes = 10 * 1024 * 1024; // 10 MB
QMutex Log::mutex;

QString Log::levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::Debug:    return "DEBUG";
        case LogLevel::Info:     return "INFO";
        case LogLevel::Warning:  return "WARNING";
        case LogLevel::Error:    return "ERROR";
        case LogLevel::Critical: return "CRITICAL";
    }
    return "UNKNOWN";
}

QString Log::colorForLevel(LogLevel level) {
    switch (level) {
        case LogLevel::Debug:    return "#7f8c8d";
        case LogLevel::Info:     return "#a9b7c6";
        case LogLevel::Warning:  return "#ffcc00";
        case LogLevel::Error:    return "#ff5555";
        case LogLevel::Critical: return "#ff0033";
    }
    return "#ffffff";
}

void Log::rotateIfNeeded() {
    if (logFilePath.isEmpty()) return;
    QFileInfo info(logFilePath);
    if (info.exists() && info.size() >= maxFileSizeBytes) {
        logFile.close();
        const QString backup = logFilePath + "." +
            QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
        QFile::rename(logFilePath, backup);
        logFile.setFileName(logFilePath);
        logFile.open(QIODevice::Append | QIODevice::Text);
    }
}






void Log::write(LogLevel level, const QString& message, const QString& category) {
    if (level < minLevel) return;

    QMutexLocker locker(&mutex);

    const QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
    const QString levelStr = levelToString(level);
    const QString catTag = category.isEmpty() ? QString() : QString(" [%1]").arg(category);
    const QString threadTag = QString(" [T%1]")
        .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()));

    const QString plainLine = QString("%1%2 [%3]%4 %5")
        .arg(timestamp, threadTag, levelStr, catTag, message);

    if (consoleLoggingEnabled) {
        if (level >= LogLevel::Error)
            std::cerr << plainLine.toStdString() << "\n";
        else
            std::cout << plainLine.toStdString() << "\n";
    }

    if (fileLoggingEnabled && !logFilePath.isEmpty()) {
        rotateIfNeeded();
        if (!logFile.isOpen()) {
            logFile.setFileName(logFilePath);
            logFile.open(QIODevice::Append | QIODevice::Text);
        }
        if (logFile.isOpen()) {
            QTextStream stream(&logFile);
            stream << plainLine << "\n";
            stream.flush();
        }
    }

    if (inAppLoggingEnabled) {
        const QString htmlMsg = QString("<pre style='color:%1;'>%2 [%3]%4 %5</pre>")
            .arg(colorForLevel(level), timestamp, levelStr, catTag, message.toHtmlEscaped());
        emit LogDispatcher::instance()->messageLogged(level, category, message, htmlMsg);
    }
}

// debug/info/warning/error/critical are now templates defined in Log.h —
// they resolve message formatting via LogFormatter<T> and call write() below.

void Log::setLogFile(const QString& filePath) {
    QMutexLocker locker(&mutex);
    if (logFile.isOpen()) logFile.close();
    logFilePath = filePath;

    const QFileInfo info(filePath);
    QDir dir(info.absolutePath());
    if (!dir.exists()) dir.mkpath(".");

    logFile.setFileName(logFilePath);
    logFile.open(QIODevice::Append | QIODevice::Text);
}





void Log::enableFileLogging(bool enable)    { fileLoggingEnabled = enable; }
void Log::enableInAppLogging(bool enable)   { inAppLoggingEnabled = enable; }
void Log::enableConsoleLogging(bool enable) { consoleLoggingEnabled = enable; }
void Log::setMinLevel(LogLevel level)       { minLevel = level; }
void Log::setMaxFileSize(qint64 bytes)      { maxFileSizeBytes = bytes; }