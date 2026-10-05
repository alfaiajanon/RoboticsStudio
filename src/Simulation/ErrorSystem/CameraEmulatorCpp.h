#pragma once

#include "Emulator.h"
#include "Document/Components/ComponentInstance.h"
#include "Document/Components/ComponentBlueprint.h"
#include "Simulation/Rendering/CameraRenderer.h"
#include "Utils/Log.h"
#include <QVariantMap>
#include <algorithm>
#include <atomic>
#include <cmath>
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
 *             build only); a full renderer drops the frame.
 *   worker    render + read back, then this stream's error model (noise,
 *             colour cast, quantisation, random drops), queued as pending.
 *   update()  every physics tick: publish the newest pending frame whose
 *             capture time + latency_ms has passed (sim time) into the
 *             signal's ImageIOValue. A frame the worker delivers after its
 *             deadline is published on arrival (counted as late).
 *
 * Parameters (component .rsdef -> emulator.parameters), read in init():
 *   fps                 frames per sim second             (default 15)
 *   latency_ms          capture -> visible to JS, sim ms  (default 50)
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
        std::deque<RenderedFrame> pending;

        void onCameraFrame(RenderedFrame& frame) override {
            if (dropProbability > 0.0 && uniform(rng) < dropProbability) {
                dropped++;
                return;
            }
            if (depth) applyDepthErrors(frame);
            else       applyRgbErrors(frame);

            std::lock_guard<std::mutex> lock(pendingMutex);
            pending.push_back(std::move(frame));
        }

        void clearPending() {
            std::lock_guard<std::mutex> lock(pendingMutex);
            pending.clear();
        }

    private:
        // Worker thread only.
        std::mt19937 rng{std::random_device{}()};
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



class CameraEmulatorCpp : public Emulator {
    Q_OBJECT

    private:
        double fps = 15.0;
        double prevTickTime = 0.0;   // physics thread: sim time of the previous update()
        // Replaced (never mutated) by init(): frames still in flight keep the
        // old streams alive and land there harmlessly.
        std::vector<std::shared_ptr<CameraStream>> streams;
        // MCU thread: seq of the last frame JS captured, per stream.
        std::vector<quint64> lastReadSeq;

        static double param(const QMap<QString, QVariant>& p, const char* key, double def) {
            return p.contains(key) ? p.value(key).toDouble() : def;
        }

        // Stream for a JS-given signal name; empty name = first camera signal.
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
            lastReadSeq.assign(streams.size(), 0);
            clearPublished();
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
                if (req.cameraId < 0 || !ctx.cameraRenderer->submit(ctx.m, ctx.d, req)) s->dropped++;
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
                    while (!s->pending.empty() && s->pending.front().simTime + s->latencySec <= simTime) {
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
            std::fill(lastReadSeq.begin(), lastReadSeq.end(), 0);
            prevTickTime = 0.0;
        }



        // ---- API exposed to JS. `signal` picks the camera of a multi-camera
        // ---- component; omitted, the first camera signal is used.

        Q_INVOKABLE int width(const QString& signal = QString()) const {
            CameraStream* s = find(signal);
            return s ? s->width : 0;
        }

        Q_INVOKABLE int height(const QString& signal = QString()) const {
            CameraStream* s = find(signal);
            return s ? s->height : 0;
        }

        // Sequence number of the newest published frame (0 = none yet).
        Q_INVOKABLE double seq(const QString& signal = QString()) const {
            return double(current(signal).seq);
        }

        // True if a frame newer than the last capture() is ready.
        Q_INVOKABLE bool available(const QString& signal = QString()) const {
            int index = -1;
            if (!find(signal, &index)) return false;
            ImageIOValue img = current(signal);
            return img.pixels && img.seq != lastReadSeq[index];
        }

        // {width, height, time, seq, format: "rgb"|"depth", data: ArrayBuffer}
        // rgb: 3 bytes per pixel, rows top to bottom. depth: Float32 meters
        // (read with new Float32Array(frame.data)), 0 = no return.
        // undefined until the first frame arrives.
        Q_INVOKABLE QVariant capture(const QString& signal = QString()) {
            int index = -1;
            if (!find(signal, &index)) return QVariant();
            ImageIOValue img = current(signal);
            if (!img.pixels) return QVariant();
            lastReadSeq[index] = img.seq;

            QVariantMap frame;
            frame["width"] = img.width;
            frame["height"] = img.height;
            frame["time"] = img.time;
            frame["seq"] = double(img.seq);
            frame["format"] = img.format == ImageFormat::Depth32F ? "depth" : "rgb";
            frame["data"] = *img.pixels;
            return frame;
        }

