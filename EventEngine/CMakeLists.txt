cmake_minimum_required(VERSION 3.6)
project(EventEngine CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_VERBOSE_MAKEFILE false)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(VENDOR_ROOT ${CMAKE_SOURCE_DIR}/Vendor/)

# Define target sources
file(GLOB EVENTPROCESSING_SOURCE_FILES "*.hpp" "*.cpp")

# External dependencies
include_directories(${VENDOR_ROOT}SDL2/include)

# Main lib
add_library(EventEngine STATIC ${EVENTPROCESSING_SOURCE_FILES})
set_target_properties(EventEngine PROPERTIES LINKER_LANGUAGE CXX)
