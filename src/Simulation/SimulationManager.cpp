#include "SimulationManager.h"
#include "MujocoContext.h"

#include <chrono>
#include <functional>
#include <limits>
#include <qobject.h>
#include "Utils/Log.h"
#include "Document/Project.h"
#include "Document/Stage.h"
#include "Document/DesignRules.h"
#include "Document/Components/ComponentBlueprint.h"
#include "Document/Components/ComponentInstance.h"
#include "Document/Components/Units.h"
#include "Telemetry/TelemetryRegistry.h"
#include "Simulation/ErrorSystem/Emulator.h"
#include "Simulation/Firmware/FirmwareRuntime.h"
#include "Simulation/Rendering/CameraRenderer.h"
#include "Simulation/Rendering/DisplayTextures.h"
#include "mujoco/mjdata.h"





/*
 * Initializes the simulation manager and spawns the background physics thread.
 * Starts in a stopped state with a default 1.0x real-time scale.
 */
SimulationManager::SimulationManager(Project* project): project(project),
                                                        isAlive(true),
                                                        currentState(SimulationState::PAUSED),
                                                        timeScale(1.0f) {
    displayTextures = new DisplayTextures();
    cameraRenderer = new CameraRenderer(&mujocoContext, &physicsMutex, displayTextures);

    // Main viewport: upload display frames in the viewport's GL context. The
    // cache lives in the lambda; only the UI thread renders this context.
    mujocoContext.beforeRender = [this, cache = DisplayTextures::UploadCache()](const mjrContext* con) mutable {
        displayTextures->upload(con, cache);
    };
}





/*
 * Safely signals the physics thread to terminate and waits for it to join.
 * Ensures clean memory cleanup when the application closes.
 */
SimulationManager::~SimulationManager() {
    isAlive = false;

    project->getFirmware()->stop();
    project->getStageController()->stop();

    if (firmwareThread.joinable()) {
        firmwareThread.join();
    }
    if (stageThread.joinable()) {
        stageThread.join();
    }
    if (physicsThread.joinable()) {
        physicsThread.join();
    }

    // After the physics thread is gone: it is the only producer.
    delete cameraRenderer;
    cameraRenderer = nullptr;
    mujocoContext.beforeRender = nullptr;
    delete displayTextures;
    displayTextures = nullptr;
}





/*
 * Following functions are the public API for controlling the simulation state and time scale.
 */
#pragma region play/edit

void SimulationManager::startThreads() {
    if (currentState != SimulationState::PAUSED) return;
    if (!physicsThread.joinable()) {
        physicsThread = std::thread(&SimulationManager::physicsLoop, this);
    }
    if (!firmwareThread.joinable()) {
        firmwareThread = std::thread(&SimulationManager::mcuLoop, this, project->getFirmware());
    }
    if (!stageThread.joinable()) {
        stageThread = std::thread(&SimulationManager::mcuLoop, this, project->getStageController());
    }
}


void SimulationManager::play() {
    startThreads();

    // Between steps only: the schedule and the scripts change below.
    std::lock_guard<std::mutex> stepLock(stepMutex);

    ComponentInstance* root = project->getRootComponent();

    // The pins between the board and the parts (before the emulators look
    // them up), and the buses they register on. Prototype: the ideal board,
    // every part wired. Real: the active board through the project's nets.
    const bool real = project->getLevel() == ProjectLevel::Real;
    const RealBoard board = real ? project->realBoard() : RealBoard();
    unpoweredUids.clear();
    if (!real) {
        pinBus.build(project->idealBoard());
        deviceBus.reset(&pinBus);
    } else {
        if (board.isValid()) pinBus.build(board, project->netlist());
        else pinBus.clear();
        deviceBus.reset(&pinBus);
        const BoardDef& def = board.def();
        const int uid = board.isValid() ? board.instance()->uid : -1;
        auto handle = [&](const QString& id) { return id.isEmpty() || uid < 0 ? -1 : pinBus.handleOf(uid, id); };
        deviceBus.restrictTo(handle(def.i2cSda), handle(def.i2cScl), handle(def.spiSck), handle(def.spiMosi));

        // The wiring checks, in the Output panel (they don't stop the run).
        for (const RuleIssue& issue : DesignRules::check(*project)) {
            if (issue.isError()) Log::error("Wiring: " + issue.message);
            else Log::warning("Wiring: " + issue.message);
        }
        for (int dead : DesignRules::unpoweredParts(*project).keys()) unpoweredUids.insert(dead);
    }
    initEmulators(root);
    buildSchedule(root);
    startPower(root);

    // The robot runs its compiled firmware. A .js robot script from before
    // firmware is refused: physics runs, the robot stays idle (physics never
    // waits for an idle controller).
    robotController = project->getFirmware();
    if (real && !board.isValid()) {
        project->getFirmware()->stop();
        Log::warning("Real level without a board: the physics runs, nothing drives the parts. Add a board from the library.");
    } else if (project->usesFirmware()) {
        project->getFirmware()->load(project->getFirmwareBinary(), &pinBus, &deviceBus,
                                     real ? BoardProfile::fromBoard(board) : BoardProfile());
    } else {
        project->getFirmware()->stop();
        if (project->hasRobotScript())
            Log::error("The robot script is JavaScript (" + project->activeSketchPath() + "): JavaScript robot scripts were "
                       "removed. Port it to a firmware sketch (.ino), see docs/user/firmware.md. The robot stays idle.");
        else if (real)
            Log::warning(board.instance()->name + " has no firmware: pick a sketch in its Inspector. The parts stay idle.");
    }

    // Stage script: sees the controlled props and the robot pose, nothing else.
    Stage* stage = project->getStage();
    MicroController* stageMcu = project->getStageController();
    QString stageScript = project->readStageScript();
    if (stageScript.isEmpty()) {
        stageMcu->stop();
    } else {
        for (const auto& h : stage->getHandles()) {
            stageMcu->setGlobal("prop_" + h->id, h.get());
        }
        stageMcu->setGlobal("robot", stage->getRobotPose());
        stageMcu->compile(stageScript);
    }

    simBudget = 0.0;
    currentState = SimulationState::PLAYING;
}



