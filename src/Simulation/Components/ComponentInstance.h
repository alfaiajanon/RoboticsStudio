#pragma once

#include <QString>
#include <QList>
#include <QMap>
#include <QVariant>
#include <vector>
#include <mutex>
#include "ComponentBlueprint.h"
#include "Simulation/ErrorSystem/Emulator.h"
#include "Utils/Spatial.h"
#include "Utils/Buffer.h"



/*
 * Live memory buffer representing an individual input or output stream for a component.
 * Used to sync UI sliders and sensors with MuJoCo's low-level data arrays.
 */
struct IOStream {
    int mujocoId = -1;       
    double targetValue = 0;  
    double currentValue = 0; 
    RingBuffer<PlotPoint> historyBuffer{0};
};




/*
 * Defines an explicit closed-loop connection between two components in the workspace.
 * Generates MuJoCo equality constraints only when the kinematic tree cycles back on itself.
 */
class Constraint {
public:
    int componentAUid;
    int componentBUid;
    QString connectorA;
    QString connectorB;
    float snapAngle = 0.0f;
};




/*
 * Represents a live, instantiated component within the project workspace.
 * Holds its unique spatial state via a unified Transform, hierarchical connections, and live IO.
 */
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

        void setActuatorTarget(const QString& key, double value);
        double getActuatorTarget(const QString& key) const;
        void setJointTarget(const QString& key, double value);
        double getJointTarget(const QString& key) const;
        void setSensorCurrent(const QString& key, double value);
        double getSensorCurrent(const QString& key) const;

        RingBuffer<PlotPoint>* getSensorBuffer(const QString& key);
        RingBuffer<PlotPoint>* getActuatorBuffer(const QString& key);

        void setMujocoActuatorId(const QString& key, int id);
        void setMujocoSensorId(const QString& key, int id);
        void setMujocoJointId(const QString& key, int id);
        int getMujocoActuatorId(const QString& key) const;
        int getMujocoSensorId(const QString& key) const;
        int getMujocoJointId(const QString& key) const;

};