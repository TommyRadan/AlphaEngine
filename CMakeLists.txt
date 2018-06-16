# This file defines AlphaEngine build system

cmake_minimum_required(VERSION 3.6)
project(AlphaEngine CXX)

# Debug build or not?
add_definitions(-D_DEBUG)

# Version
add_definitions(-DVERSION_MAJOR=0)
add_definitions(-DVERSION_MINOR=0)
add_definitions(-DVERSION_PATCH=0)

# Compile options
set(LIBRARY_OUTPUT_PATH ${CMAKE_SOURCE_DIR}/Binaries/)
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_SOURCE_DIR}/Binaries/)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_VERBOSE_MAKEFILE false)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Set top level directory to be searched for includes
include_directories(${CMAKE_SOURCE_DIR})

# Include subprojects
add_subdirectory(EventEngine)
add_subdirectory(Infrastructure)
add_subdirectory(RenderingEngine)

# Main shared lib
#add_library(AlphaEngine SHARED Control/MainLoop.cpp)
add_executable(AlphaEngine Control/MainLoop.cpp)

# Add Dependencies
add_dependencies(AlphaEngine EventEngine)
add_dependencies(AlphaEngine Infrastructure)
add_dependencies(AlphaEngine RenderingEngine)

# Link Libraries
target_link_libraries(AlphaEngine ${LIBRARY_OUTPUT_PATH}libInfrastructure.a)
target_link_libraries(AlphaEngine ${LIBRARY_OUTPUT_PATH}libEventEngine.a)
target_link_libraries(AlphaEngine ${LIBRARY_OUTPUT_PATH}libRenderingEngine.a)

# Find and link to external dependencies
find_package(SDL2 REQUIRED)
target_link_libraries(AlphaEngine "-lSDL2")
include_directories(${SDL2_INCLUDE_DIR})

find_package(GLEW REQUIRED)
target_link_libraries(AlphaEngine ${GLEW_LIBRARY})
include_directories(${GLEW_INCLUDE_DIR})

find_package(OpenGL REQUIRED)
target_link_libraries(AlphaEngine ${OPENGL_LIBRARIES})

if(APPLE)
    target_link_libraries(AlphaEngine "-framework CoreFoundation -framework IOKit -framework CoreVideo")
endif(APPLE)
