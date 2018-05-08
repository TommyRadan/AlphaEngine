cmake_minimum_required(VERSION 3.6)
project(Infrastructure CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_VERBOSE_MAKEFILE false)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Define target sources
file(GLOB INFRASTRUCTURE_SOURCE_FILES "*.hpp" "*.cpp")

# External dependencies
find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIR})

# Main lib
add_library(Infrastructure STATIC ${INFRASTRUCTURE_SOURCE_FILES})
set_target_properties(Infrastructure PROPERTIES LINKER_LANGUAGE CXX)
