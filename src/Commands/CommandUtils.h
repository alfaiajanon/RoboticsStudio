#pragma once

#include <functional>
#include <QString>
#include <QUndoStack>

#include "Application/Application.h"
#include "Commands/GenericCommand.h"


/*
 * Shared undo-stack helper, usable from any panel/window (not just the
 * Inspector). Pushes a GenericCommand whose do/undo lambdas are wrapped
 * with Application::reloadSimulation() when requiresReload is set.
 *
 * mergeKey lets repeated edits to the same field (slider drag, spinbox
 * scroll/type) collapse into a single undo step. Leave it empty for
 * one-shot actions (attach/detach, button clicks) that should never merge.
 *
 * requiresReload doubles as the command's affectsStructure hint: structural
 * commands (XML changes) reload the simulation AND trigger an editor
 * refresh; value-only commands (joint targets) do neither -- they sync live.
 */
namespace Commands {

inline void push(std::function<void()> doFn, std::function<void()> undoFn,
                 const QString& text, const QString& mergeKey = "",
                 bool requiresReload = true) {
    auto* stack = Application::getInstance()->getUndoStack();
    stack->push(new GenericCommand(
        [doFn, requiresReload]() {
            doFn();
            if (requiresReload)
                Application::getInstance()->reloadSimulation();
        },
        [undoFn, requiresReload]() {
            undoFn();
            if (requiresReload)
                Application::getInstance()->reloadSimulation();
        },
        text,
        mergeKey,
        requiresReload
    ));
}

} // namespace Commands
