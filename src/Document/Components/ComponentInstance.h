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

// Pixel layout of an ImageIOValue: RGB888 is 3 bytes per pixel, rows top to
// bottom; Depth32F is one float (meters from the camera) per pixel.
enum class ImageFormat { RGB888, Depth32F };

// Runtime value of an image signal (camera output or display input). Copying
// it is cheap: the pixels are shared and never modified once published -- a
// new frame is a new buffer. pixels is null until the first frame.
struct ImageIOValue {
    int mujocoId = -1;   // MuJoCo camera id; -1 for displays
    int width = 0;
    int height = 0;
    ImageFormat format = ImageFormat::RGB888;
    double time = 0.0;   // sim time the frame shows (camera: capture time)
    quint64 seq = 0;     // increments with every published frame
    std::shared_ptr<const QByteArray> pixels;
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
        QMap<QString, std::shared_ptr<ImageIOValue>> images;   // camera outputs + display inputs, by signal name

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
        void setImageValue(const QString& key, ImageIOValue& value);
        BasicIOValue getActuatorValue(const QString& key) const;
        BasicIOValue getJointValue(const QString& key) const;
        BasicIOValue getSensorValue(const QString& key) const;
        ImageIOValue getImageValue(const QString& key) const;

        // int getActuatorChannelId(const QString& key) const;
        // int getSensorChannelId(const QString& key) const;

        // void setMujocoActuatorId(const QString& key, int id);
        // void setMujocoSensorId(const QString& key, int id);
        // void setMujocoJointId(const QString& key, int id);
        // int getMujocoActuatorId(const QString& key) const;
        // int getMujocoSensorId(const QString& key) const;
        // int getMujocoJointId(const QString& key) const;
};
