#include "MujocoContext.h"


MujocoContext* MujocoContext::getInstance() {
    static MujocoContext instance;
    return &instance;
}


MujocoContext::MujocoContext(){
    mjv_defaultOption(&opt);
    mjv_defaultScene(&scn);
    mjr_defaultContext(&con);
    mjv_defaultCamera(&cam);
}

void MujocoContext::loadModel(const char* model_path){
    char error[1000];
    
    m = mj_loadXML(model_path, nullptr, error, sizeof(error));
    if (!m){
        Log::error(error);
        return;
    }
    d = mj_makeData(m);
    mjv_makeScene(m, &scn, 2000);
    Log::info("MuJoCo model loaded successfully.");
    // mjr_makeContext(m, &con, mjFONTSCALE_150);
}


void MujocoContext::loadModelFromString(const std::string& xml_content) {
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
    mjv_makeScene(m, &scn, 2000);
}




MujocoContext::~MujocoContext(){
    mj_deleteData(d);
    mj_deleteModel(m);
    mjv_freeScene(&scn);
}


mjModel* MujocoContext::getModel(){
    return m;
}

mjData* MujocoContext::getData(){
    return d;
}

mjvScene* MujocoContext::getScene(){
    return &scn;
}

mjvOption* MujocoContext::getOption(){
    return &opt;
}

mjrContext* MujocoContext::getContext(){
    return &con;
}

mjvCamera* MujocoContext::getCamera(){
    return &cam;
}


void MujocoContext::step(){
    mj_step(m, d);
}


void MujocoContext::updateScene(){
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

void MujocoContext::render(mjrRect viewport){

    if (!isGPUInitialized){
        isGPUInitialized = true;
        mjr_makeContext(m, &con, mjFONTSCALE_150);
    }
    mjr_render(viewport, &scn, &con);
}

void MujocoContext::setControl(int index, double value){
    d->ctrl[index] = value;
}











// #include "MujocoContext.h"
// #include <iostream>

// MujocoContext* MujocoContext::getInstance() {
//     static MujocoContext instance;
//     return &instance;
// }

// MujocoContext::MujocoContext(){
//     // Initialize pointers to nullptr for safety
//     m = nullptr;
//     d = nullptr;
// }

// void MujocoContext::loadModel(const char* model_path){
//     char error[1000];

//     // Cleanup existing model
//     if (m) {
//         mj_deleteData(d);
//         mj_deleteModel(m);
//     }

//     // Load Model (CPU Physics only)
//     m = mj_loadXML(model_path, nullptr, error, sizeof(error));
//     if (!m){
//         std::cerr << "MuJoCo Load Error: " << error << std::endl;
//         return;
//     }
//     d = mj_makeData(m);
// }

// MujocoContext::~MujocoContext(){
//     if (d) mj_deleteData(d);
//     if (m) mj_deleteModel(m);
// }

// // Getters
// mjModel* MujocoContext::getModel(){ return m; }
// mjData* MujocoContext::getData(){ return d; }

// void MujocoContext::step(){
//     if (m && d) mj_step(m, d);
// }