NOTE: NOT FULLY UPDATED TO MATCH CURRENT CODEBASE

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
| `RemoveComponentCommand` | `src/Commands/RemoveComponentCommand.h` | Dedicated command for permanently deleting a component + its subtree (owns the subtree while done). |
| `Project::takeComponent()` / `adoptComponent()` | `src/Document/Project.cpp` | Unlink/re-link an instance without deleting it — the undo/redo primitives for creation. |
| `Project::takeSubtree()` / `adoptSubtree()` | `src/Document/Project.cpp` | Subtree variants of the above — the undo/redo primitives for deletion. |
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
  do/undo, which regenerates the MuJoCo XML and refreshes the editor. Turn it **off**
  for anything that doesn't change the MJCF — joint targets and actuator controls sync
  live through the physics loop (`syncToMujocoJoint`/`syncToMujocoActuator`), and the
  project name only labels `<mujoco model="...">`. Keep it on for structural edits
  (attach/detach/delete/replace, snap angle, connector reassign, root rotation),
  which are baked into the XML.

### Merging (slider drags)

Give a command a non-empty `mergeKey` and repeated pushes with the same key collapse into one
undo step: the merged command keeps the **first** undo lambda (returns to the value before the
drag started) and the **latest** do lambda. Empty mergeKey = never merge. Merging only ever
happens with the command **on top of the stack** — so a shared key means "consecutive edits of
the same field merge", and a finished edit run is sealed as soon as any other command lands
on top.

The joint sliders exploit this with two key schemes (`InspectorPanel::build_inputs`):

- **Mouse drag** — while moving, no commands are pushed at all: the value is previewed
  directly on the document (the EDITING physics loop shows the motion live) and
  `InspectorPanel::buildUI()` defers rebuilds so the slider keeps focus. `sliderPressed`
  assigns a **unique** key (`joint:<uid>:<jkey>:drag:<session>`); `sliderReleased` pushes one
  command (old = value captured at press) under that key. Because the key is unique per drag,
  the next edit can never merge into it — every drag is exactly one undo step.
- **Keyboard / wheel / spinbox nudges** — pushed immediately under a shared per-field key
  (`joint:<uid>:<jkey>:nudge`), so a run of consecutive nudges is one undo step, but a drag
  (or any other command) in between starts a fresh step.

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

## Deleting components (the other special case)

The cross button on an attached connector offers **Detach** (plain value-flip command —
the instance stays in the project as an orphan, selectable in connector combos) and
**Delete Permanently** (`RemoveComponentCommand`), which removes the component *and its
whole subtree*. The ownership rule is the mirror image of creation:

- **Done state** → the **command** owns the subtree (`Project::takeSubtree()` unlinked
  the root and removed every descendant from `componentMap`; intra-subtree links stay
  intact).
- **Undone state** → the `Project` owns it again (`Project::adoptSubtree()` re-links
  the same instances — same uids, same emulators).
- **Command destroyed** while in the done state → it recursively deletes the instances
  *and their emulators*.

## Stack lifecycle rules

- **Project open** → `undoStack.clear()`. Old commands point into the old component tree;
  keeping them would be use-after-free.
- **Successful save** → `undoStack.setClean()` (in `Project::saveProject()`), enabling
  "modified since save" tracking later.
- Stack changes (`indexChanged`) trigger `EditorWindow::scheduleRefresh()` **only when the
  command affects structure** (`GenericCommand::affectsStructure()`, same flag as
  `requiresReload`). Value-only pushes (joint targets) skip the refresh entirely — the
  committing widget already shows the value, and rebuilding would steal focus. Undoing a
  value-only command calls `InspectorPanel::updateJointValues()`, which syncs the
  sliders/spinboxes from the document in place — no rebuild, no focus loss. (Redo of a
  value-only command is indistinguishable from a push at the signal level, so it skips
  the refresh — the inspector catches up on the next structural edit or reselection.)
- `scheduleRefresh()` and `Application::reloadSimulation()` are both **coalesced**: multiple
  triggers in the same event-loop turn (a structural command fires both) collapse into one
  rebuild / one XML regeneration — no double rebuilds, no double log lines.
- Every command logs `Do:` / `Undo:` / `Redo: <text>` via `Log::info` (in `GenericCommand`,
  `AddComponentCommand`, `RemoveComponentCommand`), so the Output panel shows the full
  edit history as it happens.

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

- Detached (orphan) components can't be deleted from the UI yet — only attached ones
  (via the connector cross button).
- Script file operations (add/rename script) are not undoable.
- No visual "unsaved changes" marker yet (`setClean()` is already in place to support one).
