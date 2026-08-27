#pragma once

#include <QString>
#include <QList>
#include <QMap>
#include "Spatial.h"

struct Geom {
    QString type;
    QString mesh;
    QString material;
    QList<double> size;
    Position pos;
    QList<double> color;
};




struct ActuatorDef {
    QString type;              // "position" | "velocity" | "motor" -- empty means no actuator
    QList<double> range;
    QList<double> ctrlrange;
    QList<double> forceRange;
    double kp = 0.0;
    double kv = 0.0;
};




struct SensorDef {
    QString type;               // e.g. "jointpos", "jointvel", "accelerometer" -- empty means no sensor
};




struct Site {
    QString id;
    Transform localTransform;
    SensorDef sensor;           // optional site-attached sensor (accelerometer, etc.)
};




struct Node {
    QString id;
    double mass = 0.0;
    Transform localTransform;
    QList<Geom> geoms;
    QList<Site> sites;
};




struct Edge {
    QString id;
    QString bodyA;
    QString bodyB;
    QString type;
    Transform localTransform;
    Position axis;               // [TODO]: what is this being used for ?
    QList<double> range;
    double damping = 0.0;
    double armature = 0.0;
    double frictionloss = 0.0;
    bool collision = true;

    ActuatorDef actuator;        // optional actuator driving this joint
    SensorDef sensor;            // optional joint-attached sensor (jointpos/jointvel)
};




class KinematicGraph {
public:
    KinematicGraph();
    ~KinematicGraph();

    void setDefaultNode(const QString& nodeId);
    void addNode(const Node& node);
    void addEdge(const Edge& edge);

    Node getDefaultNode() const;
    Node getNode(const QString& id) const;
    Edge getEdge(const QString& id) const;
    QMap<QString, Node> getNodes() const;
    QList<Edge> getEdges() const;
    QList<Edge> getEdgesForNode(const QString& nodeId) const;
    void removeNode(const QString& id);
    void removeEdge(const QString& id);
    bool containsNode(const QString& id) const;
    void clear();

private:
    QString defaultNodeId;
    QMap<QString, Node> nodes;
    QList<Edge> edges;
};
