#!/bin/bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build -j4 
export QT_QPA_PLATFORM=xcb 
./build/RoboticsStudio