#pragma once

#include "Emulator.h"
#include "Document/Components/ComponentInstance.h"
#include "Document/Components/ComponentBlueprint.h"
#include "Simulation/Rendering/CameraRenderer.h"
#include "Simulation/Controller/DeviceBus.h"
#include "Simulation/Controller/PinBus.h"
#include "Utils/Log.h"
#include <QVariantMap>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <limits>
#include <mutex>
#include <random>

/*
 * OV7670 / ESP32-cam style camera (rgb) and cheap depth camera (depth).
 * One stream per camera-kind output signal, so an RGB-D module is one
 * component with two signals.
 *
 * Pipeline (Async cadence, period 1/fps in sim time):
 *   sample()  physics thread: submit the camera to the CameraRenderer (scene
 *             build only) and note the frame as in flight.
 *   worker    render + read back, then this stream's error model (noise,
 *             colour cast, quantisation, random drops), queued as pending.
 *   awaitDue  before each step: wait until every in-flight frame due by the
 *             end of the step (capture time + latency_ms) has come back, so
 *             what the firmware sees depends on sim time only. A frame that
 *             takes longer than kDeliveryTimeout of wall time is given up on
 *             and published on arrival (counted as late).
 *   update()  every physics step: publish the newest pending frame whose
 *             capture time + latency_ms has passed (sim time) into the
 *             signal's ImageIOValue.
 *
 * Random drops and noise come from the instance seed, so a run repeats.
 *
 * Parameters (component .rsdef -> emulator.parameters), read in init():
 *   fps                 frames per sim second             (default 15)
 *   latency_ms          capture -> visible to firmware, sim ms (default 50)
 *   drop_probability    chance a frame is lost, 0..1      (default 0)
 *   noise_stddev        rgb: per-channel noise, 0..255    (default 0)
 *   color_gain          rgb: [r, g, b] channel gains      (default [1,1,1])
 *   bits_per_channel    rgb: 1..8 quantisation            (default 8)
 *   depth_noise_stddev  depth: meters                     (default 0)
 *   depth_min/depth_max depth: valid range in meters, outside reads 0
 *                       (default 0 / 0 = no limit)
 */



// Worker-side half of one camera signal: owned jointly by the emulator and
// any frame in flight (see CameraFrameSink).
class CameraStream : public CameraFrameSink {
    public:
        // Fixed after construction (read by the worker without locking).
        QString signal;
        int width = 0;
        int height = 0;
        bool depth = false;
        double latencySec = 0.05;
        double dropProbability = 0.0;
        double noiseStdDev = 0.0;
        double gain[3] = {1.0, 1.0, 1.0};
        int bitsPerChannel = 8;
        double depthNoiseStdDev = 0.0;
        double depthMin = 0.0;
        double depthMax = 0.0;

        std::atomic<quint64> dropped{0};
        std::atomic<quint64> late{0};

        // Physics thread only.
        quint64 seq = 0;

        // Rendered frames waiting for their latency to pass, oldest first.
        std::mutex pendingMutex;
        std::condition_variable resolvedCv;   // a frame in flight came back (or was lost)
        std::deque<RenderedFrame> pending;
        std::deque<double> inFlight;          // capture times submitted, not yet back
        int abandoned = 0;                    // timed out of inFlight, still to come back
        bool warnedLate = false;              // physics thread only

        // Before the first frame (init(), no frame in flight).
        void seedRng(quint64 s) { rng.seed(s); noise.reset(); }

        // Physics thread, right before submitting a frame captured at simTime.
        void markInFlight(double simTime) {
            std::lock_guard<std::mutex> lock(pendingMutex);
            inFlight.push_back(simTime);
        }

        void onCameraFrame(RenderedFrame& frame) override {
            const bool drop = dropProbability > 0.0 && uniform(rng) < dropProbability;
            if (drop) dropped++;
            else if (depth) applyDepthErrors(frame);
            else applyRgbErrors(frame);

            {
                std::lock_guard<std::mutex> lock(pendingMutex);
                resolveOldest();
                if (!drop) pending.push_back(std::move(frame));
            }
            resolvedCv.notify_all();
        }

        void onCameraFrameLost() override {
            dropped++;
            {
                std::lock_guard<std::mutex> lock(pendingMutex);
                resolveOldest();
            }
            resolvedCv.notify_all();
        }

        // Physics thread. False if a frame due by `until` did not come back in
        // time; it is then no longer waited for (published whenever it arrives).
        bool awaitDue(double until, std::chrono::milliseconds timeout) {
            std::unique_lock<std::mutex> lock(pendingMutex);
            auto due = [&] { return !inFlight.empty() && inFlight.front() + latencySec <= until; };
            if (resolvedCv.wait_for(lock, timeout, [&] { return !due(); })) return true;
            while (due()) {
                inFlight.pop_front();
                ++abandoned;
            }
            return false;
        }

