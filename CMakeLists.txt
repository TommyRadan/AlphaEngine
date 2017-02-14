# This file defines AlphaEngine build system

cmake_minimum_required(VERSION 3.6)
project(AlphaEngine)

# Define different target directories based on OS
if (WIN32)
    set(LIBRARY_OUTPUT_PATH ${CMAKE_SOURCE_DIR}/Binaries/Windows/)
    set(EXECUTABLE_OUTPUT_PATH ${CMAKE_SOURCE_DIR}/Binaries/Windows/)
elseif (APPLE)
    set(LIBRARY_OUTPUT_PATH ${CMAKE_SOURCE_DIR}/Binaries/Apple/)
    set(EXECUTABLE_OUTPUT_PATH ${CMAKE_SOURCE_DIR}/Binaries/Apple/)
elseif (UNIX)
    set(LIBRARY_OUTPUT_PATH ${CMAKE_SOURCE_DIR}/Binaries/Unix/)
    set(EXECUTABLE_OUTPUT_PATH ${CMAKE_SOURCE_DIR}/Binaries/Unix/)
endif ()

set(CMAKE_VERBOSE_MAKEFILE false)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Set top level directory to be searched for includes
include_directories(${CMAKE_SOURCE_DIR})

# Include subprojects
add_subdirectory(Mathematics)
add_subdirectory(Control)
add_subdirectory(MediaLayer)
add_subdirectory(RenderingEngine)

# Main shared lib
#add_library(AlphaEngine SHARED Control/Dummy.cpp)
add_executable(AlphaEngine Control/Dummy.cpp)

# Add Dependencies
add_dependencies(AlphaEngine Mathematics)
add_dependencies(AlphaEngine Control)
add_dependencies(AlphaEngine MediaLayer)
add_dependencies(AlphaEngine RenderingEngine)

# Link Libraries
target_link_libraries(AlphaEngine ${LIBRARY_OUTPUT_PATH}libMathematics.a)
target_link_libraries(AlphaEngine ${LIBRARY_OUTPUT_PATH}libControl.a)
target_link_libraries(AlphaEngine ${LIBRARY_OUTPUT_PATH}libMediaLayer.a)
target_link_libraries(AlphaEngine ${LIBRARY_OUTPUT_PATH}libRenderingEngine.a)
target_link_libraries(AlphaEngine ${LIBRARY_OUTPUT_PATH}libImageLoading.a)
target_link_libraries(AlphaEngine ${LIBRARY_OUTPUT_PATH}libpng.a)
target_link_libraries(AlphaEngine ${LIBRARY_OUTPUT_PATH}libjpeg.a)
target_link_libraries(AlphaEngine ${LIBRARY_OUTPUT_PATH}zlib.a)
target_link_libraries(AlphaEngine ${LIBRARY_OUTPUT_PATH}libOpenGL.a)

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
