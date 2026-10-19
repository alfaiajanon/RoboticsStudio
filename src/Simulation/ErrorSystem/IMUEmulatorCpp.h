#pragma once

#include "Simulation/ErrorSystem/Emulator.h"
#include "EmulatorIO.h"
#include "Steps.h"
#include "Document/Components/ComponentInstance.h"
#include "Document/Components/ComponentBlueprint.h"
#include "Simulation/Controller/DeviceBus.h"
#include "Simulation/Controller/PinBus.h"
#include <QJsonObject>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

/*
 * InvenSense MPU-6050 on the board's I2C bus (0x68, 0x69 with AD0 high),
 * with its register map and its measurement chain, per axis in the order the
 * signal takes through the chip:
 *
 *   true value     specific force and angular rate at the part's site
 *                  (MuJoCo accelerometer / gyro: the `acceleration`,
 *                  `gyroscope` outputs, simulation-only)
 *   scale + bias   per-unit sensitivity error and zero offset (datasheet
 *                  tolerances), plus a slow random walk (bias drift)
 *   noise          white noise at the ADC from the datasheet noise density
 *   DLPF           first-order low-pass at the configured bandwidth (CONFIG),
 *                  so the noise reaching the output is density x sqrt(BW)
 *   sample + hold  data registers update at the output data rate:
 *                  (8 kHz with DLPF off, else 1 kHz) / (1 + SMPLRT_DIV),
 *                  at most once per physics step
 *   range + LSB    clipped at the full-scale range (ACCEL_CONFIG,
 *                  GYRO_CONFIG) and quantised to its LSB, as int16
 *
 * Registers (subset): SMPLRT_DIV 0x19, CONFIG 0x1A, GYRO_CONFIG 0x1B,
 * ACCEL_CONFIG 0x1C, data 0x3B-0x48 (accel XYZ, temp, gyro XYZ; big-endian),
 * PWR_MGMT_1 0x6B (powers up asleep: 0x40; bit 7 resets), WHO_AM_I 0x75 =
 * 0x68. Other registers keep what is written and read 0 by default. The
 * register pointer auto-increments. Asleep, the data registers read 0.
 * TEMP_OUT is a constant 25 °C.
 *
 * Signals: out acceleration (m/s², truth), gyroscope (°/s, truth),
 * accel_reading (g) and gyro_reading (°/s), what the chip reports (plots).
 *
 * Parameters (component .rsdef -> emulator.parameters), read once in init():
 *   accel_noise_density_ug     µg/sqrt(Hz)
 *   gyro_noise_density_dps     °/s/sqrt(Hz)
 *   accel_bias_spread_mg       per-unit zero-g offset (stddev)
 *   gyro_bias_spread_dps       per-unit zero-rate offset (stddev)
 *   accel_bias_walk_mg         bias random walk, mg/sqrt(s)
 *   gyro_bias_walk_dps         bias random walk, °/s/sqrt(s)
 *   scale_spread               per-unit sensitivity error (stddev, fraction)
 *   ad0_high                   1: address 0x69
 */
class IMUEmulatorCpp : public Emulator, public I2CDevice {
    Q_OBJECT

    private:
        static constexpr double kG = 9.80665;
        static constexpr double kAccelBandwidth[7] = {260, 184, 94, 44, 21, 10, 5};   // Hz, DLPF_CFG 0..6
        static constexpr double kGyroBandwidth[7] = {256, 188, 98, 42, 20, 10, 5};

        struct Axis {
            double scale = 1.0;
            Steps::UnitBias bias;
            Steps::NoiseDensity noise;
            Steps::FirstOrderLowPass dlpf;
        };

        std::array<uint8_t, 128> regs{};
        uint8_t pointer = 0;
        int address = 0x68;

        Steps::Rng rng;
        std::array<Axis, 3> accel, gyro;
        double accelDensityG = 400e-6, gyroDensity = 0.005;
        double lastTime = 0.0;
        double nextSample = 0.0;

