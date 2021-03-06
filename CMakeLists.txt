#
# Copyright (c) 2015-2019 Tomislav Radanovic
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
#

# This file defines AlphaEngine build system

cmake_minimum_required(VERSION 3.6)
project(AlphaEngine CXX)

# Debug build or not?
# add_definitions(-D_DEBUG)

# Version
add_definitions(-DVERSION_MAJOR=0)
add_definitions(-DVERSION_MINOR=0)
add_definitions(-DVERSION_PATCH=0)

# Compile options
set(LIBRARY_OUTPUT_PATH ${CMAKE_SOURCE_DIR}/Binaries/)
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_SOURCE_DIR}/Binaries/)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_VERBOSE_MAKEFILE false)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_FLAGS "/O2")

# Vendor options
set(VENDOR_ROOT ${CMAKE_SOURCE_DIR}/Vendor/)
if (APPLE)
    set(VENDOR_LIB lib-macos)
    set(LIB_EXT .framework)
elseif(WIN32)
    set(VENDOR_LIB lib-win)
    set(LIB_EXT .dll)
endif(APPLE)

# Make output directory if it doesn't exist
add_custom_target(build-time-make-directory ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory Binaries)

# Set top level directory to be searched for includes
include_directories(${CMAKE_SOURCE_DIR})

# Main executable
file(GLOB EVENT_ENGINE_FILES "EventEngine/*.hpp" "EventEngine/*.cpp")
file(GLOB INFRASTRUCTURE_FILES "Infrastructure/*.hpp" "Infrastructure/*.cpp")
file(GLOB SCENE_GRAPH_FILES "SceneGraph/*.hpp" "SceneGraph/*.cpp")
file(GLOB RENDERING_ENGINE_FILES
        "RenderingEngine/*.hpp" "RenderingEngine/*.cpp"
        "RenderingEngine/Mesh/*.hpp" "RenderingEngine/Mesh/*.cpp"
        "RenderingEngine/Renderables/*.hpp" "RenderingEngine/Renderables/*.cpp"
        "RenderingEngine/Renderables/Premade2D/*.hpp" "RenderingEngine/Renderables/Premade2D/*.cpp"
        "RenderingEngine/Renderables/Premade3D/*.hpp" "RenderingEngine/Renderables/Premade3D/*.cpp"
        "RenderingEngine/Renderers/*.hpp" "RenderingEngine/Renderers/*.cpp"
        "RenderingEngine/Camera/*.hpp" "RenderingEngine/Camera/*.cpp"
        "RenderingEngine/OpenGL/*.hpp" "RenderingEngine/OpenGL/*.cpp")
file(GLOB EXTERNAL_FILES
        "External/*.hpp" "External/*.cpp"
        "External/API/*.hpp" "External/API/*.cpp")
add_executable(AlphaEngine 
    Control/MainLoop.cpp
    ${EXTERNAL_FILES}
    ${EVENT_ENGINE_FILES}
    ${INFRASTRUCTURE_FILES}
    ${SCENE_GRAPH_FILES}
    ${RENDERING_ENGINE_FILES})

# Find and link to external dependencies
if(WIN32)
	target_link_libraries(AlphaEngine ${VENDOR_ROOT}SDL2/${VENDOR_LIB}/SDL2main.lib)
	target_link_libraries(AlphaEngine ${VENDOR_ROOT}SDL2/${VENDOR_LIB}/SDL2.lib)
elseif(APPLE)
	target_link_libraries(AlphaEngine ${VENDOR_ROOT}SDL2/${VENDOR_LIB}/SDL2.framework)
endif(WIN32)
include_directories(${VENDOR_ROOT}SDL2/include)

include_directories(${VENDOR_ROOT}GLM)
include_directories(${VENDOR_ROOT}Loguru)
include_directories(${VENDOR_ROOT}GLEW/include)
include_directories(${VENDOR_ROOT}SDL2/include)
include_directories(${VENDOR_ROOT}STB/)

if(WIN32)
    target_link_libraries(AlphaEngine ${VENDOR_ROOT}GLEW/${VENDOR_LIB}/glew32.lib)
    target_link_libraries(AlphaEngine ${VENDOR_ROOT}GLEW/${VENDOR_LIB}/glew32s.lib)
elseif(APPLE)
    find_package(GLEW REQUIRED)
    target_link_libraries(AlphaEngine ${GLEW_LIBRARY})
endif(WIN32)

find_package(OpenGL REQUIRED)
target_link_libraries(AlphaEngine ${OPENGL_LIBRARIES})

# Post-build
if(APPLE)
    target_link_libraries(AlphaEngine "-framework CoreFoundation -framework IOKit -framework CoreVideo")
endif(APPLE)

add_custom_command(TARGET AlphaEngine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        "${VENDOR_ROOT}/SDL2/${VENDOR_LIB}/SDL2${LIB_EXT}"
        "${LIBRARY_OUTPUT_PATH}/SDL2${LIB_EXT}")

if(WIN32)
    add_custom_command(TARGET AlphaEngine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
			"${VENDOR_ROOT}/GLEW/${VENDOR_LIB}/glew32.dll"
			"${LIBRARY_OUTPUT_PATH}/glew32.dll")
endif(WIN32)
