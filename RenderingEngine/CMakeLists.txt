cmake_minimum_required(VERSION 3.6)
project(RenderingEngine CXX)

# Use C++11 standard
set(CMAKE_CXX_STANDARD 11)

add_subdirectory(ImageLoading)
add_subdirectory(OpenGL)

set(CMAKE_VERBOSE_MAKEFILE false)
set(CMAKE_POSITION_INDEPENDENT_CODE ON) # -fPIC

set(RENDERINGENGINE_SOURCE_FILES
        Camera.hpp
        StandardRenderer/StandardRenderer.hpp
        Material.hpp
        ObjLoader.hpp
        Renderer.hpp
        RenderingEngine.hpp
        Vertex.hpp
        Geometry.cpp
        Model.cpp
        RenderingEngine.cpp
        Mesh.cpp)

# Main lib
add_library(RenderingEngine STATIC ${RENDERINGENGINE_SOURCE_FILES})

# Add Dependencies
add_dependencies(RenderingEngine ImageLoading)
add_dependencies(RenderingEngine OpenGL)