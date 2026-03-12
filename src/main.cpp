#include "Core/Application.h"


int main(int argc, char *argv[]) {
    Application::init(argc, argv);
    
    int exitCode = Application::getInstance()->run();
    Application::destroy();
    
    return exitCode;
}







// #include <GLFW/glfw3.h>
// #include <mujoco/mujoco.h>
// #include <iostream>

// /*
//  * Raw MuJoCo simulation without any custom wrappers.
//  * Loads the XML, initializes OpenGL, steps physics, and renders to a GLFW window.
//  */




// int main() {
//     char error[1000] = "Could not load XML model";
//     mjModel* m = mj_loadXML("/home/anon/Documents/Code Projects/Mixed Projects/RoboticsStudio/test.xml", 0, error, 1000);
    
//     if (!m) {
//         std::cerr << error << std::endl;
//         return 1;
//     }

//     mjData* d = mj_makeData(m);
//     mj_forward(m, d);

//     if (!glfwInit()) {
//         return 1;
//     }
    
//     GLFWwindow* window = glfwCreateWindow(1024, 768, "Pure MuJoCo", NULL, NULL);
//     if (!window) {
//         glfwTerminate();
//         return 1;
//     }
    
//     glfwMakeContextCurrent(window);
//     glfwSwapInterval(1);

//     mjvCamera cam;
//     mjvOption opt;
//     mjvScene scn;
//     mjrContext con;
    
//     mjv_defaultCamera(&cam);
//     mjv_defaultOption(&opt);
//     mjv_defaultScene(&scn);
//     mjr_defaultContext(&con);

//     mjv_makeScene(m, &scn, 2000);
//     mjr_makeContext(m, &con, mjFONTSCALE_150);

//     cam.type = mjCAMERA_FREE;
//     cam.distance = 0.8;
//     cam.elevation = -45.0;
//     cam.azimuth = 90.0;
//     cam.lookat[0] = 0.0;
//     cam.lookat[1] = 0.0;
//     cam.lookat[2] = 0.0;

//     d->ctrl[0] = 1.0;

//     while (!glfwWindowShouldClose(window)) {
//         for (int i = 0; i < 8; ++i) {
//             mj_step(m, d);
//         }

//         std::cout << "qpos[m->jnt_qposadr[0]]: " << d->qpos[m->jnt_qposadr[0]] << std::endl;

//         mjv_updateScene(m, d, &opt, NULL, &cam, mjCAT_ALL, &scn);

//         int w, h;
//         glfwGetFramebufferSize(window, &w, &h);
//         mjrRect viewport = {0, 0, w, h};
        
//         mjr_render(viewport, &scn, &con);

//         glfwSwapBuffers(window);
//         glfwPollEvents();
//     }

//     mjv_freeScene(&scn);
//     mjr_freeContext(&con);
//     mj_deleteData(d);
//     mj_deleteModel(m);
//     glfwTerminate();

//     return 0;
// }