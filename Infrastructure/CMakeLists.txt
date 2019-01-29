cmake_minimum_required(VERSION 3.6)
project(Infrastructure CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_VERBOSE_MAKEFILE false)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(VENDOR_ROOT ${CMAKE_SOURCE_DIR}/Vendor/)

# Define target sources
file(GLOB INFRASTRUCTURE_SOURCE_FILES "*.hpp" "*.cpp")

# External dependencies
include_directories(${VENDOR_ROOT}SDL2/include)
include_directories(${VENDOR_ROOT}STBI/)
include_directories(${VENDOR_ROOT}GLM/)
include_directories(${VENDOR_ROOT}Loguru/)

# Main lib
add_library(Infrastructure STATIC ${INFRASTRUCTURE_SOURCE_FILES})
set_target_properties(Infrastructure PROPERTIES LINKER_LANGUAGE CXX)