        bool asleep() const { return regs[0x6B] & 0x40; }
        int dlpfConfig() const { return std::min(regs[0x1A] & 0x07, 6); }
        double accelRangeG() const { return 2.0 * (1 << ((regs[0x1C] >> 3) & 0x03)); }
        double gyroRangeDps() const { return 250.0 * (1 << ((regs[0x1B] >> 3) & 0x03)); }
        double sampleRate() const { return (dlpfConfig() == 0 ? 8000.0 : 1000.0) / (1.0 + regs[0x19]); }

        void powerOnDefaults() {
            regs.fill(0);
            regs[0x6B] = 0x40;   // asleep
            regs[0x75] = 0x68;   // WHO_AM_I (also with AD0 high)
            pointer = 0;
        }

        static void putInt16(uint8_t* at, long v) {
            const int16_t x = int16_t(v);
            at[0] = uint8_t((x >> 8) & 0xff);
            at[1] = uint8_t(x & 0xff);
        }
        static int16_t getInt16(const uint8_t* at) { return int16_t((at[0] << 8) | at[1]); }

        // Latch the filtered values into the data registers 0x3B.. (physics thread).
        void latchRegisters() {
            Steps::Saturation accelSat{accelRangeG()}, gyroSat{gyroRangeDps()};
            Steps::Quantize accelQ{accelRangeG() / 32768.0}, gyroQ{gyroRangeDps() / 32768.0};
            for (int i = 0; i < 3; ++i) {
                putInt16(&regs[0x3B + 2 * i], accelQ.counts(accelSat.apply(accel[i].dlpf.y)));
                putInt16(&regs[0x43 + 2 * i], gyroQ.counts(gyroSat.apply(gyro[i].dlpf.y)));
            }
            putInt16(&regs[0x41], std::lround((25.0 - 36.53) * 340.0));   // TEMP_OUT at 25 °C
        }

        double readingAccelG(int i) const { return getInt16(&regs[0x3B + 2 * i]) * accelRangeG() / 32768.0; }
        double readingGyroDps(int i) const { return getInt16(&regs[0x43 + 2 * i]) * gyroRangeDps() / 32768.0; }

    public:
        using Emulator::Emulator;

        void init() override {
            using EmulatorIO::param;
            ComponentBlueprint* bp = component ? component->getBlueprint() : nullptr;
            QMap<QString, QVariant> p = bp ? bp->emulatorDef.parameters : QMap<QString, QVariant>{};
            accelDensityG = param(p, "accel_noise_density_ug", 400.0) * 1e-6;
            gyroDensity = param(p, "gyro_noise_density_dps", 0.005);
            const double accelSpreadG = param(p, "accel_bias_spread_mg", 0.0) / 1000.0;
            const double gyroSpread = param(p, "gyro_bias_spread_dps", 0.0);
            const double scaleSpread = param(p, "scale_spread", 0.0);
            address = param(p, "ad0_high", 0.0) != 0.0 ? 0x69 : 0x68;

            rng.seed(seed());
            std::normal_distribution<double> n(0.0, 1.0);
            for (int i = 0; i < 3; ++i) {
                accel[i].scale = 1.0 + n(rng) * scaleSpread;
                accel[i].bias.draw(rng, accelSpreadG);
                accel[i].bias.walkRate = param(p, "accel_bias_walk_mg", 0.0) / 1000.0;
                accel[i].dlpf.reset();
                gyro[i].scale = 1.0 + n(rng) * scaleSpread;
                gyro[i].bias.draw(rng, gyroSpread);
                gyro[i].bias.walkRate = param(p, "gyro_bias_walk_dps", 0.0);
                gyro[i].dlpf.reset();
            }
            powerOnDefaults();
            lastTime = 0.0;
            nextSample = 0.0;
            if (buses && pins)
                buses->addI2C(address, this, QString("MPU6050 (part %1)").arg(component->uid),
                              pins->ensurePin(component->uid, "sda", "i2c_sda"), pins->ensurePin(component->uid, "scl", "i2c_scl"));
        }

