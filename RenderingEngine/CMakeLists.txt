cmake_minimum_required(VERSION 3.6)
project(AlphaEngine.RenderingEngine)

set(RENDERINGENGINE_LIB_NAME RenderingEngine)

include(RenderingEngine/libjpeg/CMakeLists.txt)
include(RenderingEngine/libpng/CMakeLists.txt)
include(RenderingEngine/zlib/CMakeLists.txt)
include(RenderingEngine/OpenGL/CMakeLists.txt)

set(RENDERINGENGINE_SOURCE_FILES
        RenderingEngine/Geometry.cpp
        RenderingEngine/Model.cpp
        RenderingEngine/RenderingEngine.cpp
        RenderingEngine/Image.cpp
        RenderingEngine/Mesh.cpp)

# Main lib
add_library(${RENDERINGENGINE_LIB_NAME} STATIC ${RENDERINGENGINE_SOURCE_FILES})

# Add Dependencies
add_dependencies(${RENDERINGENGINE_LIB_NAME} ${JPEG_LIB_NAME})
target_link_libraries(${RENDERINGENGINE_LIB_NAME} ${LIBRARY_OUTPUT_PATH}/lib${JPEG_LIB_NAME}.a)

add_dependencies(${RENDERINGENGINE_LIB_NAME} ${PNG_LIB_NAME})
target_link_libraries(${RENDERINGENGINE_LIB_NAME} ${LIBRARY_OUTPUT_PATH}/lib${PNG_LIB_NAME}.a)

add_dependencies(${RENDERINGENGINE_LIB_NAME} ${ZLIB_LIB_NAME})
target_link_libraries(${RENDERINGENGINE_LIB_NAME} ${LIBRARY_OUTPUT_PATH}/lib${ZLIB_LIB_NAME}.a)

add_dependencies(${RENDERINGENGINE_LIB_NAME} ${OPENGL_LIB_NAME})
target_link_libraries(${RENDERINGENGINE_LIB_NAME} ${LIBRARY_OUTPUT_PATH}/lib${OPENGL_LIB_NAME}.a)