void SimulationManager::edit() {
    startThreads();

    // Stop the scripts first: a step in progress may be waiting for one.
    project->getFirmware()->stop();
    project->getStageController()->stop();
    std::lock_guard<std::mutex> stepLock(stepMutex);

    MujocoContext* mj = &mujocoContext;
    for (const auto& h : project->getStage()->getHandles()) h->resetRuntime();

    ComponentInstance* root = project->getRootComponent();
    for (const QString& line : power.summary()) Log::info("Power: " + line);
    power.clear();
    offEmulators.clear();
    deadUids.clear();
    clearSchedule();
    resetEmulators(root);
    deviceBus.reset(&pinBus);
    pinBus.clear();
    unpoweredUids.clear();

    mj_resetData(mj->getModel(), mj->getData());
    mj->forward();

    currentState = SimulationState::EDITING;
}



// Scripts run only when a step releases them, so pausing the steps pauses
// them too (a pending delay() resumes where it left off).
void SimulationManager::pause() {
    if (currentState != SimulationState::PLAYING) return;
    currentState = SimulationState::PAUSED;
}



void SimulationManager::resume() {
    if (currentState != SimulationState::PAUSED) return;
    simBudget = 0.0;
    currentState = SimulationState::PLAYING;
}






SimulationState SimulationManager::getState() const {
    return currentState.load();
}







void SimulationManager::setTimeScale(float scale) {
    timeScale = scale;
}







void SimulationManager::trackFps() {
    if (!fpsTimer.isValid()) {
        fpsTimer.start();
    }

    frameCount++;

    if (fpsTimer.elapsed() >= 400) {
        int currentFps = static_cast<int>((frameCount*1000.0 / fpsTimer.elapsed()));
        // emit fpsUpdated(currentFps);
        status.fps=currentFps;
        double wallSec = fpsTimer.elapsed() / 1000.0;
        status.realTimeFactor = currentState == SimulationState::PLAYING
            ? std::max(0.0, (status.simTime - fpsWindowSimStart) / wallSec) : 0.0;
        status.timeScale = timeScale.load();
        fpsWindowSimStart = status.simTime;
        emit statusUpdated(status);

        frameCount = 0;
        fpsTimer.restart();
    }
}







#pragma region mcuLoop


// The controller runs only while a physics step releases it (lockstep, see
// LockstepGate); runCycle() blocks until then.
void SimulationManager::mcuLoop(RobotController* controller) {
    while (isAlive) {
        controller->runCycle();
    }
}









/*
 * The continuous background physics loop. Sim time is the only clock: wall
 * time only paces how many lockstep steps (stepOnce) run per iteration.
 */
#pragma region physicsLoop

