#include "GlfwRuntime.h"

#include <GLFW/glfw3.h>
#include "Utils/Log.h"


// Number of live references to the process-global GLFW state.
static int liveRefs = 0;


bool GlfwRuntime::acquire() {
    if (liveRefs == 0 && !glfwInit()) {
        Log::error("Failed to initialize GLFW");
        return false;
    }
    ++liveRefs;
    return true;
}


void GlfwRuntime::release() {
    if (liveRefs > 0 && --liveRefs == 0) {
        glfwTerminate();
    }
}
