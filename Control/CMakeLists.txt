cmake_minimum_required(VERSION 3.6)
project(AlphaEngine.Control)

# Use C++11 standard
set(CMAKE_CXX_STANDARD 11)

set(CONTROL_LIB_NAME Control)

set(CONTROL_SOURCE_FILES
        Control/MainLoop.cpp
        Control/Settings.hpp)

add_library(${CONTROL_LIB_NAME} STATIC ${CONTROL_SOURCE_FILES})