        void clearPending() {
            {
                std::lock_guard<std::mutex> lock(pendingMutex);
                pending.clear();
                inFlight.clear();
                abandoned = 0;
            }
            resolvedCv.notify_all();
        }

    private:
        // pendingMutex held: a frame came back, in submit order.
        void resolveOldest() {
            if (abandoned > 0) --abandoned;
            else if (!inFlight.empty()) inFlight.pop_front();
        }

        // Worker thread only.
        std::mt19937_64 rng;
        std::normal_distribution<double> noise{0.0, 1.0};
        std::uniform_real_distribution<double> uniform{0.0, 1.0};

        void applyRgbErrors(RenderedFrame& frame) {
            const bool doNoise = noiseStdDev > 0.0;
            const bool doGain = gain[0] != 1.0 || gain[1] != 1.0 || gain[2] != 1.0;
            const int bits = std::clamp(bitsPerChannel, 1, 8);
            if (!doNoise && !doGain && bits == 8) return;

            const int levels = (1 << bits) - 1;
            unsigned char* px = reinterpret_cast<unsigned char*>(frame.pixels.data());
            const qsizetype n = frame.pixels.size();
            for (qsizetype i = 0; i < n; ++i) {
                double v = px[i] * gain[i % 3];
                if (doNoise) v += noise(rng) * noiseStdDev;
                v = std::clamp(v, 0.0, 255.0);
                if (bits < 8) v = std::round(v / 255.0 * levels) * 255.0 / levels;
                px[i] = static_cast<unsigned char>(std::lround(v));
            }
        }

        void applyDepthErrors(RenderedFrame& frame) {
            const bool doNoise = depthNoiseStdDev > 0.0;
            if (!doNoise && depthMin <= 0.0 && depthMax <= 0.0) return;

            float* px = reinterpret_cast<float*>(frame.pixels.data());
            const qsizetype n = frame.pixels.size() / qsizetype(sizeof(float));
            for (qsizetype i = 0; i < n; ++i) {
                double v = px[i];
                if ((depthMin > 0.0 && v < depthMin) || (depthMax > 0.0 && v > depthMax)) {
                    px[i] = 0.0f;   // out of range reads as "no return", like real sensors
                    continue;
                }
                if (doNoise) v = std::max(0.0, v + noise(rng) * depthNoiseStdDev);
                px[i] = static_cast<float>(v);
            }
        }
};



class CameraEmulatorCpp : public Emulator, public CameraDevice {
    Q_OBJECT

    private:
        double fps = 15.0;
        double prevTickTime = 0.0;   // physics thread: sim time of the previous update()
        // Replaced (never mutated) by init(): frames still in flight keep the
        // old streams alive and land there harmlessly.
        std::vector<std::shared_ptr<CameraStream>> streams;

        static double param(const QMap<QString, QVariant>& p, const char* key, double def) {
            return p.contains(key) ? p.value(key).toDouble() : def;
        }

        // Stream for a signal name; empty name = first camera signal.
        CameraStream* find(const QString& name, int* index = nullptr) const {
            for (size_t i = 0; i < streams.size(); ++i) {
                if (name.isEmpty() || streams[i]->signal == name) {
                    if (index) *index = int(i);
                    return streams[i].get();
                }
            }
            return nullptr;
        }

        ImageIOValue current(const QString& name) const {
            CameraStream* s = find(name);
            return s ? component->getImageValue(s->signal) : ImageIOValue{};
        }

    public:
        using Emulator::Emulator;

        void init() override {
            streams.clear();
            ComponentBlueprint* bp = component ? component->getBlueprint() : nullptr;
            if (!bp) return;

            const auto& params = bp->emulatorDef.parameters;
            fps = param(params, "fps", 15.0);
            if (fps <= 0.0) fps = 15.0;

            for (auto it = bp->interfaceOutputs.constBegin(); it != bp->interfaceOutputs.constEnd(); ++it) {
                if (it.value().target.kind != "camera") continue;

                auto s = std::make_shared<CameraStream>();
                s->signal = it.key();
                for (const CameraDef& c : bp->cameras) {
                    if (c.id != it.value().target.id) continue;
                    s->width = c.resolution.first;
                    s->height = c.resolution.second;
                    s->depth = c.type == "depth";
                }
                s->latencySec = std::max(0.0, param(params, "latency_ms", 50.0)) / 1000.0;
                s->dropProbability = std::clamp(param(params, "drop_probability", 0.0), 0.0, 1.0);
                s->noiseStdDev = param(params, "noise_stddev", 0.0);
                s->bitsPerChannel = int(param(params, "bits_per_channel", 8));
                s->depthNoiseStdDev = param(params, "depth_noise_stddev", 0.0);
                s->depthMin = param(params, "depth_min", 0.0);
                s->seedRng(seed() ^ (0x9E3779B97F4A7C15ull * (streams.size() + 1)));
                s->depthMax = param(params, "depth_max", 0.0);
                const QVariantList g = params.value("color_gain").toList();
                for (int i = 0; i < 3 && i < g.size(); ++i) s->gain[i] = g[i].toDouble();

                if (s->width <= 0 || s->height <= 0) {
                    Log::error(QString("CameraEmulator: component %1: camera signal '%2' has no resolution; disabled")
                                    .arg(component->uid).arg(s->signal));
                    continue;
                }
                streams.push_back(s);
            }
            clearPublished();
            // Firmware reaches the camera through its SCCB data pin (esp32-camera's pin_sccb_sda).
            if (pins && buses && !streams.empty()) buses->addCamera(pins->ensurePin(component->uid, "sda", "i2c_sda"), this);
        }

