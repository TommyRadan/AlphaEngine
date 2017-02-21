cmake_minimum_required(VERSION 3.6)
project(RenderingEngine CXX)

# Use C++11 standard
set(CMAKE_CXX_STANDARD 11)

add_subdirectory(ImageLoading)
add_subdirectory(OpenGL)

set(CMAKE_VERBOSE_MAKEFILE false)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(RENDERINGENGINE_SOURCE_FILES
        Renderers/StandardRenderer/StandardRenderer.hpp
        Renderers/StandardRenderer/StandardRenderer.cpp
        Renderers/Renderer.hpp
        Renderers/Renderer.cpp
        MeshLoading/ObjLoader.hpp
        MeshLoading/Vertex.hpp
        MeshLoading/Mesh.cpp
        Camera.hpp
        Material.hpp
        RenderingEngine.hpp
        RenderingEngine.cpp
        Geometry.cpp
        Model.cpp)

# Main lib
add_library(RenderingEngine STATIC ${RENDERINGENGINE_SOURCE_FILES})

# Add Dependencies
add_dependencies(RenderingEngine ImageLoading)
add_dependencies(RenderingEngine OpenGL)
