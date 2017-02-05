cmake_minimum_required(VERSION 3.6)
project(AlphaEngine.Control)

set(CONTROL_LIB_NAME Control)

set(CONTROL_SOURCE_FILES
        Control/MainLoop.cpp)

add_library(${CONTROL_LIB_NAME} STATIC ${CONTROL_SOURCE_FILES})