        void update() override {
            const double dt = std::max(0.0, simTime - lastTime);
            lastTime = simTime;

            // The chain runs continuously (the analog front end and filter
            // don't stop between samples); the registers latch at the ODR.
            BasicIOValue a = component->getSensorValue("acceleration");
            BasicIOValue g = component->getSensorValue("gyroscope");
            const double fs = dt > 0.0 ? 1.0 / dt : 0.0;
            for (int i = 0; i < 3; ++i) {
                const double trueAccelG = (a.data.size() > size_t(i) ? a.data[i] : 0.0) / kG;
                const double trueGyro = g.data.size() > size_t(i) ? g.data[i] : 0.0;
                // White noise at the ADC (density over the step's Nyquist band),
                // then the DLPF shapes it, so the output noise is density x sqrt(BW).
                accel[i].noise = {accelDensityG, fs / 2.0};
                gyro[i].noise = {gyroDensity, fs / 2.0};
                accel[i].dlpf.cutoff = kAccelBandwidth[dlpfConfig()];
                gyro[i].dlpf.cutoff = kGyroBandwidth[dlpfConfig()];
                const double ax = trueAccelG * accel[i].scale + accel[i].bias.step(dt, rng) + accel[i].noise.sample(rng);
                const double gx = trueGyro * gyro[i].scale + gyro[i].bias.step(dt, rng) + gyro[i].noise.sample(rng);
                // A cutoff above the step's Nyquist frequency passes through.
                if (accel[i].dlpf.cutoff >= fs / 2.0) { accel[i].dlpf.y = ax; accel[i].dlpf.primed = true; }
                else accel[i].dlpf.apply(ax, dt);
                if (gyro[i].dlpf.cutoff >= fs / 2.0) { gyro[i].dlpf.y = gx; gyro[i].dlpf.primed = true; }
                else gyro[i].dlpf.apply(gx, dt);
            }

            if (asleep()) {
                std::fill(regs.begin() + 0x3B, regs.begin() + 0x49, 0);
            } else if (simTime + 1e-12 >= nextSample) {
                latchRegisters();
                nextSample = simTime + 1.0 / sampleRate();
            }

            BasicIOValue ar = component->getSensorValue("accel_reading");
            BasicIOValue gr = component->getSensorValue("gyro_reading");
            for (int i = 0; i < 3; ++i) {
                if (ar.data.size() > size_t(i)) ar.data[i] = readingAccelG(i);
                if (gr.data.size() > size_t(i)) gr.data[i] = readingGyroDps(i);
            }
            if (!ar.data.empty()) component->setSensorValue("accel_reading", ar);
            if (!gr.data.empty()) component->setSensorValue("gyro_reading", gr);
        }

        void reset() override {
            powerOnDefaults();
        }

        // ---- I2C (controller thread; the lockstep hand-off orders it with update())

        bool i2cWrite(const uint8_t* data, int len, bool stop) override {
            (void)stop;
            if (len <= 0) return true;
            pointer = data[0] & 0x7f;
            for (int i = 1; i < len; ++i) {
                uint8_t value = data[i];
                if (pointer == 0x6B && (value & 0x80)) {   // DEVICE_RESET
                    powerOnDefaults();
                    value &= 0x7f;
                    value |= 0x40;   // and back asleep, as after power-on
                }
                if (pointer != 0x75 && (pointer < 0x3B || pointer > 0x48)) regs[pointer] = value;   // read-only: WHO_AM_I, data
                pointer = (pointer + 1) & 0x7f;
            }
            return true;
        }

        int i2cRead(uint8_t* data, int len) override {
            for (int i = 0; i < len; ++i) {
                data[i] = regs[pointer];
                pointer = (pointer + 1) & 0x7f;
            }
            return len;
        }
};
