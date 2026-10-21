#pragma once

#include <QList>
#include <QPointF>
#include <QString>

#include "Application/Application.h"
#include "Commands/GenericCommand.h"
#include "Document/Project.h"
#include "Document/Components/ComponentInstance.h"


/*
 * Undoable edits of the Real-level electronics: wires, the schematic layout,
 * and a board's firmware slot. None of them changes the
 * MJCF, so there is no simulation reload; they count as structural so the
 * editor refresh rebuilds the wiring page, the Inspector and the checks.
 */
namespace Commands {

inline void addWire(const Wire& w) {
    Project* project = Application::getInstance()->getProject();
    if (project->netlist().hasWire(w) || w.a == w.b) return;
    Application::getInstance()->getUndoStack()->push(new GenericCommand(
        [project, w]() { project->netlist().addWire(w); },
        [project, w]() { project->netlist().removeWire(w); },
        QString("Wire %1.%2 to %3.%4").arg(w.a.uid).arg(w.a.pin).arg(w.b.uid).arg(w.b.pin)));
}

inline void removeWires(const QList<Wire>& wires) {
    if (wires.isEmpty()) return;
    Project* project = Application::getInstance()->getProject();
    Application::getInstance()->getUndoStack()->push(new GenericCommand(
        [project, wires]() { for (const Wire& w : wires) project->netlist().removeWire(w); },
        [project, wires]() { for (const Wire& w : wires) project->netlist().addWire(w); },
        wires.size() == 1 ? QString("Remove wire") : QString("Remove %1 wires").arg(wires.size())));
}

// One undo step per drag (the page pushes it when the drag ends).
inline void moveSchematicBlocks(const QHash<int, QPointF>& from, const QHash<int, QPointF>& to) {
    if (from == to) return;
    Project* project = Application::getInstance()->getProject();
    auto apply = [project](const QHash<int, QPointF>& pos) {
        for (auto it = pos.begin(); it != pos.end(); ++it) project->layout().insert(it.key(), it.value());
    };
    Application::getInstance()->getUndoStack()->push(new GenericCommand(
        [apply, to]() { apply(to); }, [apply, from]() { apply(from); }, "Move schematic block"));
}

inline void setBoardFirmware(int uid, const QString& sketch) {
    Project* project = Application::getInstance()->getProject();
    ComponentInstance* board = project->getComponentByUid(uid);
    if (!board || board->firmware == sketch) return;
    const QString old = board->firmware;
    auto apply = [project, uid](const QString& s) {
        if (ComponentInstance* b = project->getComponentByUid(uid)) b->firmware = s;
    };
    Application::getInstance()->getUndoStack()->push(new GenericCommand(
        [apply, sketch]() { apply(sketch); }, [apply, old]() { apply(old); }, "Set board firmware"));
}

} // namespace Commands
