cmake_minimum_required(VERSION 3.6)
project(Control CXX)

# Use C++11 standard
set(CMAKE_CXX_STANDARD 11)

set(CMAKE_VERBOSE_MAKEFILE false)
set(CMAKE_POSITION_INDEPENDENT_CODE ON) # -fPIC

set(CONTROL_SOURCE_FILES
        MainLoop.cpp
        Settings.hpp)

add_library(Control STATIC ${CONTROL_SOURCE_FILES})
