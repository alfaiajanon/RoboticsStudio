#include "OffscreenSim.h"
#include "Simulation/MujocoContext.h"
#include "Simulation/SimulationManager.h"
#include "Simulation/Rendering/GlfwRuntime.h"
#include "Application/Application.h"


OffscreenSim::OffscreenSim(MujocoContext* ctx) : mujocoContext(ctx) {}


OffscreenSim::~OffscreenSim() {
    if (hiddenWindow) {
        glfwMakeContextCurrent(hiddenWindow);
        glfwDestroyWindow(hiddenWindow);
        hiddenWindow = nullptr;
    }
    if (acquiredGlfw) GlfwRuntime::release();
}

void OffscreenSim::init(int w, int h){
    width = w;
    height = h;

    if (!GlfwRuntime::acquire()) exit(1);
    acquiredGlfw = true;

    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_FALSE);

    hiddenWindow = glfwCreateWindow(MAX_WIDTH, MAX_HEIGHT, "Hidden MuJoCo", nullptr, nullptr);
    if (!hiddenWindow){
        Log::error("Failed to create GLFW hidden window");
        GlfwRuntime::release();
        acquiredGlfw = false;
        exit(1);
    }

    glfwMakeContextCurrent(hiddenWindow);

    pixelBuffer.resize(w * h * 3);
}




void OffscreenSim::setSize(int w, int h) {
    if (w > MAX_WIDTH)  w = MAX_WIDTH;
    if (h > MAX_HEIGHT) h = MAX_HEIGHT;
    width = w;
    height = h;
    pixelBuffer.resize(width * height * 3);
}




QImage OffscreenSim::render(std::mutex* sceneLock){
    MujocoContext* ctx = mujocoContext;
    if (!ctx) {
        // No injected context: render the app's simulation, if one exists yet.
        Application* app = Application::getInstance();
        SimulationManager* simManager = app ? app->getSimulationManager() : nullptr;
        if (!simManager) return QImage();
        ctx = simManager->getMujocoContext();
    }
    if (!ctx->getModel()) return QImage();

    glfwMakeContextCurrent(hiddenWindow);

    {
        // Only this part reads mjData, which the physics thread writes.
        std::unique_lock<std::mutex> lock;
        if (sceneLock) lock = std::unique_lock<std::mutex>(*sceneLock);
        ctx->updateScene();
        if (decorHook) decorHook(ctx->getModel(), ctx->getData(), ctx->getScene());
    }
    mjrRect viewport = {0, 0, width, height};
    ctx->render(viewport);

    glPixelStorei(GL_PACK_ALIGNMENT, 1);

    mjr_readPixels(pixelBuffer.data(), nullptr, viewport, ctx->getContext());
    QImage img(pixelBuffer.data(), width, height, width * 3, QImage::Format_RGB888);

    return img.flipped(Qt::Vertical);
}
