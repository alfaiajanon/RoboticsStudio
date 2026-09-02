#pragma once

#include "mujoco/mujoco.h"
#include "Utils/Log.h"

class MujocoContext {
    private:
        mjModel* m = nullptr;
        mjData* d = nullptr;
        mjvScene scn;
        mjvOption opt;
        mjrContext con;
        mjvCamera cam;

        bool isGPUInitialized = false;
        // Set by loadModelFromString instead of freeing the mjrContext
        // immediately: GL object IDs are only valid in the GL context that
        // created them, and at load time the WRONG GLFW window may be
        // current (e.g. the main viewport's, while the component preview
        // reloads) -- freeing then would delete the other viewer's GPU
        // resources. render() performs the free once the owning window is
        // current (OffscreenSim always makes it current before rendering).
        bool pendingContextFree = false;

    public:
        MujocoContext();

        void loadModel(const char* model_path);
        bool loadModelFromString(const std::string& model_xml);
        void setControl(int index, double value);
        void render(mjrRect viewport);
        void updateScene();
        void step();
        void forward();

        mjModel* getModel();
        mjData* getData();
        mjvScene* getScene();
        mjvOption* getOption();
        mjrContext* getContext();
        mjvCamera* getCamera();
        
        ~MujocoContext();
};