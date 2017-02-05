# This file defines AlphaEngine build system

cmake_minimum_required(VERSION 3.6)
project(AlphaEngine)

# Use C++11 standard
set(CMAKE_CXX_STANDARD 11)

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
include(Mathematics/CMakeLists.txt)
include(Control/CMakeLists.txt)
include(MediaLayer/CMakeLists.txt)
include(RenderingEngine/CMakeLists.txt)

# Main shared lib
add_library(AlphaEngine SHARED Control/Dummy.cpp)

# Add Dependencies
add_dependencies(AlphaEngine ${MATH_LIB_NAME})
target_link_libraries(AlphaEngine ${LIBRARY_OUTPUT_PATH}/lib${MATH_LIB_NAME}.a)

add_dependencies(AlphaEngine ${CONTROL_LIB_NAME})
target_link_libraries(AlphaEngine ${LIBRARY_OUTPUT_PATH}/lib${CONTROL_LIB_NAME}.a)

add_dependencies(AlphaEngine ${MEDIALAYER_LIB_NAME})
target_link_libraries(AlphaEngine ${LIBRARY_OUTPUT_PATH}/lib${MEDIALAYER_LIB_NAME}.a)

add_dependencies(AlphaEngine ${RENDERINGENGINE_LIB_NAME})
target_link_libraries(AlphaEngine ${LIBRARY_OUTPUT_PATH}/lib${RENDERINGENGINE_LIB_NAME}.a)
