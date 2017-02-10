cmake_minimum_required(VERSION 3.6)
project(AlphaEngine.RenderingEngine)

# Use C++11 standard
set(CMAKE_CXX_STANDARD 11)

set(RENDERINGENGINE_LIB_NAME RenderingEngine)

include(RenderingEngine/ImageLoading/CMakeLists.txt)
include(RenderingEngine/OpenGL/CMakeLists.txt)

set(RENDERINGENGINE_SOURCE_FILES
        RenderingEngine/Camera.hpp
        RenderingEngine/StandardRenderer/StandardRenderer.hpp
        RenderingEngine/Material.hpp
        RenderingEngine/ObjLoader.hpp
        RenderingEngine/Renderer.hpp
        RenderingEngine/RenderingEngine.hpp
        RenderingEngine/Vertex.hpp
        RenderingEngine/Geometry.cpp
        RenderingEngine/Model.cpp
        RenderingEngine/RenderingEngine.cpp
        RenderingEngine/Mesh.cpp)

# Main lib
add_library(${RENDERINGENGINE_LIB_NAME} STATIC ${RENDERINGENGINE_SOURCE_FILES})

# Add Dependencies
add_dependencies(${RENDERINGENGINE_LIB_NAME} ${IMAGELOADING_LIB_NAME})
target_link_libraries(${RENDERINGENGINE_LIB_NAME} ${LIBRARY_OUTPUT_PATH}/lib${IMAGELOADING_LIB_NAME}.a)

add_dependencies(${RENDERINGENGINE_LIB_NAME} ${OPENGL_LIB_NAME})
target_link_libraries(${RENDERINGENGINE_LIB_NAME} ${LIBRARY_OUTPUT_PATH}/lib${OPENGL_LIB_NAME}.a)
