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

# Set top level directory to be searched for includes
include_directories(${CMAKE_SOURCE_DIR})

# Include subprojects
add_subdirectory(Mathematics)
add_subdirectory(Control)
add_subdirectory(MediaLayer)
add_subdirectory(RenderingEngine)

# Main shared lib
add_library(AlphaEngine SHARED Control/Dummy.cpp)

# Add Dependencies
add_dependencies(AlphaEngine Mathematics)
add_dependencies(AlphaEngine Control)
add_dependencies(AlphaEngine MediaLayer)
add_dependencies(AlphaEngine RenderingEngine)

# Link Libraries
target_link_libraries(AlphaEngine ${LIBRARY_OUTPUT_PATH}/libMathematics.a)
target_link_libraries(AlphaEngine ${LIBRARY_OUTPUT_PATH}/libControl.a)
target_link_libraries(AlphaEngine ${LIBRARY_OUTPUT_PATH}/libMediaLayer.a)
target_link_libraries(AlphaEngine ${LIBRARY_OUTPUT_PATH}/libRenderingEngine.a)
target_link_libraries(AlphaEngine ${LIBRARY_OUTPUT_PATH}/libImageLoading.a)
target_link_libraries(AlphaEngine ${LIBRARY_OUTPUT_PATH}/libpng.a)
target_link_libraries(AlphaEngine ${LIBRARY_OUTPUT_PATH}/libjpeg.a)
target_link_libraries(AlphaEngine ${LIBRARY_OUTPUT_PATH}/zlib.a)
target_link_libraries(AlphaEngine ${LIBRARY_OUTPUT_PATH}/libOpenGL.a)
target_link_libraries(AlphaEngine ${LIBRARY_OUTPUT_PATH}/libgl3w.a)

find_package(OpenGL REQUIRED)
target_link_libraries(AlphaEngine ${OPENGL_LIBRARIES})
