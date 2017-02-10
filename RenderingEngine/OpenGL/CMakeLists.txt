cmake_minimum_required(VERSION 3.6)
project(OpenGL)

# Use C++11 standard
set(CMAKE_CXX_STANDARD 11)

add_subdirectory(gl3w)

set(CMAKE_VERBOSE_MAKEFILE false)
set(CMAKE_POSITION_INDEPENDENT_CODE ON) # -fPIC

set(OPENGL_SOURCE_FILES
        Framebuffer.cpp
        Framebuffer.hpp
        OpenGL.cpp
        OpenGL.hpp
        Program.cpp
        Program.hpp
        Renderbuffer.cpp
        Renderbuffer.hpp
        Shader.cpp
        Shader.hpp
        Texture.cpp
        Texture.hpp
        Typedef.hpp
        VertexArray.cpp
        VertexArray.hpp
        VertexBuffer.cpp
        VertexBuffer.hpp)

add_library(OpenGL STATIC ${OPENGL_SOURCE_FILES})

# Add Dependencies
add_dependencies(OpenGL gl3w)
