#pragma once

#include <QOpenGLFramebufferObject>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <GLFW/glfw3.h>
#include <QImage>
#include <iostream>
#include <vector>

#include "mujoco/mujoco.h"
#include "Utils/Log.h"


class MujocoContext;



using namespace std;

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

        ~OffscreenSim();

        void init(int w, int h);
        void setSize(int w, int h);

        QImage render();
};




