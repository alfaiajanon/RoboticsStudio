#include "CameraRenderer.h"

#include <GLFW/glfw3.h>
#include <cstring>

#include "GlfwRuntime.h"
#include "Simulation/MujocoContext.h"
#include "Utils/Log.h"



/*
 * UI thread. Creates the hidden window (no context made current here) and
 * starts the worker. If GLFW or the window fails, the renderer stays
 * unavailable and every submit() drops its frame.
 */
CameraRenderer::CameraRenderer(MujocoContext* ctx, std::mutex* physicsMutex, DisplayTextures* displays)
    : mujocoContext(ctx), physicsMutex(physicsMutex), displays(displays) {
    mjv_defaultOption(&option);
    // Same as the app's viewer: no site markers in camera images.
    for (int i = 0; i < mjNGROUP; ++i) option.sitegroup[i] = 0;

    mjr_defaultContext(&gpu);
    for (SceneSlot& slot : sceneSlots) mjv_defaultScene(&slot.scene);

    if (!GlfwRuntime::acquire()) return;
    acquiredGlfw = true;

    // Everything is drawn into MuJoCo's offscreen buffer; the window only
    // provides the GL context, so it can be tiny.
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_FALSE);
    window = glfwCreateWindow(16, 16, "MuJoCo cameras", nullptr, nullptr);
    if (!window) {
        Log::error("CameraRenderer: failed to create the hidden GLFW window; cameras disabled");
        return;
    }

    available = true;
    worker = std::thread(&CameraRenderer::workerLoop, this);
}



CameraRenderer::~CameraRenderer() {
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        stopRequested = true;
    }
    queueCv.notify_all();
    if (worker.joinable()) worker.join();   // frees the mjrContext on its way out

    for (SceneSlot& slot : sceneSlots) {
        if (slot.allocated) mjv_freeScene(&slot.scene);
    }
    if (window) glfwDestroyWindow(window);
    if (acquiredGlfw) GlfwRuntime::release();
}



/*
 * Physics thread, physicsMutex held (so never concurrent with invalidate()).
 * The scene is built outside queueMutex: a slot in Free state belongs to the
 * producer, and there is only one producer.
 */
bool CameraRenderer::submit(const mjModel* m, mjData* d, const CameraRequest& request) {
    if (!available || !m || !d || !request.sink) return false;
    if (request.cameraId < 0 || request.cameraId >= m->ncam) return false;
    if (request.width <= 0 || request.height <= 0) return false;

    int index = -1;
    quint64 gen;
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        gen = generation;

        // Latest wins: take back this camera's job if it's still waiting.
        for (auto it = queue.begin(); it != queue.end(); ++it) {
            if (sceneSlots[*it].request.sink == request.sink && sceneSlots[*it].request.cameraId == request.cameraId) {
                index = *it;
                queue.erase(it);
                break;
            }
        }
        if (index < 0) {
            for (int i = 0; i < kSlotCount; ++i) {
                if (sceneSlots[i].state == SlotState::Free) { index = i; break; }
            }
        }
        if (index < 0) return false;
        sceneSlots[index].state = SlotState::Queued;   // not in the queue yet: invisible to the worker
    }

    SceneSlot& slot = sceneSlots[index];
    if (!slot.allocated || slot.generation != gen) {
        mjv_makeScene(m, &slot.scene, kMaxGeom);   // frees the old allocation first
        slot.allocated = true;
        slot.generation = gen;
    }

    mjvCamera cam;
    mjv_defaultCamera(&cam);
    cam.type = mjCAMERA_FIXED;
    cam.fixedcamid = request.cameraId;
    mujocoContext->applySceneFlags(&slot.scene);   // same world flags (fog, ...) as the viewport
    mjv_updateScene(m, d, &option, nullptr, &cam, mjCAT_ALL, &slot.scene);

    slot.request = request;
    slot.simTime = d->time;
    slot.jobGeneration = gen;

    {
        std::lock_guard<std::mutex> lock(queueMutex);
        queue.push_back(index);
    }
    queueCv.notify_one();
    return true;
}



/*
 * physicsMutex held, old model still alive. Queued jobs are dropped (their
 * scenes reference the old model); a job the worker is rendering finishes but
 * isn't delivered. Slots and the GPU context rebuild lazily.
 */
void CameraRenderer::invalidate() {
    std::lock_guard<std::mutex> lock(queueMutex);
    ++generation;
    for (int index : queue) sceneSlots[index].state = SlotState::Free;
    queue.clear();
}



