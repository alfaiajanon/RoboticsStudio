#pragma once

#include <functional>
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
        // World render flags (fog, shadows, ...) for every scene drawn from
        // this model: reapplied to scn after each mjv_makeScene (which resets
        // them) and copied into other renderers' scenes via applySceneFlags.
        // -1 = never set: keep the scene's own value (mjv_makeScene's
        // defaults; mjv_defaultScene zeroes them all, so don't copy those).
        signed char sceneFlags[mjNRNDFLAG];

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

        // Optional: run by render() with the GL context current, after the
        // mjrContext is (re)built and before mjr_render -- e.g. to upload
        // live display textures. Null by default.
        std::function<void(const mjrContext*)> beforeRender;

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
        void setSceneFlag(mjtRndFlag flag, bool enabled);
        void applySceneFlags(mjvScene* scene) const;
        mjrContext* getContext();
        mjvCamera* getCamera();
        
        ~MujocoContext();
};