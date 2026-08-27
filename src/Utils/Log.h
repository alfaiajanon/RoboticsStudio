#pragma once
#include <QObject>
#include <QString>
#include <QDebug>
#include <QJsonObject>
#include <QJsonDocument>
#include <QFile>
#include <QMutex>



enum class LogLevel : int {
    Debug = 0,
    Info,
    Warning,
    Error,
    Critical
};




/*
Dispatches log events to any UI (log console, status bar, etc).
If you connect a slot from a widget while logging happens on a worker
thread, use Qt::QueuedConnection so the slot runs safely on the GUI thread.
*/
class LogDispatcher : public QObject {
    Q_OBJECT
    public:
        static LogDispatcher* instance();
        static LogDispatcher* getInstance() { return instance(); } // back-compat

    signals:
        void messageLogged(LogLevel level, const QString& category,
                            const QString& plainMessage, const QString& htmlMessage);

    private:
        explicit LogDispatcher(QObject* parent = nullptr) : QObject(parent) {}
};





/*
Extensibility point
1.  If you specialize LogFormatter<T>, that's used.
2.  Otherwise, it falls back to T's operator<<(QDebug&, const T&) —
    which already covers int, bool, double, QStringList, QVariant,
    QByteArray, and most other Qt types for free.

To make your own type loggable, specialize it once, anywhere:
    template<> struct LogFormatter<MyType> {
        static QString toString(const MyType& v) { return v.toDisplayString(); }
    };
*/

template<typename T>
struct LogFormatter {
    static QString toString(const T& value) {
        QString result;
        QDebug(&result).nospace() << value;
        return result;
    }
};

template<> struct LogFormatter<QString> {
    static QString toString(const QString& value) { return value; }
};

template<> struct LogFormatter<const char*> {
    static QString toString(const char* value) { return QString::fromUtf8(value); }
};

template<> struct LogFormatter<QJsonObject> {
    static QString toString(const QJsonObject& value) {
        return QString::fromUtf8(QJsonDocument(value).toJson(QJsonDocument::Compact));
    }
};

template<> struct LogFormatter<QJsonDocument> {
    static QString toString(const QJsonDocument& value) {
        return QString::fromUtf8(value.toJson(QJsonDocument::Compact));
    }
};








#pragma region Main Log Class

class Log {
public:
    template<typename T>
    static void debug(const T& value, const QString& category = QString()) {
        write(LogLevel::Debug, LogFormatter<T>::toString(value), category);
    }
    template<typename T>
    static void info(const T& value, const QString& category = QString()) {
        write(LogLevel::Info, LogFormatter<T>::toString(value), category);
    }
    template<typename T>
    static void warning(const T& value, const QString& category = QString()) {
        write(LogLevel::Warning, LogFormatter<T>::toString(value), category);
    }
    template<typename T>
    static void error(const T& value, const QString& category = QString()) {
        write(LogLevel::Error, LogFormatter<T>::toString(value), category);
    }
    template<typename T>
    static void critical(const T& value, const QString& category = QString()) {
        write(LogLevel::Critical, LogFormatter<T>::toString(value), category);
    }

    static void setLogFile(const QString& filePath);
    static void enableFileLogging(bool enable);
    static void enableInAppLogging(bool enable);
    static void enableConsoleLogging(bool enable);
    static void setMinLevel(LogLevel level);
    static void setMaxFileSize(qint64 bytes); // triggers rotation when exceeded

private:
    static void write(LogLevel level, const QString& message, const QString& category);
    static QString levelToString(LogLevel level);
    static QString colorForLevel(LogLevel level);
    static void rotateIfNeeded();

    static bool fileLoggingEnabled;
    static bool inAppLoggingEnabled;
    static bool consoleLoggingEnabled;
    static LogLevel minLevel;
    static QString logFilePath;
    static QFile logFile;
    static qint64 maxFileSizeBytes;
    static QMutex mutex;
};

// Optional convenience macros for call sites
#define LOG_DEBUG(msg)    Log::debug(msg)
#define LOG_INFO(msg)     Log::info(msg)
#define LOG_WARNING(msg)  Log::warning(msg)
#define LOG_ERROR(msg)    Log::error(msg)