cmake_minimum_required(VERSION 3.1.3 FATAL_ERROR)
project(gl3w)

set(GL3W_LIB_NAME gl3w)

set(CMAKE_VERBOSE_MAKEFILE false)
set(CMAKE_POSITION_INDEPENDENT_CODE ON) # -fPIC

set(SOURCE_FILES
		RenderingEngine/OpenGL/gl3w/gl3w.c
		RenderingEngine/OpenGL/gl3w/gl3w.h
		RenderingEngine/OpenGL/gl3w/glcorearb.h
)

# add and depend on OpenGL
find_package(OpenGL REQUIRED)
set(EXTERNAL_INCLUDE_DIRS ${EXTERNAL_INCLUDE_DIRS} ${OPENGL_INCLUDE_DIR})
set(EXTERNAL_LIBRARIES ${EXTERNAL_LIBRARIES} ${OPENGL_LIBRARIES})

# create gl3w target
add_library(${CMAKE_PROJECT_NAME} STATIC ${SOURCE_FILES})