void CameraRenderer::workerLoop() {
    glfwMakeContextCurrent(window);

    RenderedFrame frame;
    while (true) {
        int index;
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            queueCv.wait(lock, [this] { return stopRequested || !queue.empty(); });
            if (stopRequested) break;
            index = queue.front();
            queue.pop_front();
            sceneSlots[index].state = SlotState::Rendering;
        }

        SceneSlot& slot = sceneSlots[index];
        bool rendered = false;
        if (ensureGpuContext(slot.jobGeneration)) {
            renderSlot(slot, frame);
            rendered = true;
        }

        std::shared_ptr<CameraFrameSink> sink;
        bool current;
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            current = slot.jobGeneration == generation;
            sink = std::move(slot.request.sink);
            slot.request = CameraRequest{};
            slot.state = SlotState::Free;
        }
        // Delivered outside the lock; a frame of a replaced model is dropped.
        if (rendered && current && sink) sink->onCameraFrame(frame);
    }

    if (gpuReady) mjr_freeContext(&gpu);
    gpuReady = false;
    glfwMakeContextCurrent(nullptr);
}



/*
 * (Re)builds the mjrContext when the job comes from a model the GPU doesn't
 * have yet. Needs the live model, so it takes physicsMutex -- once per model
 * reload. False if the model changed again since the job was queued.
 */
bool CameraRenderer::ensureGpuContext(quint64 jobGeneration) {
    if (gpuReady && gpuGeneration == jobGeneration) return true;

    std::lock_guard<std::mutex> physicsLock(*physicsMutex);
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        if (jobGeneration != generation) return false;
    }
    const mjModel* m = mujocoContext->getModel();
    if (!m) return false;

    mjr_makeContext(m, &gpu, mjFONTSCALE_100);   // frees the previous context first
    displayCache = DisplayTextures::UploadCache();   // textures are back to the model's (dark) data
    gpu.readDepthMap = mjDEPTH_ZERONEAR;
    mjr_setBuffer(mjFB_OFFSCREEN, &gpu);
    gpuReady = true;
    gpuGeneration = jobGeneration;

    offWidth = gpu.offWidth;
    offHeight = gpu.offHeight;
    zNear = m->vis.map.znear * m->stat.extent;
    zFar = m->vis.map.zfar * m->stat.extent;
    return true;
}



void CameraRenderer::renderSlot(SceneSlot& slot, RenderedFrame& out) {
    // generateMujocoXML sizes the offscreen buffer for the largest camera;
    // clip anyway rather than read past it.
    int w = std::min(slot.request.width, offWidth);
    int h = std::min(slot.request.height, offHeight);
    mjrRect viewport = {0, 0, w, h};

    mjr_setBuffer(mjFB_OFFSCREEN, &gpu);
    if (displays) displays->upload(&gpu, displayCache);
    mjr_render(viewport, &slot.scene, &gpu);

    out.width = w;
    out.height = h;
    out.depth = slot.request.depth;
    out.simTime = slot.simTime;

    if (!slot.request.depth) {
        rgbBuffer.resize(size_t(w) * h * 3);
        mjr_readPixels(rgbBuffer.data(), nullptr, viewport, &gpu);

        // GL rows are bottom to top.
        out.pixels.resize(qsizetype(w) * h * 3);
        const size_t rowBytes = size_t(w) * 3;
        for (int y = 0; y < h; ++y) {
            std::memcpy(out.pixels.data() + y * rowBytes, rgbBuffer.data() + size_t(h - 1 - y) * rowBytes, rowBytes);
        }
    } else {
        depthBuffer.resize(size_t(w) * h);
        mjr_readPixels(nullptr, depthBuffer.data(), viewport, &gpu);

        // Non-linear [0,1] depth buffer -> meters along the view axis.
        out.pixels.resize(qsizetype(w) * h * sizeof(float));
        float* dst = reinterpret_cast<float*>(out.pixels.data());
        const double ratio = 1.0 - zNear / zFar;
        for (int y = 0; y < h; ++y) {
            const float* srcRow = depthBuffer.data() + size_t(h - 1 - y) * w;
            float* dstRow = dst + size_t(y) * w;
            for (int x = 0; x < w; ++x) {
                dstRow[x] = float(zNear / (1.0 - srcRow[x] * ratio));
            }
        }
    }
}
