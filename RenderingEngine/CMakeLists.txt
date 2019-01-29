cmake_minimum_required(VERSION 3.6)
project(RenderingEngine CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_VERBOSE_MAKEFILE false)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(VENDOR_ROOT ${CMAKE_SOURCE_DIR}/Vendor/)

# Define target sources
file(GLOB RENDERINGENGINE_SOURCE_FILES
        "*.hpp" "*.cpp"
        "Mesh/*.hpp" "Mesh/*.cpp"
        "Renderables/*.hpp" "Renderables/*.cpp"
        "Renderers/*.hpp" "Renderers/*.cpp"
        "OpenGL/*.hpp" "OpenGL/*.cpp")

# External dependencies
include_directories(${VENDOR_ROOT}SDL2/include)
include_directories(${VENDOR_ROOT}GLM/)
include_directories(${VENDOR_ROOT}Loguru/)
include_directories(${VENDOR_ROOT}GLEW/include)

# Main lib
add_library(RenderingEngine STATIC ${RENDERINGENGINE_SOURCE_FILES})
set_target_properties(RenderingEngine PROPERTIES LINKER_LANGUAGE CXX)
