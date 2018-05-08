cmake_minimum_required(VERSION 3.6)
project(RenderingEngine CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_VERBOSE_MAKEFILE false)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Define target sources
file(GLOB RENDERINGENGINE_SOURCE_FILES
        "*.hpp" "*.cpp"
        "Mesh/*.hpp" "Mesh/*.cpp"
        "Renderables/*.hpp" "Renderables/*.cpp"
        "Renderers/*.hpp" "Renderers/*.cpp"
        "OpenGL/*.hpp" "OpenGL/*.cpp")

# External dependencies
find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIR})

find_package(GLEW REQUIRED)
include_directories(${GLEW_INCLUDE_DIR})

# Main lib
add_library(RenderingEngine STATIC ${RENDERINGENGINE_SOURCE_FILES})
set_target_properties(RenderingEngine PROPERTIES LINKER_LANGUAGE CXX)