        // rgb: {r, g, b} of one pixel; depth: {depth}. undefined if out of range.
        Q_INVOKABLE QVariant getPixel(int x, int y, const QString& signal = QString()) const {
            ImageIOValue img = current(signal);
            if (!img.pixels || x < 0 || y < 0 || x >= img.width || y >= img.height) return QVariant();
            QVariantMap px;
            if (img.format == ImageFormat::Depth32F) {
                px["depth"] = reinterpret_cast<const float*>(img.pixels->constData())[y * img.width + x];
            } else {
                const unsigned char* p = reinterpret_cast<const unsigned char*>(img.pixels->constData()) + (y * img.width + x) * 3;
                px["r"] = p[0];
                px["g"] = p[1];
                px["b"] = p[2];
            }
            return px;
        }

        // Depth in meters at a pixel (0 = no return, -1 = not a depth camera / out of range).
        Q_INVOKABLE double getDepth(int x, int y, const QString& signal = QString()) const {
            ImageIOValue img = current(signal);
            if (!img.pixels || img.format != ImageFormat::Depth32F) return -1.0;
            if (x < 0 || y < 0 || x >= img.width || y >= img.height) return -1.0;
            return reinterpret_cast<const float*>(img.pixels->constData())[y * img.width + x];
        }

        // One byte per pixel, rows top to bottom, as an ArrayBuffer.
        // rgb: luma. depth: near = bright, 0 for no return; scaled to depth_max
        // (or the frame's farthest point if depth_max is 0).
        Q_INVOKABLE QByteArray toGray(const QString& signal = QString()) const {
            ImageIOValue img = current(signal);
            if (!img.pixels) return QByteArray();
            const int n = img.width * img.height;
            QByteArray out(n, '\0');
            unsigned char* dst = reinterpret_cast<unsigned char*>(out.data());

            if (img.format == ImageFormat::Depth32F) {
                const float* src = reinterpret_cast<const float*>(img.pixels->constData());
                CameraStream* s = find(signal);
                double range = s ? s->depthMax : 0.0;
                if (range <= 0.0) {
                    for (int i = 0; i < n; ++i) range = std::max(range, double(src[i]));
                }
                if (range <= 0.0) return out;
                for (int i = 0; i < n; ++i) {
                    if (src[i] <= 0.0f) continue;
                    dst[i] = static_cast<unsigned char>(std::clamp(255.0 * (1.0 - src[i] / range), 0.0, 255.0));
                }
            } else {
                const unsigned char* src = reinterpret_cast<const unsigned char*>(img.pixels->constData());
                for (int i = 0; i < n; ++i) {
                    dst[i] = static_cast<unsigned char>((77 * src[3 * i] + 150 * src[3 * i + 1] + 29 * src[3 * i + 2]) >> 8);
                }
            }
            return out;
        }

        // Mean over a rectangle (clipped to the image).
        // rgb: {r, g, b, gray}; depth: {depth} over pixels with a return.
        Q_INVOKABLE QVariant regionMean(int x, int y, int w, int h, const QString& signal = QString()) const {
            ImageIOValue img = current(signal);
            if (!img.pixels) return QVariant();
            const int x0 = std::max(0, x), y0 = std::max(0, y);
            const int x1 = std::min(img.width, x + w), y1 = std::min(img.height, y + h);
            if (x1 <= x0 || y1 <= y0) return QVariant();

            QVariantMap result;
            if (img.format == ImageFormat::Depth32F) {
                const float* src = reinterpret_cast<const float*>(img.pixels->constData());
                double sum = 0.0;
                int count = 0;
                for (int yy = y0; yy < y1; ++yy)
                    for (int xx = x0; xx < x1; ++xx) {
                        float v = src[yy * img.width + xx];
                        if (v > 0.0f) { sum += v; ++count; }
                    }
                result["depth"] = count ? sum / count : 0.0;
            } else {
                const unsigned char* src = reinterpret_cast<const unsigned char*>(img.pixels->constData());
                double r = 0, g = 0, b = 0;
                for (int yy = y0; yy < y1; ++yy)
                    for (int xx = x0; xx < x1; ++xx) {
                        const unsigned char* p = src + (yy * img.width + xx) * 3;
                        r += p[0]; g += p[1]; b += p[2];
                    }
                const double n = double(x1 - x0) * (y1 - y0);
                result["r"] = r / n;
                result["g"] = g / n;
                result["b"] = b / n;
                result["gray"] = (0.299 * r + 0.587 * g + 0.114 * b) / n;
            }
            return result;
        }

        // Frames lost so far: renderer busy, drop_probability, no camera.
        Q_INVOKABLE double droppedFrames(const QString& signal = QString()) const {
            CameraStream* s = find(signal);
            return s ? double(s->dropped.load()) : 0.0;
        }

        // Frames the renderer delivered after their latency had already passed
        // (published on arrival instead) -- the GPU can't keep up with fps.
        Q_INVOKABLE double lateFrames(const QString& signal = QString()) const {
            CameraStream* s = find(signal);
            return s ? double(s->late.load()) : 0.0;
        }
};
