#pragma once

#include <QByteArray>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "mujoco/mujoco.h"
#include "DisplayTextures.h"


class MujocoContext;
struct GLFWwindow;



// One rendered camera image, as it comes off the GPU (rows top to bottom).
struct RenderedFrame {
    int width = 0;
    int height = 0;
    bool depth = false;   // false: RGB888 bytes; true: float32 meters per pixel
    double simTime = 0.0; // sim time the scene was captured at
    QByteArray pixels;
};

// Receives rendered frames. Called on the renderer's worker thread (lost
// frames also from invalidate()). Every accepted submit() ends in exactly one
// of the two calls. Jobs hold a shared_ptr to their sink, so a sink outlives
// any frame still in flight -- its owner can go away without waiting.
class CameraFrameSink {
    public:
        virtual ~CameraFrameSink() = default;
        virtual void onCameraFrame(RenderedFrame& frame) = 0;
        // Not rendered: the model was replaced, or the GPU context failed.
        virtual void onCameraFrameLost() {}
};

struct CameraRequest {
    int cameraId = -1;   // MuJoCo camera id
    int width = 0;
    int height = 0;
    bool depth = false;
    std::shared_ptr<CameraFrameSink> sink;
};



/*
 * Renders MuJoCo cameras off the physics thread.
 *
 * Owns one hidden GLFW window (created on the UI thread, as GLFW requires)
 * whose GL context is current only on the worker thread, plus its own
 * mjrContext -- GL objects are per context, so the main viewport's can't be
 * shared.
 *
 * Producer (physics thread, physicsMutex held): submit() builds the camera's
 * mjvScene from the live mjData into a free scene slot -- CPU only, no GL --
 * and queues it. Every frame is rendered, in submit order: when all slots are
 * busy a new one is added (up to kMaxSlots; only past that is a frame
 * dropped). submit() never waits -- the physics thread waits for due frames
 * through the camera emulator instead, outside physicsMutex.
 *
 * Worker: mjr_render into the offscreen buffer, mjr_readPixels, flip, linear
 * depth, hand the frame to the request's sink. mjr_render needs only the scene
 * and the mjrContext, so the worker runs without physicsMutex; it takes it
 * only to (re)build the mjrContext after a model reload.
 *
 * Model reloads: invalidate() (physicsMutex held, old model still alive)
 * bumps a generation; queued jobs are dropped, frames from the old generation
 * are not delivered, and slots/context are rebuilt lazily for the new model.
 *
 * Lock order: physicsMutex -> queueMutex. The worker never holds queueMutex
 * while it waits for physicsMutex.
 */
class CameraRenderer {
    public:
        // displays (optional): screens to upload before each render, so
        // cameras see what displays show.
        CameraRenderer(MujocoContext* ctx, std::mutex* physicsMutex, DisplayTextures* displays = nullptr);
        ~CameraRenderer();

        CameraRenderer(const CameraRenderer&) = delete;
        CameraRenderer& operator=(const CameraRenderer&) = delete;

        // Physics thread, physicsMutex held. False if the frame was dropped
        // (kMaxSlots in use, renderer unavailable, bad request); the sink is
        // then not called.
        bool submit(const mjModel* m, mjData* d, const CameraRequest& request);

        // physicsMutex held, before the model is swapped.
        void invalidate();

        bool isAvailable() const { return available; }

    private:
        enum class SlotState { Free, Queued, Rendering };

        struct SceneSlot {
            mjvScene scene;
            bool allocated = false;
            quint64 generation = 0;    // model the scene was allocated for
            SlotState state = SlotState::Free;
            CameraRequest request;
            double simTime = 0.0;
            quint64 jobGeneration = 0; // model the queued scene was built from
        };

        static constexpr int kInitialSlots = 4;
        static constexpr int kMaxSlots = 32;
        static constexpr int kMaxGeom = 2000;

        MujocoContext* mujocoContext;
        std::mutex* physicsMutex;
        DisplayTextures* displays;

        GLFWwindow* window = nullptr;
        bool acquiredGlfw = false;
        bool available = false;

        mjvOption option;

        std::mutex queueMutex;
        std::condition_variable queueCv;
        // Grows on demand (producer only, under queueMutex); a deque keeps
        // references to existing slots valid. The worker takes its slot
        // reference under queueMutex.
        std::deque<SceneSlot> sceneSlots;
        std::deque<int> queue;         // slot indices, oldest first
        quint64 generation = 1;        // guarded by queueMutex
        bool stopRequested = false;    // guarded by queueMutex

        // Worker-only state.
        mjrContext gpu;
        bool gpuReady = false;
        DisplayTextures::UploadCache displayCache;
        quint64 gpuGeneration = 0;
        int offWidth = 0;
        int offHeight = 0;
        double zNear = 0.0;
        double zFar = 0.0;
        std::vector<unsigned char> rgbBuffer;
        std::vector<float> depthBuffer;

        std::thread worker;

        void workerLoop();
        bool ensureGpuContext(quint64 jobGeneration);
        void renderSlot(SceneSlot& slot, RenderedFrame& out);
};
