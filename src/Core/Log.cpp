#include "Log.h"

using namespace std;
#include <QJsonObject>


bool Log::fileLoggingEnabled = false;
bool Log::inAppLoggingEnabled = false;



void Log::info(const char* message){
    cout << "[INFO]: " << message << endl;
}

void Log::warning(const char* message){
    cout << "[WARNING]: " << message << endl;
}

void Log::error(const char* message){
    cerr << "[ERROR]: " << message << endl;
}



void Log::info(const QString& message){
    cout << "[INFO]: " << message.toStdString() << endl;
}

void Log::warning(const QString& message){
    cout << "[WARNING]: " << message.toStdString() << endl;
}

void Log::error(const QString& message){
    cerr << "[ERROR]: " << message.toStdString() << endl;
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


