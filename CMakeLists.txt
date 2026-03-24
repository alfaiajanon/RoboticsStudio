cmake_minimum_required(VERSION 3.10)
project(RoboticsStudio)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)


set(CMAKE_CXX_STANDARD 17)
set(MUJOCO_DIR "${CMAKE_SOURCE_DIR}/extern/mujoco-3.3.7")

find_package(Qt6 COMPONENTS Xml Widgets OpenGL OpenGLWidgets Qml PrintSupport REQUIRED)

include_directories(${MUJOCO_DIR}/include)
link_directories(${MUJOCO_DIR}/lib)


file(GLOB_RECURSE SRC_FILES 
    src/**/**/*.cpp 
    src/**/*.cpp 
    src/*.cpp 
    src/**/**/*.h 
    src/**/*.h 
    src/*.h
    src/UI/Resources/resources.qrc
    models/resources.qrc
)
add_executable(RoboticsStudio ${SRC_FILES})

target_link_libraries(RoboticsStudio 
    Qt6::Widgets 
    Qt6::Xml
    Qt6::OpenGL 
    Qt6::OpenGLWidgets 
    Qt6::PrintSupport
    Qt6::Qml
    mujoco 
    glfw 
    GL 
    dl 
    pthread 
    m
)
target_include_directories(RoboticsStudio PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

