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
 * Power (Real level, the net on `vmot`): the driver chops the phase current
 * to rated_current_a, so the holding torque doesn't depend on the supply;
 * the pull-out corner does (back-EMF limits how fast the current rises), it
 * scales with supply / pullout_voltage. The driver draws the coils' power,
 * 2 x I^2 x R, from the supply (an ideal driver: no switching loss).
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
 *   rated_current_a, phase_resistance_ohm, pullout_voltage   (power)
 */
class StepperEmulatorCpp : public Emulator {
    Q_OBJECT

    private:
        Steps::StepperRotor rotor;
        int stepPin = -1, dirPin = -1;

        bool warnedInertia = false;
        double cornerAtRated = 31.4;     // rad/s at pullout_voltage
        double pulloutVoltage = 0.0;     // 0 = corner independent of the supply
        double coilPower = 0.0;          // W, both phases at the rated current
        double drawn = 0.0;

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
            rotor.cornerSpeed = cornerAtRated = std::max(1.0, param(p, "pullout_corner_rpm", 300.0)) * M_PI / 30.0;
            pulloutVoltage = param(p, "pullout_voltage", 0.0);
            const double phase = param(p, "rated_current_a", 0.0);
            coilPower = 2.0 * phase * phase * param(p, "phase_resistance_ohm", 0.0);
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
            if (supplyVolts >= 0.0 && pulloutVoltage > 0.0)
                rotor.cornerSpeed = std::max(1e-3, cornerAtRated * supplyVolts / pulloutVoltage);
            drawn = supplyVolts > 0.0 ? coilPower / supplyVolts : 0.0;
            const double theta = physicsData->qpos[physicsModel->jnt_qposadr[physicsModel->dof_jntid[dof]]];
            const double torque = rotor.torque(steps, theta, physicsData->qvel[dof], inertia, dt);
            EmulatorIO::writeScalar(component, "motor_torque", torque, true);
            EmulatorIO::writeScalar(component, "missed_steps", double(rotor.missedFullSteps), false);
        }

        double supplyCurrent() const override { return drawn; }

        // Without power the driver forgets its position: at power-on the rotor
        // snaps to the nearest equilibrium again.
        void powerCut() override {
            rotor.reset();
            reset();
        }

        void reset() override {
            drawn = 0.0;
            EmulatorIO::writeScalar(component, "motor_torque", 0.0, true);
            EmulatorIO::writeScalar(component, "missed_steps", 0.0, false);
        }
};
