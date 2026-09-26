#pragma once

#include "Utils/Global.h"
#include "Utils/Spatial.h"
#include <QVariant>
#include <qobject.h>
#include <vector>
#include <mutex>
#include <memory>
#include <variant>


class Emulator;
class ComponentBlueprint;


// using IOData = std::variant<double, std::vector<double>>;

// struct IOStream {
//     int mujocoId = -1;
//     int channelId = -1;
//     IOData targetData = 0.0;
//     IOData currentData = 0.0;
// };

struct BasicIOValue {
    int mujocoId = -1;
    int dim = 1;
    std::vector<double> data;
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

        // plotting data
        QMap<QString, std::shared_ptr<BasicIOValue>> joints;
        QMap<QString, std::shared_ptr<BasicIOValue>> sensors;
        QMap<QString, std::shared_ptr<BasicIOValue>> actuators;

        ComponentBlueprint* blueprint = nullptr;

    public:
        // data
        int uid;
        QString modelId;
        QString name;

        // parent/child connections
        int parentUid;
        QString parentConnector;
        QString selfConnector;
        float snapAngle;
        QList<ComponentInstance*> children;

        // self structuring
        Transform transform;

        // joint parameters
        QMap<QString, QVariant> parameters;

        // Emulator/Controller
        Emulator* emulator = nullptr;



        //----------------------------//
        //          methods           //
        //----------------------------//

        ComponentInstance() : uid(-1), parentUid(-1), snapAngle(0.0f) {}

        QList<QString> getFreeConnections() const;
        QMap<QString, QPair<int, QString>> getActiveConnections() const;

        ComponentBlueprint* getBlueprint() const;
        void setBlueprint(ComponentBlueprint* blueprint);

        // void initializeIO();

        void setActuatorValue(const QString& key, BasicIOValue& value);
        void setJointValue(const QString& key, BasicIOValue& value);
        void setSensorValue(const QString& key, BasicIOValue& value);
        BasicIOValue getActuatorValue(const QString& key) const;
        BasicIOValue getJointValue(const QString& key) const;
        BasicIOValue getSensorValue(const QString& key) const;

        // int getActuatorChannelId(const QString& key) const;
        // int getSensorChannelId(const QString& key) const;

        // void setMujocoActuatorId(const QString& key, int id);
        // void setMujocoSensorId(const QString& key, int id);
        // void setMujocoJointId(const QString& key, int id);
        // int getMujocoActuatorId(const QString& key) const;
        // int getMujocoSensorId(const QString& key) const;
        // int getMujocoJointId(const QString& key) const;
};
