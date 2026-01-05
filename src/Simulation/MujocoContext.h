#pragma once

#include "mujoco/mujoco.h"
#include "Core/Log.h"

class MujocoContext {
    private:
        mjModel* m = nullptr;
        mjData* d = nullptr;
        mjvScene scn;
        mjvOption opt;
        mjrContext con;
        mjvCamera cam;

        bool isGPUInitialized = false;

        MujocoContext();

    public:
        static MujocoContext* getInstance();

        void loadModel(const char* model_path);
        void loadModelFromString(const std::string& model_xml);
        void setControl(int index, double value);
        void render(mjrRect viewport);
        void updateScene();
        void step();

        mjModel* getModel();
        mjData* getData();
        mjvScene* getScene();
        mjvOption* getOption();
        mjrContext* getContext();
        mjvCamera* getCamera();
        
        ~MujocoContext();
};