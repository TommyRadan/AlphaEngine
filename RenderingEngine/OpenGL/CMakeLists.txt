cmake_minimum_required(VERSION 3.6)
project(AlphaEngine.RenderingEngine.OpenGL)

# Use C++11 standard
set(CMAKE_CXX_STANDARD 11)

set(OPENGL_LIB_NAME OpenGL)

include(RenderingEngine/OpenGL/gl3w/CMakeLists.txt)

set(OPENGL_SOURCE_FILES
        RenderingEngine/OpenGL/Framebuffer.cpp
        RenderingEngine/OpenGL/Framebuffer.hpp
        RenderingEngine/OpenGL/OpenGL.cpp
        RenderingEngine/OpenGL/OpenGL.hpp
        RenderingEngine/OpenGL/Program.cpp
        RenderingEngine/OpenGL/Program.hpp
        RenderingEngine/OpenGL/Renderbuffer.cpp
        RenderingEngine/OpenGL/Renderbuffer.hpp
        RenderingEngine/OpenGL/Shader.cpp
        RenderingEngine/OpenGL/Shader.hpp
        RenderingEngine/OpenGL/Texture.cpp
        RenderingEngine/OpenGL/Texture.hpp
        RenderingEngine/OpenGL/Typedef.hpp
        RenderingEngine/OpenGL/VertexArray.cpp
        RenderingEngine/OpenGL/VertexArray.hpp
        RenderingEngine/OpenGL/VertexBuffer.cpp
        RenderingEngine/OpenGL/VertexBuffer.hpp)

add_library(${OPENGL_LIB_NAME} STATIC ${OPENGL_SOURCE_FILES})

# Add Dependencies
add_dependencies(${OPENGL_LIB_NAME} ${GL3W_LIB_NAME})
target_link_libraries(${OPENGL_LIB_NAME} ${LIBRARY_OUTPUT_PATH}/lib${GL3W_LIB_NAME}.a)
