#pragma once

#include <iostream>
#include <QString>
#include <QJsonDocument>
#include <QJsonObject>


class LogDispatcher : public QObject {
    Q_OBJECT
    
    public:
        static LogDispatcher* getInstance() {
            static LogDispatcher instance;
            return &instance;
        }

    signals:
        void messageLogged(const QString& message);
};



class Log{

    static bool fileLoggingEnabled;
    static bool inAppLoggingEnabled;

    public:
        static void info(const QString& message);
        static void warning(const QString& message);
        static void error(const QString& message);

        static void info(const QJsonObject& message);
        static void warning(const QJsonObject& message);
        static void error(const QJsonObject& message);

        static void setLogFile(const QString& filePath);    
        static void enableFileLogging(bool enable);
        static void enableInAppLogging(bool enable);

};