void SimulationManager::physicsLoop() {
    lastTickTime = std::chrono::steady_clock::now();

    while (isAlive) {
        auto now = std::chrono::steady_clock::now();
        double dtSeconds = std::chrono::duration<double>(now - lastTickTime).count();
        lastTickTime = now;
        dtSeconds = std::min(dtSeconds, 0.25);  // Clamp so a debugger pause / OS hitch doesn't force a huge
                                                // catch-up burst of steps on the next tick.
        const float scale = timeScale.load();
        const bool unbounded = scale <= 0.0f;

        if (currentState == SimulationState::PLAYING) {
            MujocoContext* mj = &mujocoContext;
            double modelTimestep;
            {
                std::lock_guard<std::mutex> lock(physicsMutex);
                modelTimestep = mj->getModel()->opt.timestep;
            }

            // Real time: run the steps the elapsed wall time allows; a heavy
            // script makes us fall behind (the budget is capped) instead of
            // changing what it sees. Max: run steps for one frame's worth of
            // wall time.
            if (unbounded) simBudget = 0.0;
            else simBudget = std::min(simBudget + dtSeconds * scale, std::max(0.25 * scale, modelTimestep));

            const auto batchStart = std::chrono::steady_clock::now();
            const auto batchWall = std::chrono::milliseconds(unbounded ? 16 : 100);
            int steps = 0;
            while (isAlive && (unbounded || simBudget >= modelTimestep)) {
                if (std::chrono::steady_clock::now() - batchStart >= batchWall) break;
                if (!stepOnce()) break;
                if (!unbounded) simBudget -= modelTimestep;
                ++steps;
            }

            {
                std::lock_guard<std::mutex> lock(physicsMutex);
                mjData* d = mj->getData();
                if (steps > 0) displayTextures->collect();

                status.simTime=d->time;
                status.activeContacts=d->ncon;

                int invasiveCount = 0;
                const double PENETRATION_THRESHOLD = -0.002;
                for (int i = 0; i < d->ncon; ++i) {
                    if (d->contact[i].dist < PENETRATION_THRESHOLD) {
                        invasiveCount++;
                    }
                }
                status.invasiveCollisions = invasiveCount;
            }
        }

        else if (currentState == SimulationState::EDITING) {
            // In editing mode, we sync joint positions to allow dragging components
            std::lock_guard<std::mutex> lock(physicsMutex);

            MujocoContext* mj = &mujocoContext;
            ComponentInstance* root = project->getRootComponent();

            syncToMujocoJoint(root, mj->getModel(), mj->getData());
            mj->forward();

            syncFromMujocoSensor(root, mj->getModel(), mj->getData());
            displayTextures->collect();   // emulator resets blank the screens


            status.simTime=0;
            status.activeContacts=mj->getData()->ncon;
            int invasiveCount = 0;
            const double PENETRATION_THRESHOLD = -0.002;
            mjData* d = mj->getData();
            for (int i = 0; i < d->ncon; ++i) {
                if (d->contact[i].dist < PENETRATION_THRESHOLD) {
                    invasiveCount++;
                }
            }
            status.invasiveCollisions = invasiveCount;
        }

        trackFps();

        // Max mode keeps stepping; just let the UI thread at physicsMutex.
        if (currentState == SimulationState::PLAYING && unbounded) std::this_thread::yield();
        else std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}







// splitmix64: decorrelates per-instance seeds derived from one project seed.
static quint64 mixSeed(quint64 x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}


void SimulationManager::initEmulators(ComponentInstance* comp) {
    if (!comp) return;
    if (comp->emulator && !unpoweredUids.contains(comp->uid)) {
        quint64 projectSeed = quint64(project->getSimulationSeed());
        comp->emulator->setSeed(mixSeed(mixSeed(projectSeed) ^ quint64(comp->uid)));
        comp->emulator->setPins(&pinBus);
        comp->emulator->setBuses(&deviceBus);
        comp->emulator->setSupply(-1.0);   // ideal until the power model says otherwise
        comp->emulator->init();
    }
    for (ComponentInstance* child : comp->children) {
        initEmulators(child);
    }
}



/*
 * Flattens the component tree into the per-cadence emulator lists (pre-order,
 * so synchronous emulators keep the order the old recursive pass had).
 * Throttled/async deadlines start at the current sim time: the first update or
 * sample happens on the first tick. Periods shorter than one physics step are
 * clamped to one step.
 */
void SimulationManager::buildSchedule(ComponentInstance* root) {
    std::lock_guard<std::mutex> lock(physicsMutex);

    syncEmulators.clear();
    throttledEmulators.clear();
    asyncEmulators.clear();

    mjModel* m = mujocoContext.getModel();
    mjData* d = mujocoContext.getData();
    if (!m || !d) return;

    collectEmulators(root, d->time, m->opt.timestep);

    nextAsyncDue = d->time;
    nextTelemetryDue = d->time;
}


void SimulationManager::clearSchedule() {
    std::lock_guard<std::mutex> lock(physicsMutex);
    syncEmulators.clear();
    throttledEmulators.clear();
    asyncEmulators.clear();
}


void SimulationManager::collectEmulators(ComponentInstance* comp, double now, double minPeriod) {
    if (!comp) return;
    if (Emulator* e = unpoweredUids.contains(comp->uid) ? nullptr : comp->emulator) {
        Schedule s = e->schedule();
        if (s.cadence == Cadence::Synchronous) {
            syncEmulators.push_back(e);
        } else {
            double period = s.periodSec;
            if (period < minPeriod) {
                Log::warning(QString("SimulationManager: component %1 (%2): emulator period %3 s is shorter than one physics step; using %4 s")
                                .arg(comp->uid).arg(comp->modelId).arg(period).arg(minPeriod));
                period = minPeriod;
            }
            ScheduledEmulator entry{e, period, now};
            if (s.cadence == Cadence::Throttled) throttledEmulators.push_back(entry);
            else                                 asyncEmulators.push_back(entry);
        }
    }
    for (ComponentInstance* child : comp->children) {
        collectEmulators(child, now, minPeriod);
    }
}


// Next deadline after `now`, keeping the phase; if the loop fell more than a
// period behind (a hitch), skip the missed samples instead of bursting.
static double advanceDeadline(double due, double period, double now) {
    due += period;
    if (due <= now) due = now + period;
    return due;
}


/*
 * Called from inside the step loop (physicsMutex held) once sim time reaches
 * nextAsyncDue: samples every async emulator that is due, then recomputes the
 * earliest deadline.
 */
void SimulationManager::runDueAsync(double now) {
    SampleContext ctx;
    ctx.m = mujocoContext.getModel();
    ctx.d = mujocoContext.getData();
    ctx.simTime = now;
    ctx.cameraRenderer = cameraRenderer;

    double earliest = std::numeric_limits<double>::infinity();
    for (ScheduledEmulator& s : asyncEmulators) {
        if (now >= s.nextDue) {
            if (!offEmulators.contains(s.emulator)) s.emulator->sample(ctx);
            s.nextDue = advanceDeadline(s.nextDue, s.periodSec, now);
        }
        earliest = std::min(earliest, s.nextDue);
    }
    nextAsyncDue = earliest;
}


/*
 * Real level, at Play (under stepMutex, after the emulators' init): the power
 * network for this run. Parts that start without enough supply start off.
 */
void SimulationManager::startPower(ComponentInstance* root) {
    deadUids = unpoweredUids;
    offEmulators.clear();
    powerCutsLogged.clear();
    QHash<int, Emulator*> emulators;
    std::function<void(ComponentInstance*)> collect = [&](ComponentInstance* comp) {
        if (!comp) return;
        if (comp->emulator && !unpoweredUids.contains(comp->uid)) emulators.insert(comp->uid, comp->emulator);
        for (ComponentInstance* child : comp->children) collect(child);
    };
    collect(root);
    power.build(*project, emulators);
    if (!power.isActive()) return;

    for (const PowerSystem::Part& p : power.parts()) {
        if (unpoweredUids.contains(p.uid)) continue;
        if (p.emulator) p.emulator->setSupply(p.volts);
        if (!p.on) {
            setPartPowered(project->getComponentByUid(p.uid), false);
            Log::warning(QString("Power: %1 starts without enough supply (%2 V, it needs %3 V): it stays off")
                             .arg(p.name).arg(p.volts, 0, 'f', 2).arg(p.minV, 0, 'f', 2));
        }
    }
}


// A part loses or regains its supply during the run.
void SimulationManager::setPartPowered(ComponentInstance* comp, bool powered, double now) {
    if (!comp || !comp->emulator) return;
    Emulator* e = comp->emulator;
    auto devices = [&](bool on) {
        if (auto* d = dynamic_cast<I2CDevice*>(e)) deviceBus.setPowered(d, on);
        if (auto* d = dynamic_cast<SPIDevice*>(e)) deviceBus.setPowered(d, on);
        if (auto* d = dynamic_cast<CameraDevice*>(e)) deviceBus.setPowered(d, on);
    };
    if (!powered) {
        e->powerCut();
        offEmulators.insert(e);
        deadUids.insert(comp->uid);
        devices(false);
    } else {
        offEmulators.remove(e);
        if (!unpoweredUids.contains(comp->uid)) deadUids.remove(comp->uid);
        devices(true);
        e->powerRestored(now);
    }
}


/*
 * Real level, once per physics tick after the emulators (physicsMutex held):
 * solve the power network with this step's currents, hand every part its
 * supply for the next step, switch parts off/on, and hold or release the
 * board in reset (brownout).
 */
void SimulationManager::applyPower(double now, double dt) {
    static constexpr int kCutsLogged = 5;   // per part, then only the summary at Stop
    const PowerSystem::Events ev = power.step(now, dt);
    for (const PowerSystem::Part& p : power.parts()) {
        if (p.emulator) p.emulator->setSupply(p.volts);
    }
    for (int uid : ev.switchedOff) {
        ComponentInstance* comp = project->getComponentByUid(uid);
        setPartPowered(comp, false);
        const int n = ++powerCutsLogged[uid];
        if (n <= kCutsLogged && comp)
            Log::warning(QString("Power: %1 lost its supply at %2 s (%3 V)%4").arg(comp->name).arg(now, 0, 'f', 3)
                             .arg(power.supplyOf(uid), 0, 'f', 2).arg(n == kCutsLogged ? "; further cuts of this part not logged" : ""));
    }
    for (int uid : ev.switchedOn) setPartPowered(project->getComponentByUid(uid), true, now);

    // Only the active board runs firmware; idle boards only count their resets.
    ComponentInstance* activeBoard = project->getLevel() == ProjectLevel::Real ? project->activeBoard() : nullptr;
    FirmwareRuntime* fw = project->getFirmware();
    for (int uid : ev.brownedOut) {
        ComponentInstance* board = project->getComponentByUid(uid);
        const QString name = board ? board->name : QString::number(uid);
        Log::warning(QString("MCU: %1 brownout reset at %2 s (%3 V on its supply rail)")
                         .arg(name).arg(now, 0, 'f', 3).arg(power.boardRailVolts(uid), 0, 'f', 2));
        if (board && board == activeBoard && robotController == fw && fw->holdInReset()) pinBus.releaseBoardPins();
    }
    for (int uid : ev.released) {
        if (activeBoard && activeBoard->uid == uid && robotController == fw && fw->isHeld() && fw->restart(now))
            Log::info(QString("MCU: %1 starts again at %2 s (setup())").arg(activeBoard->name).arg(now, 0, 'f', 3));
    }
}



/*
 * Once per physics tick, after the sensor sync (physicsMutex held):
 * synchronous emulators every tick, throttled ones when due, async ones every
 * tick so they can publish what their worker finished.
 */
void SimulationManager::runEmulators(double now) {
    const mjModel* m = mujocoContext.getModel();
    const mjData* d = mujocoContext.getData();
    for (Emulator* e : syncEmulators) {
        if (offEmulators.contains(e)) continue;
        e->setSimTime(now);
        e->setPhysics(m, d);
        e->update();
    }
    for (ScheduledEmulator& s : throttledEmulators) {
        if (now >= s.nextDue) {
            s.emulator->setSimTime(now);
            if (!offEmulators.contains(s.emulator)) s.emulator->update();
            s.nextDue = advanceDeadline(s.nextDue, s.periodSec, now);
        }
    }
    for (ScheduledEmulator& s : asyncEmulators) {
        if (offEmulators.contains(s.emulator)) continue;
        s.emulator->setSimTime(now);
        s.emulator->update();
    }
}



/*
 * One lockstep step [t, t + dt):
 *   1. the scripts, in a fixed order (robot, then stage), see the state at t
 *      and run until they yield past this step;
 *   2. async workers deliver everything due by t + dt (sim time decides what
 *      the script sees, never the host's speed);
 *   3. under physicsMutex: inputs -> mj_step -> async samples -> sensors ->
 *      emulators -> telemetry (on its own sim-time period).
 * 1 and 2 run without physicsMutex, so the viewport can render meanwhile and
 * the camera worker can rebuild its context.
 */
bool SimulationManager::stepOnce() {
    std::lock_guard<std::mutex> stepLock(stepMutex);
    if (!isAlive || currentState != SimulationState::PLAYING) return false;

    MujocoContext* mj = &mujocoContext;
    double t, dt;
    {
        std::lock_guard<std::mutex> lock(physicsMutex);
        if (!mj->getModel() || !mj->getData()) return false;
        t = mj->getData()->time;
        dt = mj->getModel()->opt.timestep;
    }

    if (robotController) robotController->runStep(t, dt);
    project->getStageController()->runStep(t, dt);

    for (ScheduledEmulator& s : asyncEmulators) s.emulator->awaitDue(t + dt);

    std::lock_guard<std::mutex> lock(physicsMutex);
    mjModel* m = mj->getModel();
    mjData* d = mj->getData();
    ComponentInstance* root = project->getRootComponent();

    syncToMujocoActuator(root, m, d, deadUids);
    syncToMujocoProps(project->getStage(), m, d);
    mj->step();
    // Async devices sample at the exact step that reaches their deadline.
    if (!asyncEmulators.empty() && d->time >= nextAsyncDue) {
        runDueAsync(d->time);
    }
    syncFromMujocoSensor(root, m, d);
    syncFromMujocoProps(project->getStage(), m, d);

    runEmulators(d->time); // position it here to capture both sensor and actuator properly
    if (power.isActive()) applyPower(d->time, m->opt.timestep);

    if (d->time >= nextTelemetryDue - 1e-9) {
        TelemetryRegistry::getInstance().captureAll(d->time);
        nextTelemetryDue = advanceDeadline(nextTelemetryDue, kTelemetryPeriod, d->time);
    }
    return true;
}



/*
 * Swaps the model in under physicsMutex and re-resolves MuJoCo ids. The
 * camera renderer is told first, while the old model is still alive, so it
 * drops queued frames and rebuilds its GPU context for the new model.
 */
bool SimulationManager::reloadModel(const QString& xml, ComponentInstance* root) {
    std::lock_guard<std::mutex> lock(physicsMutex);

    if (cameraRenderer) cameraRenderer->invalidate();
    // Fog is a render flag, not MJCF; set before the load so the rebuilt
    // scene (and every camera scene) picks it up.
    mujocoContext.setSceneFlag(mjRND_FOG, project->getStage()->getData().env.fog);
    bool ok = mujocoContext.loadModelFromString(xml.toStdString());
    cacheMujocoIds(root, mujocoContext.getModel());
    cacheStageIds(project->getStage(), mujocoContext.getModel());
    displayTextures->rebuild(root, mujocoContext.getModel());
    return ok;
}




// void SimulationManager::pushTelemetry(ComponentInstance* comp, double time) {
//     if (!comp || !comp->blueprint) return;

//     auto& registry = TelemetryRegistry::getInstance();

//     for (const QString& key : comp->blueprint->inputDefs.keys()) {
//         int channelId = comp->getActuatorChannelId(key);
//         if (channelId != -1) {
//             IOData data = comp->getActuatorTarget(key);
//             QString cType = comp->blueprint->inputDefs[key].channelType;

//             if (cType == "vector" && std::holds_alternative<std::vector<double>>(data)) {
//                 if (auto channel = registry.getVector(channelId)) {
//                     channel->push(time, std::get<std::vector<double>>(data));
//                 }
//             } else if (std::holds_alternative<double>(data)) {
//                 if (auto channel = registry.getScalar(channelId)) {
//                     channel->push(time, std::get<double>(data));
//                 }
//             }
//         }
//     }

//     for (const QString& key : comp->blueprint->outputDefs.keys()) {
//         int channelId = comp->getSensorChannelId(key);
//         if (channelId != -1) {
//             IOData data = comp->getSensorCurrent(key);
//             QString cType = comp->blueprint->outputDefs[key].channelType;

//             if (cType == "vector" && std::holds_alternative<std::vector<double>>(data)) {
//                 if (auto channel = registry.getVector(channelId)) {
//                     channel->push(time, std::get<std::vector<double>>(data));
//                 }
//             } else if (std::holds_alternative<double>(data)) {
//                 if (auto channel = registry.getScalar(channelId)) {
//                     channel->push(time, std::get<double>(data));
//                 }
//             }
//         }
//     }

//     for (ComponentInstance* child : comp->children) {
//         pushTelemetry(child, time);
//     }
// }




void SimulationManager::resetEmulators(ComponentInstance* comp) {
    if (!comp) return;
    if (comp->emulator) {
        comp->emulator->reset();
    }
    for (ComponentInstance* child : comp->children) {
        resetEmulators(child);
    }
}








#pragma region mujoco stuff



/*
 * One-time setup to map signals to MuJoCo's internal C-array indices.
 * Signal -> device -> MJCF name comp_<uid>_<deviceId> (D9). Traverses the
 * component tree recursively.
 *
 *   input  kind joint    : joint id from the joint name; actuator id from the first
 *                          actuator targeting that joint (none: logged, no ctrl written)
 *   input  kind actuator : actuator id from the actuator name
 *   output kind sensor   : sensor id from the sensor name (dimension cross-checked
 *                          against the model once, here)
 *   output kind camera   : camera id from the camera name (into the ImageIOValue)
 *   emulator / display kinds have no MuJoCo id and are left at -1.
 */
void SimulationManager::cacheMujocoIds(ComponentInstance* root, mjModel* m) {
    if (!m || !root) return;

    QString prefix = "comp_" + QString::number(root->uid) + "_";

    if (ComponentBlueprint* bp = root->getBlueprint()) {
        for (auto it = bp->interfaceInputs.constBegin(); it != bp->interfaceInputs.constEnd(); ++it) {
            const QString& key = it.key();
            const InterfaceDef& def = it.value();

            QString actuatorId = bp->actuatorForInput(def);
            if (def.target.kind == "joint") {
                int jointMjId = mj_name2id(m, mjOBJ_JOINT, (prefix + def.target.id).toStdString().c_str());
                BasicIOValue jvalue = root->getJointValue(def.target.id);
                jvalue.mujocoId = jointMjId;
                root->setJointValue(def.target.id, jvalue);

                // Fine if an actuator-kind input drives this joint's actuator
                // instead (a servo's motor torque); the joint input then only poses it.
                bool drivenElsewhere = false;
                for (const ActuatorDef& a : bp->actuators) {
                    if (a.target.kind != "joint" || a.target.id != def.target.id) continue;
                    for (const InterfaceDef& other : bp->interfaceInputs) {
                        if (other.target.kind == "actuator" && other.target.id == a.id) drivenElsewhere = true;
                    }
                }
                if (actuatorId.isEmpty() && !drivenElsewhere) {
                    Log::warning(QString("SimulationManager: component %1 (%2): input '%3' targets joint '%4' which has no position/velocity actuator; commands will not drive it")
                                    .arg(root->uid).arg(root->modelId, key, def.target.id));
                }
            }

            // -1 when nothing to drive, so a stale id from an earlier model can't write ctrl.
            BasicIOValue value = root->getActuatorValue(key);
            if (!value.data.empty()) {
                value.mujocoId = actuatorId.isEmpty() ? -1
                    : mj_name2id(m, mjOBJ_ACTUATOR, (prefix + actuatorId).toStdString().c_str());
                root->setActuatorValue(key, value);
            }
        }

        for (auto it = bp->interfaceOutputs.constBegin(); it != bp->interfaceOutputs.constEnd(); ++it) {
            const QString& key = it.key();
            const InterfaceDef& def = it.value();

            if (def.target.kind == "camera") {
                ImageIOValue img = root->getImageValue(key);
                img.mujocoId = mj_name2id(m, mjOBJ_CAMERA, (prefix + def.target.id).toStdString().c_str());
                if (img.mujocoId < 0) {
                    Log::error(QString("SimulationManager: component %1 (%2): camera '%3' not found in the MuJoCo model; signal '%4' disabled")
                                    .arg(root->uid).arg(root->modelId, def.target.id, key));
                }
                root->setImageValue(key, img);
                continue;
            }
            if (def.target.kind != "sensor") continue;

            int id = mj_name2id(m, mjOBJ_SENSOR, (prefix + def.target.id).toStdString().c_str());
            int dim = bp->interfaceDim(def);
            if (id >= 0 && m->sensor_dim[id] != dim) {
                Log::error(QString("SimulationManager: component %1 (%2): sensor '%3' has dimension %4 in MuJoCo but %5 in the device table; signal '%6' disabled")
                                .arg(root->uid).arg(root->modelId, def.target.id).arg(m->sensor_dim[id]).arg(dim).arg(key));
                id = -1;
            }
            BasicIOValue value = root->getSensorValue(key);
            value.mujocoId = id;
            root->setSensorValue(key, value);
        }
    }

    for (ComponentInstance* child : root->children) {
        cacheMujocoIds(child, m);
    }
}




/*
 * Pre-step hook to push live input commands into the engine.
 * Writes d->ctrl for every input signal that resolved to an actuator.
 * The unit conversion comes from the interface signal.
 */
void SimulationManager::syncToMujocoActuator(ComponentInstance* root, mjModel* m, mjData* d, const QSet<int>& dead) {
    if (!m || !d || !root) return;

    if (ComponentBlueprint* bp = root->getBlueprint()) {
        for (auto it = bp->interfaceInputs.constBegin(); it != bp->interfaceInputs.constEnd(); ++it) {
            BasicIOValue data = root->getActuatorValue(it.key());
            // Signals without a runtime slot (display/camera) come back as a default value whose
            // mujocoId is 0, not -1 -- skip them or they would overwrite ctrl[0].
            if (data.data.empty()) continue;
            int mujocoId = data.mujocoId;
            if (mujocoId >= 0 && mujocoId < m->nu) {

                double val = data.dim == 1 ? data.data[0] : 0.0;
                if (dead.contains(root->uid)) val = 0.0;   // a dead part drives nothing

                val *= Units::toMujoco(it.value().unit);

                d->ctrl[mujocoId] = val;
            }
        }
    }

    for (ComponentInstance* child : root->children) {
        syncToMujocoActuator(child, m, d, dead);
    }
}






/*
 * Edit-mode posing: lerps qpos of the joint each kind-joint input targets.
 */
void SimulationManager::syncToMujocoJoint(ComponentInstance* root, mjModel* m, mjData* d) {
    if (!m || !d || !root) return;

    if (ComponentBlueprint* bp = root->getBlueprint()) {
        for (auto it = bp->interfaceInputs.constBegin(); it != bp->interfaceInputs.constEnd(); ++it) {
            const InterfaceDef& def = it.value();
            if (def.target.kind != "joint") continue;

            BasicIOValue data = root->getJointValue(def.target.id);
            if (data.data.empty()) continue;
            int mujocoId = data.mujocoId;

            if (mujocoId >= 0 && mujocoId < m->njnt) {

                double targetVal = data.dim == 1 ? data.data[0] : 0.0;

                targetVal *= Units::toMujoco(def.unit);

                int qposIndex = m->jnt_qposadr[mujocoId];
                double currentPos = d->qpos[qposIndex];

                double smoothSpeed = 0.3;
                if (std::abs(targetVal - currentPos) > 0.0001) {
                    double newPos = currentPos + (targetVal - currentPos) * smoothSpeed;
                    d->qpos[qposIndex] = newPos;
                }
            }
        }
    }

    for (ComponentInstance* child : root->children) {
        syncToMujocoJoint(child, m, d);
    }
}





/*
 * Post-step hook to pull live physics telemetry out of the engine.
 * Reads each output signal that resolved to a sensor; the dimension comes
 * from the device type (checked against sensor_dim in cacheMujocoIds) and the
 * unit conversion from the interface signal.
 */
void SimulationManager::syncFromMujocoSensor(ComponentInstance* root, mjModel* m, mjData* d) {
    if (!m || !d || !root) return;

    if (ComponentBlueprint* bp = root->getBlueprint()) {
        for (auto it = bp->interfaceOutputs.constBegin(); it != bp->interfaceOutputs.constEnd(); ++it) {
            const QString& key = it.key();
            const InterfaceDef& def = it.value();

            BasicIOValue data = root->getSensorValue(key);
            if (data.data.empty()) continue;   // no runtime slot (camera); see syncToMujocoActuator
            int sensorId = data.mujocoId;

            if (sensorId >= 0 && sensorId < m->nsensor) {
                int adr = m->sensor_adr[sensorId];
                int dim = bp->interfaceDim(def);

                if (dim > 0 && adr + dim <= m->nsensordata) {
                    const double fromMujoco = 1.0 / Units::toMujoco(def.unit);

                    if (dim > 1 || bp->interfaceShape(def) == SignalShape::Vector) {
                        std::vector<double> vec(dim);
                        for (int i = 0; i < dim; ++i) {
                            double val = d->sensordata[adr + i];
                            val *= fromMujoco;
                            vec[i] = val;
                        }
                        data.data = vec;
                        root->setSensorValue(key, data);
                    } else {
                        double val = d->sensordata[adr];
                        val *= fromMujoco;

                        data.data[0] = val;
                        root->setSensorValue(key, data);
                    }
                }
            }
        }
    }

    for (ComponentInstance* child : root->children) {
        syncFromMujocoSensor(child, m, d);
    }
}





#pragma region stage



/*
 * Resolves the controlled props' joints/actuators and the robot root body.
 * Names that are absent (edit mode: no joints/actuators, no robot freejoint)
 * leave the ids at -1 and the syncs skip them.
 */
void SimulationManager::cacheStageIds(Stage* stage, mjModel* m) {
    if (!stage || !m) return;

    for (const auto& h : stage->getHandles()) {
        auto id = [&](mjtObj type, const QString& suffix) {
            return mj_name2id(m, type, ("prop_" + h->id + suffix).toStdString().c_str());
        };
        h->bodyId = id(mjOBJ_BODY, "");
        int jx = id(mjOBJ_JOINT, "_x");
        h->qposAdr = jx >= 0 ? m->jnt_qposadr[jx] : -1;
        h->dofAdr  = jx >= 0 ? m->jnt_dofadr[jx] : -1;
        h->actX    = id(mjOBJ_ACTUATOR, "_vx");
        h->actY    = id(mjOBJ_ACTUATOR, "_vy");
        h->actYaw  = id(mjOBJ_ACTUATOR, "_vyaw");
    }

    int freeJoint = mj_name2id(m, mjOBJ_JOINT, "robot_freejoint");
    stage->getRobotPose()->bodyId = freeJoint >= 0 ? m->jnt_bodyid[freeJoint] : -1;
}



/*
 * Pre-step: script commands -> velocity servo targets. Local (move) commands
 * are turned into world velocities with the prop's current yaw. Teleports
 * write the joint positions directly and zero the prop's velocity.
 */
void SimulationManager::syncToMujocoProps(Stage* stage, mjModel* m, mjData* d) {
    if (!stage || !m || !d) return;

    for (const auto& h : stage->getHandles()) {
        if (h->qposAdr < 0 || h->actX < 0) continue;
        PropHandle::Command c = h->takeCommand();
        double* q = d->qpos + h->qposAdr;   // x, y, z, yaw

        if (c.teleport) {
            q[0] = c.tp[0] - h->basePos[0];
            q[1] = c.tp[1] - h->basePos[1];
            q[2] = c.tp[2] - h->basePos[2];
            q[3] = (c.tp[3] - h->baseYaw) * M_PI / 180.0;
            for (int i = 0; i < 4; ++i) d->qvel[h->dofAdr + i] = 0;
        }

        double vx = c.vx, vy = c.vy;
        if (c.local) {
            double yaw = h->baseYaw * M_PI / 180.0 + q[3];
            vx = c.vx * std::cos(yaw) - c.vy * std::sin(yaw);
            vy = c.vx * std::sin(yaw) + c.vy * std::cos(yaw);
        }
        d->ctrl[h->actX] = vx;
        d->ctrl[h->actY] = vy;
        d->ctrl[h->actYaw] = c.yawRate * M_PI / 180.0;
    }
}



// Post-step: publish prop state and the robot pose to the stage script.
void SimulationManager::syncFromMujocoProps(Stage* stage, mjModel* m, mjData* d) {
    if (!stage || !m || !d) return;

    for (const auto& h : stage->getHandles()) {
        if (h->bodyId < 0 || h->qposAdr < 0) continue;
        const double* p = d->xpos + 3 * h->bodyId;
        const double* v = d->qvel + h->dofAdr;
        h->publish({p[0], p[1], p[2]}, {v[0], v[1], v[2]},
                   h->baseYaw + d->qpos[h->qposAdr + 3] * 180.0 / M_PI);
    }

    RobotPose* robot = stage->getRobotPose();
    if (robot->bodyId >= 0) {
        const double* p = d->xpos + 3 * robot->bodyId;
        const double* q = d->xquat + 4 * robot->bodyId;
        Rotation r(q[0], q[1], q[2], q[3]);
        robot->publish({p[0], p[1], p[2]}, {r.roll(), r.pitch(), r.yaw()});
    }
}
