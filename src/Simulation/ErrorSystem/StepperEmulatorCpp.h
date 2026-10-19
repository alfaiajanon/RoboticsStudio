#pragma once

#include "Emulator.h"
#include "EmulatorIO.h"
#include "Steps.h"
#include "Document/Components/ComponentInstance.h"
#include "Document/Components/ComponentBlueprint.h"
#include "Simulation/Controller/PinBus.h"
#include "Utils/Log.h"
#include <algorithm>
#include <cmath>

/*
 * Hybrid stepper behind an A4988-style driver (ideal and built in at the
 * Prototype level, always enabled), in the order the signal takes:
 *
 *   step, dir      each rising edge on STEP moves the commanded angle one
 *                  (micro)step: step_angle_deg / microstepping, in DIR's
 *                  direction (HIGH = positive).
 *   rotor          Steps::StepperRotor: the real torque-angle curve
 *                  (sinusoid, holding torque one full step off the
 *                  equilibrium, repeating every 4 full steps), pull-out
 *                  torque falling with speed, driver damping. A rotor pushed
 *                  more than 2 full steps behind falls into the next
 *                  equilibrium: 4 full steps lost for good. At power-on it
 *                  snaps to the nearest equilibrium.
 *
 * The real curve is stiff: the part's armature must give at least
 * StepperRotor::minInertia() at the physics step (warned once otherwise).
 *
 * No encoder: firmware counts its own steps. Plots see current_position and
 * missed_steps (simulation-only).
 *
 * Signals: in motor_torque (written here); out current_position (rad),
 * current_velocity (rad/s), missed_steps (emulator, full steps lost; positive
 * = behind the command).
 *
 * Parameters (component .rsdef -> emulator.parameters), read once in init():
 *   holding_torque_nm, step_angle_deg, microstepping, pullout_corner_rpm
 */
class StepperEmulatorCpp : public Emulator {
    Q_OBJECT

    private:
        Steps::StepperRotor rotor;
        int stepPin = -1, dirPin = -1;

        bool warnedInertia = false;

        // MuJoCo index of the driven joint's degree of freedom, from the
        // motor actuator; -1 if not resolved.
        int drivenDof() const {
            if (!physicsModel) return -1;
            const int act = component->getActuatorValue("motor_torque").mujocoId;
            if (act < 0 || act >= physicsModel->nu) return -1;
            const int joint = physicsModel->actuator_trnid[2 * act];
            return joint >= 0 ? physicsModel->jnt_dofadr[joint] : -1;
        }

    public:
        using Emulator::Emulator;

        void init() override {
            using EmulatorIO::param;
            ComponentBlueprint* bp = component ? component->getBlueprint() : nullptr;
            QMap<QString, QVariant> p = bp ? bp->emulatorDef.parameters : QMap<QString, QVariant>{};
            rotor.holdingTorque = param(p, "holding_torque_nm", 0.45);
            rotor.fullStep = std::max(0.01, param(p, "step_angle_deg", 1.8)) * M_PI / 180.0;
            rotor.microsteps = std::max(1, int(param(p, "microstepping", 1)));
            rotor.cornerSpeed = std::max(1.0, param(p, "pullout_corner_rpm", 300.0)) * M_PI / 30.0;
            rotor.reset();

            stepPin = pins ? pins->ensurePin(component->uid, "step", "digital") : -1;
            dirPin = pins ? pins->ensurePin(component->uid, "dir", "digital") : -1;
            warnedInertia = false;
        }

        void update() override {
            const int dof = drivenDof();
            if (dof < 0 || !physicsData) return;

            // Steps since the last physics step: STEP edges in DIR's direction.
            long steps = 0;
            if (pins) {
                const int edges = pins->takeRisingEdges(stepPin);
                const PinState* dir = pins->state(dirPin);
                const bool forward = dir && dir->drive == PinState::Drive::Level && dir->level == 1;
                steps += forward ? edges : -edges;
            }

            const double inertia = physicsData->qM[physicsModel->dof_Madr[dof]];
            const double dt = physicsModel->opt.timestep;
            if (!warnedInertia && inertia < rotor.minInertia(dt)) {
                warnedInertia = true;
                Log::warning(QString("Stepper %1: driven inertia %2 kg·m² is below %3 needed for a stable torque curve at "
                                     "a %4 ms physics step; raise the joint's armature")
                                 .arg(component->uid).arg(inertia, 0, 'g', 3).arg(rotor.minInertia(dt), 0, 'g', 3).arg(dt * 1000));
            }
            const double theta = physicsData->qpos[physicsModel->jnt_qposadr[physicsModel->dof_jntid[dof]]];
            const double torque = rotor.torque(steps, theta, physicsData->qvel[dof], inertia, dt);
            EmulatorIO::writeScalar(component, "motor_torque", torque, true);
            EmulatorIO::writeScalar(component, "missed_steps", double(rotor.missedFullSteps), false);
        }

        void reset() override {
            EmulatorIO::writeScalar(component, "motor_torque", 0.0, true);
            EmulatorIO::writeScalar(component, "missed_steps", 0.0, false);
        }
};