        // ---- CameraDevice (firmware): the first camera signal.
        int frameWidth() const override { return streams.empty() ? 0 : streams[0]->width; }
        int frameHeight() const override { return streams.empty() ? 0 : streams[0]->height; }
        bool isDepth() const override { return !streams.empty() && streams[0]->depth; }
        CameraFrame latestFrame() const override {
            CameraFrame f;
            if (streams.empty()) return f;
            const ImageIOValue img = component->getImageValue(streams[0]->signal);
            f.pixels = img.pixels;
            f.width = img.width;
            f.height = img.height;
            f.depth = img.format == ImageFormat::Depth32F;
            f.seq = img.seq;
            f.time = img.time;
            return f;
        }

        Schedule schedule() const override {
            return {Cadence::Async, 1.0 / fps};
        }

        void sample(const SampleContext& ctx) override {
            if (!ctx.cameraRenderer) return;
            for (const auto& s : streams) {
                CameraRequest req;
                req.cameraId = component->getImageValue(s->signal).mujocoId;
                req.width = s->width;
                req.height = s->height;
                req.depth = s->depth;
                req.sink = s;
                if (req.cameraId < 0) { s->dropped++; continue; }
                s->markInFlight(ctx.simTime);
                if (!ctx.cameraRenderer->submit(ctx.m, ctx.d, req)) s->onCameraFrameLost();
            }
        }

        // Wall time a due frame may take before it is published late instead.
        static constexpr std::chrono::milliseconds kDeliveryTimeout{1000};

        void awaitDue(double until) override {
            for (const auto& s : streams) {
                if (s->awaitDue(until, kDeliveryTimeout) || s->warnedLate) continue;
                s->warnedLate = true;
                Log::warning(QString("CameraEmulator: component %1: camera '%2' frame took over %3 ms to render; publishing late")
                                .arg(component->uid).arg(s->signal).arg(kDeliveryTimeout.count()));
            }
        }

        void update() override {
            for (const auto& s : streams) {
                RenderedFrame frame;
                bool have = false;
                {
                    std::lock_guard<std::mutex> lock(s->pendingMutex);
                    // Newest frame whose latency has passed; older due ones are
                    // superseded (a real camera's buffer only holds the latest).
                    // A frame captured at this very step shows up on the next
                    // one at the earliest: awaitDue() could not wait for it, so
                    // publishing it now would depend on the render speed.
                    while (!s->pending.empty() && s->pending.front().simTime + s->latencySec <= simTime
                           && s->pending.front().simTime < simTime) {
                        frame = std::move(s->pending.front());
                        s->pending.pop_front();
                        have = true;
                    }
                }
                if (!have) continue;
                // Due before the previous tick: the worker delivered it late.
                if (frame.simTime + s->latencySec < prevTickTime) s->late++;

                ImageIOValue img = component->getImageValue(s->signal);
                img.width = frame.width;
                img.height = frame.height;
                img.format = frame.depth ? ImageFormat::Depth32F : ImageFormat::RGB888;
                img.time = frame.simTime;
                img.seq = ++s->seq;
                img.pixels = std::make_shared<const QByteArray>(std::move(frame.pixels));
                component->setImageValue(s->signal, img);
            }
            prevTickTime = simTime;
        }

        void reset() override {
            for (const auto& s : streams) s->clearPending();
            clearPublished();
        }

        // Drops the last published frame (keeps the MuJoCo camera id).
        void clearPublished() {
            for (const auto& s : streams) {
                ImageIOValue img = component->getImageValue(s->signal);
                img.pixels.reset();
                img.seq = 0;
                img.time = 0.0;
                component->setImageValue(s->signal, img);
                s->seq = 0;
            }
            prevTickTime = 0.0;
        }
};
