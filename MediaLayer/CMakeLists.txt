cmake_minimum_required(VERSION 3.6)
project(AlphaEngine.MediaLayer)

# Use C++11 standard
set(CMAKE_CXX_STANDARD 11)

set(MEDIALAYER_LIB_NAME MediaLayer)

set(MEDIALAYER_SOURCE_FILES
        MediaLayer/MediaLayer.hpp
        MediaLayer/Window.hpp
        MediaLayer/Events.hpp
        MediaLayer/MediaLayer.cpp
        MediaLayer/Window.cpp)

add_library(${MEDIALAYER_LIB_NAME} STATIC ${MEDIALAYER_SOURCE_FILES})
