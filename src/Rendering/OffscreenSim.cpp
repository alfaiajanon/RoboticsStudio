#include "OffscreenSim.h"

OffscreenSim::~OffscreenSim() {
    if (hiddenWindow) {
        glfwMakeContextCurrent(hiddenWindow);
        glfwDestroyWindow(hiddenWindow);
        glfwTerminate();
        hiddenWindow = nullptr;
    }
}

void OffscreenSim::init(int w, int h){
    width = w;
    height = h;

    if (!glfwInit()){
        Log::error("Failed to initialize GLFW");
        exit(1);
    }

    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_FALSE);

    hiddenWindow = glfwCreateWindow(MAX_WIDTH, MAX_HEIGHT, "Hidden MuJoCo", nullptr, nullptr);
    if (!hiddenWindow){
        Log::error("Failed to create GLFW hidden window");
        glfwTerminate();
        exit(1);
    }

    glfwMakeContextCurrent(hiddenWindow);

    mujocoContext = MujocoContext::getInstance();
    // mujocoContext->loadModel("../models/servo.xml");

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
    glfwMakeContextCurrent(hiddenWindow);

    mujocoContext->updateScene();
    mjrRect viewport = {0, 0, width, height};
    mujocoContext->render(viewport);

    glPixelStorei(GL_PACK_ALIGNMENT, 1);

    mjr_readPixels(pixelBuffer.data(), nullptr, viewport, mujocoContext->getContext());
    // QImage img(pixelBuffer.data(), width, height, QImage::Format_RGB888);
    QImage img(pixelBuffer.data(), width, height, width * 3, QImage::Format_RGB888);

    return img.flipped(Qt::Vertical);
}