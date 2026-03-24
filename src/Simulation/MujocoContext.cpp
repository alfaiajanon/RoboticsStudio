#include "MujocoContext.h"
#include "Core/Log.h" 




MujocoContext* MujocoContext::getInstance() {
    static MujocoContext instance;
    return &instance;
}




/*
 * Initializes default MuJoCo options, scene, context, and camera.
 * Explicitly enables rendering for site tags and group 3 gizmos.
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
 * Cleans up any existing model and data before allocating the new ones.
 */
void MujocoContext::loadModelFromString(const std::string& xml_content) {
    if(isGPUInitialized){
        mjr_freeContext(&con);
        isGPUInitialized = false;
    }
    char error[1000] = "";

    mjVFS vfs;
    mj_defaultVFS(&vfs);

    mj_addBufferVFS(&vfs, "robot.xml", xml_content.c_str(), xml_content.length());

    if (m) mj_deleteModel(m);
    m = mj_loadXML("robot.xml", &vfs, error, 1000);

    mj_deleteVFS(&vfs);

    if (!m) {
        Log::error(error);
        return;
    }

    if (d) mj_deleteData(d);
    d = mj_makeData(m);
    mj_forward(m, d);
    mjv_makeScene(m, &scn, 2000);
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
 * Initializes the GPU context on the first run.
 */
void MujocoContext::render(mjrRect viewport) {
    if (!m || !d) return;

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