# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

RoboticsStudio: a drag-and-drop robotics simulator (Qt 6 / C++17 / MuJoCo 3.3.7, Linux). Users assemble components (servos, IMUs, brackets, wheels) from a library, program them with Arduino-style JavaScript (`loop()`, `delay()`), and watch live telemetry. Components emulate cheap, flawed hardware (backlash, noise).

## Build / run

```bash
./run.sh   # configure Debug into build/, build -j2, run with QT_QPA_PLATFORM=xcb
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build -j2   # build only
python3 tools/validate_rsdef.py [file-or-dir ...]   # validate .rsdef files (default: models/)
```

- There is no automated test suite. Verify by building, running the app, and running `validate_rsdef.py`.
- `CMakeLists.txt` uses `file(GLOB_RECURSE ...)` without `CONFIGURE_DEPENDS`: **re-run the CMake configure step after adding/removing source files**.
- MuJoCo (`extern/mujoco-3.3.7`) and QCustomPlot (`extern/qcustomplot`) are vendored; Qt6 (Xml, Widgets, OpenGL, OpenGLWidgets, Qml, PrintSupport, Network) and glfw come from the system. CI (`.github/workflows/build-appimage.yml`) builds a Release AppImage with Qt 6.10.2.
- Version is `APP_VERSION` in `CMakeLists.txt`, injected via `src/Application/Version.h.in`.

## Architecture

Layers under `src/`: **Document / Commands / View**, plus **Simulation** and **Telemetry**.

- `Document/` (`Project`, `ComponentData`, `ComponentBlueprint`, `ComponentInstance`, `LibraryManager`): no widgets. `LibraryManager` loads one `ComponentBlueprint` per model id from `models/Catalog.json`; the blueprint extends `ComponentData` and builds the kinematic graph and asset XML. `Project` parses `.rsproj` assemblies into a `ComponentInstance` tree and **generates one MuJoCo MJCF XML string** (`generateMujocoXML`) by asking each blueprint for tree/actuator/sensor/contact XML fragments.
- `Commands/`: `QUndoCommand`s. The View mutates authored document state **only** through commands (pushed on the undo stack owned by `Application`); commands mutate the document, then reload the simulation and refresh panels. See `docs/command.md` (partly outdated).
- `Simulation/`: `SimulationManager` runs a physics thread (`physicsLoop`, guarded by `physicsMutex`) and an MCU thread (`MicroController`, `QJSEngine` running the user's JS). `MujocoContext` wraps the model/data. `ErrorSystem/` emulators (`Emulator` QObject subclasses, one per device family) are injected into the JS engine as `comp_<uid>` globals; their `Q_INVOKABLE` methods are the scripting API. Physics loop: sync actuator values to MuJoCo → `mj_step` × N → sync sensors back → run emulators → telemetry capture. Edit mode only lerps `qpos` and calls `mj_forward` (no stepping).
- `Telemetry/`: `TelemetryRegistry` (string-keyed) with `TelemetrySource`s (`ComponentIOSource`, `BodyMotionSource`) and storage buffers; consumed by `View/Panels` (`PlotPanel`, `Canvas`) via QCustomPlot. Channels only record while subscribed. See `docs/telemetry.md` (partly outdated).
- `View/`: panels, viewports (MuJoCo rendering via OpenGL), windows (`LauncherWindow`, `EditorWindow`, `ComponentEditorWindow`).

Rules to preserve:
- `Document/` and `Simulation/` must not include `Application/Application.h`; inject dependencies instead.
- Per-instance runtime value maps in `ComponentInstance` (`joints`, `actuators`, `sensors` of `BasicIOValue`) are guarded by `ioMutex`; the physics, MCU and UI threads all touch them. Runtime writes from Simulation are not undoable.

## Component format (.rsdef, schema 2)

The `schema2` branch moved all components to schema 2; the loader (`ComponentData::fromJson`) rejects anything without `"schema": 2`. Layout: `id`, `meta`, `resources`, `specs`, `pins`, `kinematics` (bodies, joints, tendons), `connectors`, `devices` (actuators, sensors, cameras, displays as separate top-level lists, not nested in joints/sites), `interface` (inputs/outputs exposed to scripts), `emulator`.

- Formal schemas: `schema/rsdef.schema.json`, `schema/rsproj.schema.json`. JSON Schema cannot express cross-references (unique ids, target resolution, mass rule); those are enforced in `ComponentData::fromJson` and mirrored in `tools/validate_rsdef.py`. Keep the two in sync, along with the device table in `src/Document/Components/DeviceTypes.cpp`.
- Format reference: `docs/schema2.md`; real examples in `models/`. New components must be listed in `models/Catalog.json`.
- Frames: joint and connector transforms are in the **component root frame**; site and geom transforms are in the **parent body's local frame**. Body field is `overrideGeom` (camelCase); `mass`/`inertia` only when it is true.

## Notes

- `README.md` is the user-facing doc.
- Demos (`.rsproj` + JS) live in `demo/`.
