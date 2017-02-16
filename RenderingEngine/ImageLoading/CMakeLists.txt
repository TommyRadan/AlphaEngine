cmake_minimum_required(VERSION 3.6)
project(ImageLoading)

set(CMAKE_VERBOSE_MAKEFILE false)
set(CMAKE_POSITION_INDEPENDENT_CODE ON) # -fPIC

set(IMAGELOADING_SOURCE_FILES
        Image.cpp
        Image.hpp
        stb_image.c
        stb_image.h)

# Main lib
add_library(ImageLoading STATIC ${IMAGELOADING_SOURCE_FILES})
