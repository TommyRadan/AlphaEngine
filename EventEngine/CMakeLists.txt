cmake_minimum_required(VERSION 3.6)
project(EventEngine CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_VERBOSE_MAKEFILE false)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Define target sources
file(GLOB EVENTPROCESSING_SOURCE_FILES "*.hpp" "*.cpp")

# External dependencies
find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIR})

# Main lib
add_library(EventEngine STATIC ${EVENTPROCESSING_SOURCE_FILES})
set_target_properties(EventEngine PROPERTIES LINKER_LANGUAGE CXX)
