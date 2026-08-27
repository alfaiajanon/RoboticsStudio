#include "Graph.h"

KinematicGraph::KinematicGraph() {}

KinematicGraph::~KinematicGraph() {
    clear();
}



void KinematicGraph::addNode(const Node& node) {
    nodes.insert(node.id, node);
}


void KinematicGraph::removeNode(const QString& id) {
    nodes.remove(id);
}


void KinematicGraph::addEdge(const Edge& edge) {
    edges.append(edge);
}


void KinematicGraph::removeEdge(const QString& id) {
    for (int i = 0; i < edges.size(); ++i) {
        if (edges[i].id == id) {
            edges.removeAt(i);
            return;
        }
    }
}


void KinematicGraph::setDefaultNode(const QString& nodeId) {
    defaultNodeId = nodeId;
}




Node KinematicGraph::getNode(const QString& id) const {
    return nodes.value(id, Node());
}


Node KinematicGraph::getDefaultNode() const {
    return nodes.value(defaultNodeId, Node());
}


QMap<QString, Node> KinematicGraph::getNodes() const {
    return nodes;
}


QList<Edge> KinematicGraph::getEdgesForNode(const QString& nodeId) const {
    QList<Edge> connectedEdges;
    for (const Edge& edge : edges) {
        if (edge.bodyA == nodeId || edge.bodyB == nodeId) {
            connectedEdges.append(edge);
        }
    }
    return connectedEdges;
}


QList<Edge> KinematicGraph::getEdges() const {
    return edges;
}


Edge KinematicGraph::getEdge(const QString& id) const {
    for (const Edge& edge : edges) {
        if (edge.id == id) return edge;
    }
    return Edge();
}




bool KinematicGraph::containsNode(const QString& id) const {
    return nodes.contains(id);
}


void KinematicGraph::clear() {
    nodes.clear();
    edges.clear();
}
