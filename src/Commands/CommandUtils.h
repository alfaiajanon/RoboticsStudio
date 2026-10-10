#pragma once

#include <functional>
#include <QString>
#include <QUndoStack>
#include <QJsonArray>

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

/*
 * Undoable edit of the project's MCU script list and active index. No MJCF
 * change, so no simulation reload; the command still counts as structural so
 * the editor refresh rebuilds the inspector combo and the script list.
 * extraDo/extraUndo run before the list change (e.g. deleting/restoring the
 * file on disk, which setScript() needs to exist when re-activating it).
 */
inline void setMcuScripts(const QJsonArray& paths, int current, const QString& text,
                          std::function<void()> extraDo = {}, std::function<void()> extraUndo = {}) {
    Project* project = Application::getInstance()->getProject();
    const QJsonArray oldPaths = project->getScriptPaths();
    const int oldCurrent = project->getCurrentScriptIdx();

    auto apply = [project](const QJsonArray& p, int c) {
        project->setScriptPaths(p);
        project->setScript(c);
    };
    Application::getInstance()->getUndoStack()->push(new GenericCommand(
        [apply, paths, current, extraDo]() { if (extraDo) extraDo(); apply(paths, current); },
        [apply, oldPaths, oldCurrent, extraUndo]() { if (extraUndo) extraUndo(); apply(oldPaths, oldCurrent); },
        text));
}

} // namespace Commands
