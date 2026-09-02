#pragma once

#include <QOpenGLFramebufferObject>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <GLFW/glfw3.h>
#include <QImage>
#include <iostream>
#include <vector>
#include <functional>

#include "mujoco/mujoco.h"
#include "Utils/Log.h"


class MujocoContext;



using namespace std;

/*
 * Offscreen MuJoCo renderer backed by a hidden GLFW window.
 *
 * ctx == nullptr -> renders the app's simulation context, resolved lazily
 *                   via Application::getSimulationManager() at render time
 *                   (a SimulationManager doesn't exist yet when the main
 *                   viewport is constructed).
 * ctx != nullptr -> renders that context (used by standalone viewers like
 *                   the component editor preview, which own their context).
 *
 * GLFW init/terminate is process-global, so it's reference-counted across
 * all OffscreenSim instances -- terminating on every destruction would kill
 * GLFW out from under any other live viewport.
 */
class OffscreenSim {
    private:
        MujocoContext* mujocoContext;
        GLFWwindow* hiddenWindow = nullptr;
        vector<unsigned char> pixelBuffer;
        int width;
        int height;
        const int MAX_WIDTH = 3000;
        const int MAX_HEIGHT = 3000;


    public:

        explicit OffscreenSim(MujocoContext* ctx = nullptr);
        ~OffscreenSim();

        void init(int w, int h);
        void setSize(int w, int h);

        QImage render();

        // Optional hook invoked every frame between mjv_updateScene and
        // mjr_render -- lets a viewer append decor mjvGeoms (visualization
        // overlays) to the scene. Null by default; the main-app viewports
        // never set it.
        std::function<void(mjModel*, mjData*, mjvScene*)> decorHook;
};
