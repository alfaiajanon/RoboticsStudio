#include "Log.h"

using namespace std;
#include <QJsonObject>


bool Log::fileLoggingEnabled = false;
bool Log::inAppLoggingEnabled = false;




void Log::info(const QString& message){
    cout << "[INFO]: " << message.toStdString() << endl;

    QString htmlMsg = QString("<span style='color:#a9b7c6;'>[INFO]: %1</span>").arg(message.toHtmlEscaped());
    emit LogDispatcher::getInstance()->messageLogged(htmlMsg);
}

void Log::warning(const QString& message){
    cout << "[WARNING]: " << message.toStdString() << endl;
    
    QString htmlMsg = QString("<span style='color:#ffcc00;'>[WARNING]: %1</span>").arg(message.toHtmlEscaped());
    emit LogDispatcher::getInstance()->messageLogged(htmlMsg);
}

void Log::error(const QString& message){
    cerr << "[ERROR]: " << message.toStdString() << endl;
    
    QString htmlMsg = QString("<span style='color:#ff5555;'>[ERROR]: %1</span>").arg(message.toHtmlEscaped());
    emit LogDispatcher::getInstance()->messageLogged(htmlMsg);
}



void Log::info(const QJsonObject& message){
    QJsonDocument doc(message);
    cout << "[INFO]: " << doc.toJson().toStdString() << endl;
}

void Log::warning(const QJsonObject& message){
    QJsonDocument doc(message);
    cout << "[WARNING]: " << doc.toJson().toStdString() << endl;
}

void Log::error(const QJsonObject& message){
    QJsonDocument doc(message);
    cerr << "[ERROR]: " << doc.toJson().toStdString() << endl;
}




void Log::setLogFile(const QString& filePath){
    // Implementation to set log file path
}


void Log::enableFileLogging(bool enable){
    fileLoggingEnabled = enable;
}


void Log::enableInAppLogging(bool enable){
    inAppLoggingEnabled = enable;
}



