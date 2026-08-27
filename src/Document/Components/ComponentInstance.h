#pragma once

#include "Utils/Global.h"
#include "Utils/Spatial.h"
#include <QVariant>
#include <vector>
#include <mutex>
#include <memory>
#include <variant>
#include "ComponentBlueprint.h"

class Emulator;


using IOData = std::variant<double, std::vector<double>>;



struct IOStream {
    int mujocoId = -1;       
    int channelId = -1;
    IOData targetData = 0.0;  
    IOData currentData = 0.0; 
};


class Constraint {
    public:
        int componentAUid;
        int componentBUid;
        QString connectorA;
        QString connectorB;
        float snapAngle = 0.0f;
};






class ComponentInstance {
    private:
        mutable std::mutex ioMutex;

        QMap<QString, std::shared_ptr<IOStream>> joints;
        QMap<QString, std::shared_ptr<IOStream>> sensors;
        QMap<QString, std::shared_ptr<IOStream>> actuators;

    public:
        int uid;
        QString name;
        QString type;
        QString model;
        
        int parentUid;
        QString parentConnector;
        QString selfConnector;
        float snapAngle;
        
        QMap<QString, QVariant> parameters;
        
        Transform transform;
        Emulator* emulator = nullptr;
        
        ComponentBlueprint* blueprint = nullptr;
        QList<ComponentInstance*> children;

        ComponentInstance() : uid(-1), parentUid(-1), snapAngle(0.0f) {}
        
        QList<QString> getFreeConnections() const;
        QMap<QString, QPair<int, QString>> getActiveConnections() const; 

        void initializeIO();

        void setActuatorTarget(const QString& key, IOData& value);
        void setJointTarget(const QString& key, IOData& value);
        void setSensorCurrent(const QString& key, IOData& value);
        IOData getActuatorTarget(const QString& key) const;
        IOData getJointTarget(const QString& key) const;
        IOData getSensorCurrent(const QString& key) const;

        int getActuatorChannelId(const QString& key) const;
        int getSensorChannelId(const QString& key) const;

        void setMujocoActuatorId(const QString& key, int id);
        void setMujocoSensorId(const QString& key, int id);
        void setMujocoJointId(const QString& key, int id);
        int getMujocoActuatorId(const QString& key) const;
        int getMujocoSensorId(const QString& key) const;
        int getMujocoJointId(const QString& key) const;
};