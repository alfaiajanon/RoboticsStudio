#pragma once


/*
 * GLFW init/terminate is process-global, so it's reference-counted across
 * every user of hidden GLFW windows (viewports' OffscreenSim, the camera
 * renderer) -- terminating on each release would kill GLFW out from under
 * the other live windows. Main (UI) thread only, like glfwInit itself.
 */
namespace GlfwRuntime {
    // Initializes GLFW on the first acquire. Returns false if glfwInit failed
    // (the reference is not taken then).
    bool acquire();
    // Terminates GLFW when the last reference is released.
    void release();
}
