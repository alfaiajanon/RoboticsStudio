#pragma once

#include <array>
#include <memory>
#include <mutex>
#include <vector>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QVariantMap>


/*
 * The world around the robot: environment (sky, lights, fog, physics
 * options), floor and free-standing props. Authored in the Stage panel,
 * stored under "stage" in the .rsproj, and turned into MJCF fragments that
 * Project::generateMujocoXML splices around the robot.
 *
 * Controlled props are upright "character bodies" (slide x/y/z + yaw hinge,
 * velocity actuators on x/y/yaw) driven by the stage script through
 * PropHandle; the stage script also sees the robot's pose through RobotPose
 * and nothing else. The robot MCU never sees either.
 */

using Vec3 = std::array<double, 3>;
using Vec4 = std::array<double, 4>;



/*
 * Script object for one controlled prop, injected as prop_<id> into the
 * stage script. The MCU thread writes commands and reads the published
 * state; the physics thread takes commands and publishes state (see
 * SimulationManager::syncToMujocoProps / syncFromMujocoProps). Both sides
 * go through `mutex`.
 */
class PropHandle : public QObject {
    Q_OBJECT

    public:
        struct Command {
            double vx = 0, vy = 0;      // m/s; world frame, or forward/strafe when local
            bool local = false;
            double yawRate = 0;         // deg/s
            bool teleport = false;
            double tp[4] = {0, 0, 0, 0}; // x, y, z (m), yaw (deg)
        };

        QString id;
        Vec3 basePos{0, 0, 0};          // body pos in the MJCF; slide qpos is relative to it
        double baseYaw = 0;             // deg, authored yaw (lives on the geom, the hinge starts at 0)

        // MuJoCo ids, resolved by SimulationManager::cacheStageIds (physics thread only)
        int bodyId = -1, qposAdr = -1, dofAdr = -1;   // x,y,z,yaw joints are consecutive
        int actX = -1, actY = -1, actYaw = -1;

        explicit PropHandle(const QString& id, QObject* parent = nullptr) : QObject(parent), id(id) {}

        // physics side
        Command takeCommand();
        void publish(const Vec3& pos, const Vec3& vel, double yawDeg);
        void resetRuntime();

        // stage script API
        Q_INVOKABLE void setVelocity(double vx, double vy);
        Q_INVOKABLE void move(double forward, double strafe);
        Q_INVOKABLE void setYawRate(double degPerSec);
        Q_INVOKABLE void stop();
        Q_INVOKABLE void teleport(double x, double y, double z, double yawDeg);
        Q_INVOKABLE QVariantMap getPosition() const;
        Q_INVOKABLE QVariantMap getVelocity() const;
        Q_INVOKABLE double getYaw() const;

    private:
        mutable std::mutex mutex;
        Command command;
        Vec3 pos{0, 0, 0}, vel{0, 0, 0};
        double yaw = 0;
};



// The robot's root pose, the only robot state the stage script can read (as `robot`).
class RobotPose : public QObject {
    Q_OBJECT

    public:
        int bodyId = -1;   // resolved by SimulationManager::cacheStageIds

        void publish(const Vec3& pos, const Vec3& rpyDeg);

        Q_INVOKABLE QVariantMap getPosition() const;
        Q_INVOKABLE QVariantMap getRotation() const;   // roll, pitch, yaw in degrees

    private:
        mutable std::mutex mutex;
        Vec3 pos{0, 0, 0}, rpy{0, 0, 0};
};



class Stage {
    public:
        struct Environment {
            Vec3 skyTop{0.4, 0.6, 0.8};
            Vec3 skyBottom{0, 0, 0};
            Vec3 ambient{0.1, 0.1, 0.1};        // headlight
            Vec3 headlight{0.4, 0.4, 0.4};
            Vec3 lightDir{0, 0, -1};
            Vec3 lightDiffuse{0.8, 0.8, 0.8};
            bool castShadow = true;
            int shadowSize = 4096;
            bool fog = false;
            double fogStart = 3, fogEnd = 10;   // multiples of the model extent (MuJoCo <map>)
            Vec3 fogColor{0.5, 0.5, 0.5};
            Vec3 gravity{0, 0, -9.81};
            double timestep = 0.002;
        };

        struct Floor {
            QString type = "plane";             // plane | mesh | heightfield | none
            QString pattern = "checker";        // plane: checker | solid
            Vec3 color1{0.2, 0.3, 0.4};
            Vec3 color2{0.1, 0.15, 0.2};
            double texRepeat = 10;
            QString file;                       // mesh / heightfield, relative to the project dir
            Vec3 scale{1, 1, 1};                // mesh scale; heightfield: half x, half y, height
            Vec3 pos{0, 0, 0};
            Vec3 euler{0, 0, 0};
            Vec3 friction{1, 0.005, 0.0001};
        };

        struct Prop {
            QString id;
            QString shape = "box";              // box | sphere | cylinder | capsule | mesh
            Vec3 size{0.05, 0.05, 0.05};        // MuJoCo geom size (mesh: scale)
            QString mesh;                       // relative to the project dir
            Vec3 pos{0.3, 0, 0.05};
            Vec3 euler{0, 0, 0};                // deg
            Vec4 rgba{0.8, 0.5, 0.2, 1};
            double mass = 0.2;
            Vec3 friction{1, 0.005, 0.0001};
            QString motion = "dynamic";         // static | dynamic | controlled
            double maxSpeed = 1.0;              // m/s
            double maxYawRate = 180;            // deg/s
            double response = 100;              // 1/s, velocity servo bandwidth (gain = response * mass)
        };

        // Everything authored; copyable, so the panel edits a copy and commits it.
        struct Data {
            Environment env;
            Floor floor;
            QList<Prop> props;
            QStringList scripts;                // stage scripts, relative to the project dir
            int currentScript = -1;             // index into scripts; -1 = no stage script

            QString activeScript() const {
                return (currentScript >= 0 && currentScript < scripts.size()) ? scripts[currentScript] : QString();
            }
            Vec3 robotSpawn{0, 0, 0};
        };

        Stage();
        ~Stage();

        const Data& getData() const { return data; }
        void setData(const Data& d);            // also rebuilds the prop handles

        static Data fromJson(const QJsonObject& obj);
        static QJsonObject toJson(const Data& d);
        static QString validate(const Data& d); // empty when fine

        void setProjectDirectory(const QString& dir) { projectDir = dir; }

        // MJCF fragments
        QString assetsXML() const;
        QString worldBodyXML(bool isSimulation) const;
        QString actuatorsXML(bool isSimulation) const;
        QString visualXML() const;              // children of <visual>
        QString optionXML() const;              // a full <option .../> line

        // runtime
        const std::vector<std::unique_ptr<PropHandle>>& getHandles() const { return handles; }
        RobotPose* getRobotPose() { return &robotPose; }

    private:
        Data data;
        QString projectDir;
        std::vector<std::unique_ptr<PropHandle>> handles;
        RobotPose robotPose;

        QString absPath(const QString& rel) const;
        void rebuildHandles();
};
