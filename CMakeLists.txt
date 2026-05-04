cmake_minimum_required(VERSION 3.10)
project(RoboticsStudio)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(CMAKE_CXX_STANDARD 17)
set(MUJOCO_DIR "${CMAKE_SOURCE_DIR}/extern/mujoco-3.3.7")

set(APP_VERSION "0.1.0-alpha")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Core/Version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/Version.h"
)


find_package(Qt6 COMPONENTS Xml Widgets OpenGL OpenGLWidgets Qml PrintSupport Network REQUIRED)

file(GLOB_RECURSE SRC_FILES 
    src/**/**/*.cpp 
    src/**/*.cpp 
    src/*.cpp 
    src/**/**/*.h 
    src/**/*.h 
    src/*.h
    src/UI/Resources/resources.qrc
    extern/qcustomplot/*.h
    extern/qcustomplot/*.cpp
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
    Qt6::Network
    mujoco 
    glfw 
    GL 
    dl 
    pthread 
    m
)

target_link_directories(RoboticsStudio PRIVATE ${MUJOCO_DIR}/lib)

target_include_directories(RoboticsStudio PRIVATE
    ${MUJOCO_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/extern/qcustomplot
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)

