#include "OffscreenSim.h"
#include "Simulation/MujocoContext.h"
#include "Simulation/SimulationManager.h"
#include "Application/Application.h"


// Number of live OffscreenSim instances sharing the process-global GLFW state.
static int liveInstances = 0;


OffscreenSim::OffscreenSim(MujocoContext* ctx) : mujocoContext(ctx) {}


OffscreenSim::~OffscreenSim() {
    if (hiddenWindow) {
        glfwMakeContextCurrent(hiddenWindow);
        glfwDestroyWindow(hiddenWindow);
        hiddenWindow = nullptr;
    }
    if (--liveInstances == 0) {
        glfwTerminate();
    }
}

void OffscreenSim::init(int w, int h){
    width = w;
    height = h;

    if (liveInstances++ == 0) {
        if (!glfwInit()){
            Log::error("Failed to initialize GLFW");
            exit(1);
        }
    }

    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_FALSE);

    hiddenWindow = glfwCreateWindow(MAX_WIDTH, MAX_HEIGHT, "Hidden MuJoCo", nullptr, nullptr);
    if (!hiddenWindow){
        Log::error("Failed to create GLFW hidden window");
        if (--liveInstances == 0) glfwTerminate();
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




QImage OffscreenSim::render(){
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

    ctx->updateScene();
    if (decorHook) decorHook(ctx->getModel(), ctx->getData(), ctx->getScene());
    mjrRect viewport = {0, 0, width, height};
    ctx->render(viewport);

    glPixelStorei(GL_PACK_ALIGNMENT, 1);

    mjr_readPixels(pixelBuffer.data(), nullptr, viewport, ctx->getContext());
    QImage img(pixelBuffer.data(), width, height, width * 3, QImage::Format_RGB888);

    return img.flipped(Qt::Vertical);
}
