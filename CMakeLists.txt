#
# Copyright (c) 2015-2025 Tomislav Radanovic
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

cmake_minimum_required(VERSION 3.16)
project(AlphaEngine CXX)

# Set build type to Debug if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# Add debug definitions and flags
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_definitions(-D_DEBUG)
    if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Zi /Od")
    else()
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O0")
    endif()
endif()

# Set cmake module directory
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# Version
add_definitions(-DVERSION_MAJOR=0)
add_definitions(-DVERSION_MINOR=0)
add_definitions(-DVERSION_PATCH=1)

# Set the macOS SDK path
if(APPLE)
    execute_process(COMMAND 
        xcrun --show-sdk-path OUTPUT_VARIABLE MACOS_SDK_PATH 
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    set(CMAKE_OSX_SYSROOT ${MACOS_SDK_PATH})
endif(APPLE)

# Compile options
set(LIBRARY_OUTPUT_PATH ${CMAKE_SOURCE_DIR}/Binaries/)
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_SOURCE_DIR}/Binaries/)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_VERBOSE_MAKEFILE false)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Vendor options
set(VENDOR_ROOT ${CMAKE_SOURCE_DIR}/Vendor/)

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
        "RenderingEngine/OpenGL/*.hpp" "RenderingEngine/OpenGL/*.cpp"
        "RenderingEngine/Util/*.hpp" "RenderingEngine/Util/*.cpp")
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

# Use C++ standard library
target_compile_options(AlphaEngine PRIVATE -stdlib=libc++)

# Find and link to external dependencies
find_package(SDL2 REQUIRED)
target_link_libraries(AlphaEngine SDL2::SDL2)
target_include_directories(AlphaEngine PRIVATE ${SDL2_INCLUDE_DIRS})

find_package(GLM REQUIRED)
target_include_directories(AlphaEngine PRIVATE ${GLM_INCLUDE_DIR})

target_include_directories(AlphaEngine PRIVATE ${VENDOR_ROOT}Loguru)
target_include_directories(AlphaEngine PRIVATE ${VENDOR_ROOT}STB/)

find_package(GLEW REQUIRED)
target_link_libraries(AlphaEngine GLEW::GLEW)
#target_include_directories(AlphaEngine PRIVATE ${GLEW_INCLUDE_DIRS})

find_package(OpenGL REQUIRED)
target_link_libraries(AlphaEngine ${OPENGL_LIBRARIES})

# Post-build
if(APPLE)
    target_link_libraries(AlphaEngine "-framework CoreFoundation -framework IOKit -framework CoreVideo")
endif(APPLE)

#if(APPLE)
#    add_custom_command(TARGET AlphaEngine POST_BUILD
#        COMMAND ${CMAKE_COMMAND} -E copy ${SDL2_LIBRARIES} ${LIBRARY_OUTPUT_PATH}
#        COMMAND ${CMAKE_COMMAND} -E copy ${OPENGL_LIBRARIES} ${LIBRARY_OUTPUT_PATH})
#endif(APPLE)
