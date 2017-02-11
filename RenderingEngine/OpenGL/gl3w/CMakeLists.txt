cmake_minimum_required(VERSION 3.6)
project(gl3w)

set(CMAKE_VERBOSE_MAKEFILE false)
set(CMAKE_POSITION_INDEPENDENT_CODE ON) # -fPIC

set(GL3W_SOURCE_FILES
		gl3w.c
		gl3w.h
		glcorearb.h)

# Add Library
add_library(gl3w STATIC ${GL3W_SOURCE_FILES})
