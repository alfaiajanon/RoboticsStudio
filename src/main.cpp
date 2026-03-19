#include "Core/Application.h"


int main(int argc, char *argv[]) {
    Application::init(argc, argv);
    
    int exitCode = Application::getInstance()->run();
    Application::destroy();
    
    return exitCode;
}



