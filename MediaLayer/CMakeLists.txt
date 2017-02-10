cmake_minimum_required(VERSION 3.6)
project(MediaLayer CXX)

# Use C++11 standard
set(CMAKE_CXX_STANDARD 11)

set(CMAKE_VERBOSE_MAKEFILE false)
set(CMAKE_POSITION_INDEPENDENT_CODE ON) # -fPIC

set(MEDIALAYER_SOURCE_FILES
        MediaLayer.hpp
        Window.hpp
        Events.hpp
        MediaLayer.cpp
        Window.cpp)

add_library(MediaLayer STATIC ${MEDIALAYER_SOURCE_FILES})
