cmake_minimum_required(VERSION 3.6)
project(AlphaEngine.MediaLayer)

set(MEDIALAYER_LIB_NAME MediaLayer)

set(MEDIALAYER_SOURCE_FILES
        MediaLayer/MediaLayer.cpp
        MediaLayer/Window.cpp)

add_library(${MEDIALAYER_LIB_NAME} STATIC ${MEDIALAYER_SOURCE_FILES})
