#include "MujocoContext.h"

#include "Utils/Log.h" 



/*
 * Initializes default MuJoCo options, scene, context, and camera.
 * Explicitly enables rendering for site tags and group 3 gizmos.
 *
 * No longer a singleton: SimulationManager owns the app's instance, and
 * auxiliary viewers (e.g. the component editor preview) own their own --
 * sharing one model across an editor and a live simulation meant an
 * editor reload would mj_deleteModel the running sim out from under the
 * physics thread.
 */
MujocoContext::MujocoContext() {
    mjv_defaultOption(&opt);
    mjv_defaultScene(&scn);
    mjr_defaultContext(&con);
    mjv_defaultCamera(&cam);

    opt.sitegroup[0] = 0;
    opt.sitegroup[1] = 0;
    opt.sitegroup[2] = 0; 
    opt.sitegroup[3] = 0;
}




/*
 * Loads a MuJoCo model from a file path.
 * Initializes the physics data and generates the visualization scene.
 */
void MujocoContext::loadModel(const char* model_path) {
    char error[1000];
    
    m = mj_loadXML(model_path, nullptr, error, sizeof(error));
    if (!m) {
        Log::error(error);
        return;
    }
    d = mj_makeData(m);
    mjv_makeScene(m, &scn, 2000);
    Log::info("MuJoCo model loaded successfully.");
}




/*
 * Loads a MuJoCo model directly from a raw XML string using a Virtual File System.
 * Swap-on-success: the new model is fully parsed before the old one is
 * released, so a failed load (e.g. a component editor's invalid mid-edit
 * state) leaves the previous model on screen instead of a null context.
 * Returns false when the XML failed to load.
 */
bool MujocoContext::loadModelFromString(const std::string& xml_content) {
    char error[1000] = "";

    mjVFS vfs;
    mj_defaultVFS(&vfs);

    mj_addBufferVFS(&vfs, "robot.xml", xml_content.c_str(), xml_content.length());

    mjModel* newModel = mj_loadXML("robot.xml", &vfs, error, 1000);

    mj_deleteVFS(&vfs);

    if (!newModel) {
        Log::error(error);
        return false;
    }

    // Don't mjr_freeContext here -- see pendingContextFree in the header.
    if (isGPUInitialized) {
        pendingContextFree = true;
    }

    if (m) mj_deleteModel(m);
    m = newModel;

    if (d) mj_deleteData(d);
    d = mj_makeData(m);
    mj_forward(m, d);
    mjv_makeScene(m, &scn, 2000);
    return true;
}





MujocoContext::~MujocoContext() {
    if (d) mj_deleteData(d);
    if (m) mj_deleteModel(m);
    mjv_freeScene(&scn);
}




mjModel* MujocoContext::getModel() {
    return m;
}




mjData* MujocoContext::getData() {
    return d;
}




mjvScene* MujocoContext::getScene() {
    return &scn;
}




mjvOption* MujocoContext::getOption() {
    return &opt;
}




mjrContext* MujocoContext::getContext() {
    return &con;
}




mjvCamera* MujocoContext::getCamera() {
    return &cam;
}




/*
 * Advances the physics simulation by one single timestep.
 */
void MujocoContext::step() {
    if (m && d) {
        mj_step(m, d);
    }
}




void MujocoContext::forward() {
    if (m && d) {
        mj_forward(m, d);
    }
}



/*
 * Updates the abstract scene based on the current physics state.
 * Called prior to rendering to update geometry positions.
 */
void MujocoContext::updateScene() {
    if (!m || !d) return;

    mjv_updateScene(
        m,
        d,
        &opt,
        nullptr,
        &cam,
        mjCAT_ALL,
        &scn
    );
}




/*
 * Renders the updated scene to the specified OpenGL viewport.
 * Initializes the GPU context on the first run, and performs any
 * context free deferred by loadModelFromString -- at this point the
 * caller (OffscreenSim::render) has made this context's own GLFW
 * window current, so GL deletes hit the right context.
 */
void MujocoContext::render(mjrRect viewport) {
    if (!m || !d) return;

    if (pendingContextFree) {
        pendingContextFree = false;
        if (isGPUInitialized) {
            mjr_freeContext(&con);
            isGPUInitialized = false;
        }
    }

    if (!isGPUInitialized) {
        isGPUInitialized = true;
        mjr_makeContext(m, &con, mjFONTSCALE_150);
    }
    mjr_render(viewport, &scn, &con);
}




void MujocoContext::setControl(int index, double value) {
    if (d && index >= 0 && index < m->nu) {
        d->ctrl[index] = value;
    }
}