#include "Graph.h"

KinematicGraph::KinematicGraph() {}




KinematicGraph::~KinematicGraph() {
    clear();
}




/*
 * Inserts a new node into the graph dictionary.
 * Nodes represent physical bodies with mass and collision geometries.
 */
void KinematicGraph::addNode(const Node& node) {
    nodes.insert(node.id, node);
}




/*
 * Appends a new edge to the graph's edge list.
 * Edges represent the mechanical joints connecting two specific nodes.
 */
void KinematicGraph::addEdge(const Edge& edge) {
    edges.append(edge);
}




/*
 * Retrieves a copy of a specific node by its string ID.
 * Returns an empty node if the ID does not exist in the graph.
 */
Node KinematicGraph::getNode(const QString& id) const {
    return nodes.value(id, Node());
}




/*
 * Finds all edges connected to a specific node.
 * Checks both bodyA and bodyB to ensure bidirectional traversal is possible.
 */
QList<Edge> KinematicGraph::getEdgesForNode(const QString& nodeId) const {
    QList<Edge> connectedEdges;
    for (const Edge& edge : edges) {
        if (edge.bodyA == nodeId || edge.bodyB == nodeId) {
            connectedEdges.append(edge);
        }
    }
    return connectedEdges;
}




bool KinematicGraph::containsNode(const QString& id) const {
    return nodes.contains(id);
}




void KinematicGraph::clear() {
    nodes.clear();
    edges.clear();
}