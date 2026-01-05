cmake_minimum_required(VERSION 3.10)
project(RoboticsStudio)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)


set(CMAKE_CXX_STANDARD 17)
set(MUJOCO_DIR "${CMAKE_SOURCE_DIR}/extern/mujoco-3.3.7")

find_package(Qt6 COMPONENTS Xml Widgets OpenGL OpenGLWidgets REQUIRED)

include_directories(${MUJOCO_DIR}/include)
link_directories(${MUJOCO_DIR}/lib)


# will remove later
set(IMGUI_SRC
    extern/imgui/imgui.cpp
    extern/imgui/imgui_draw.cpp
    extern/imgui/imgui_tables.cpp
    extern/imgui/imgui_widgets.cpp
    extern/imgui_backends/imgui_impl_glfw.cpp
    extern/imgui_backends/imgui_impl_opengl3.cpp
)

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
add_executable(RoboticsStudio ${SRC_FILES} ${IMGUI_SRC})

target_link_libraries(RoboticsStudio 
    Qt6::Widgets 
    Qt6::Xml
    Qt6::OpenGL 
    Qt6::OpenGLWidgets 
    mujoco 
    glfw 
    GL 
    dl 
    pthread 
    m
)
target_include_directories(RoboticsStudio PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/extern/imgui
    ${CMAKE_SOURCE_DIR}/extern/imgui_backends
)

