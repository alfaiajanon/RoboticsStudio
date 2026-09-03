# Command System (Undo/Redo Architecture)

How user edits flow through RoboticsStudio, and how to add new undoable actions.

## The 30-second version

Every user edit is packaged as a **command** object with two functions: `redo()` (apply the
change) and `undo()` (revert it). Commands are pushed onto one central **undo stack** owned by
`Application`. `stack->push(cmd)` immediately calls `redo()`, so "do it now" and "do it again
after an undo" are the same code path. Ctrl+Z / Ctrl+Y walk the stack back and forth.

```
UI event (panel/widget)
      │
      ▼
Command { captured old/new values, doFn, undoFn }
      │
      ▼  stack->push()  →  runs redo() right away
QUndoStack (Application::getUndoStack())
      │
      ▼  command mutates the document directly
Project / ComponentInstance  →  reloadSimulation()  →  fresh MJCF XML + UI refresh
```

Commands never talk to the UI. They mutate the **document** (`Project`, `ComponentInstance`
fields); the simulation reload and the panel refresh happen afterwards.

## The pieces

| Piece | File | Role |
|---|---|---|
| `QUndoStack undoStack` | `src/Application/Application.h` | The single history for the whole app. |
| `GenericCommand` | `src/Commands/GenericCommand.h` | Lambda-based `QUndoCommand` — no subclass needed for simple edits. |
| `Commands::push()` | `src/Commands/CommandUtils.h` | Shared helper: pushes a `GenericCommand`, optionally wrapped with `reloadSimulation()`. |
| `AddComponentCommand` | `src/Commands/AddComponentCommand.h` | Dedicated command for creating a component on a connector (owns the instance while undone). |
| `Project::takeComponent()` / `adoptComponent()` | `src/Document/Project.cpp` | Unlink/re-link an instance without deleting it — the undo/redo primitives for creation. |
| Edit-menu actions | `src/View/Windows/EditorWindow.cpp` | `createUndoAction`/`createRedoAction` → dynamic labels + auto enable/disable. |

## GenericCommand in detail

```cpp
Commands::push(
    [uid, newAngle]() { /* do:   store new value */ },
    [uid, oldAngle]() { /* undo: restore old value */ },
    "Set snap angle",          // text shown in the Edit menu
    "snap:" + QString::number(uid)  // mergeKey (optional)
);
```

- Capture **both** the old and the new value when building the command. The lambdas must be
  able to run any number of times, in any order (do, undo, redo, undo, …).
- Look components up by **uid inside the lambda** (`project->getComponentByUid(uid)`), not by
  caching a pointer you assume is still linked — safer across structural changes.
- `requiresReload` (default `true`) calls `Application::reloadSimulation()` after each
  do/undo, which regenerates the MuJoCo XML and refreshes the editor. Leave it on for anything
  that affects the assembly.

### Merging (slider drags)

Give a command a non-empty `mergeKey` and repeated pushes with the same key collapse into one
undo step: the merged command keeps the **first** undo lambda (returns to the value before the
drag started) and the **latest** do lambda. Empty mergeKey = never merge.

### Macros (multi-step actions)

```cpp
stack->beginMacro("Replace connected component");
Commands::push(detachDo, detachUndo, "Detach");
Commands::push(attachDo, attachUndo, "Attach");
stack->endMacro();
```

One Ctrl+Z now undoes both steps together.

## Creating components (the special case)

"Add component" can't be a simple value-swap: it allocates a `ComponentInstance`. The rule:

- **Done state** → the `Project` owns the instance (it's in `componentMap`, linked to its parent).
- **Undone state** → the **command** owns it (`Project::takeComponent()` unlinked it without deleting).
- **Command destroyed** (stack cleared / undo limit) → the command deletes the instance *and its
  emulator* (emulators are unparented heap `QObject`s and don't free themselves).
- **Redo** re-links the *same* instance via `Project::adoptComponent()` — same uid, same emulator,
  no re-creation. UIDs stay monotonic (`nextComponentUid` is never rolled back).

UI sites (drag-and-drop from the Component Library) just do:

```cpp
stack->push(new AddComponentCommand(project, parentUid, connector, modelId, "", 0.0f));
```

`parentUid == 0` means "attach as root".

## Stack lifecycle rules

- **Project open** → `undoStack.clear()`. Old commands point into the old component tree;
  keeping them would be use-after-free.
- **Successful save** → `undoStack.setClean()` (in `Project::saveProject()`), enabling
  "modified since save" tracking later.
- Any stack change (`indexChanged`) triggers `EditorWindow::refresh()`, so the scene tree and
  inspector always match the document after undo/redo.

## Recipe: adding a new undoable action

1. At the UI call site, read the **current (old) value** from the document.
2. Call `Commands::push(doFn, undoFn, "Human text", mergeKeyOrEmpty)`.
3. In the lambdas, mutate only document state (by uid lookup); no widget code.
4. If the action is structural (attach/detach/replace), test undo **and** redo — both run the
  same lambdas repeatedly.
5. If the action allocates or destroys document objects, write a dedicated `QUndoCommand`
   subclass (like `AddComponentCommand`) with an explicit ownership rule — don't fudge it with
   lambdas.

## Known limits / future work

- No delete-component command yet (there is no delete UI at all).
- Script file operations (add/rename script) are not undoable.
- No visual "unsaved changes" marker yet (`setClean()` is already in place to support one).
