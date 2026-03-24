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



struct Site {
    QString id;
    Transform localTransform;
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
    Position axis;
    QList<double> range;
    double damping = 0.0;
    double armature = 0.0;
    double frictionloss = 0.0;
};




class KinematicGraph {
public:
    KinematicGraph();
    ~KinematicGraph();

    void addNode(const Node& node);
    void addEdge(const Edge& edge);

    Node getNode(const QString& id) const;
    QMap<QString, Node> getNodes() const;
    QList<Edge> getEdgesForNode(const QString& nodeId) const;
    bool containsNode(const QString& id) const;
    void clear();

private:
    QMap<QString, Node> nodes;
    QList<Edge> edges;
};