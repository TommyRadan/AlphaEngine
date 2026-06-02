#
# Copyright (c) 2015-2025 Tomislav Radanovic
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
#

# This file defines AlphaEngine build system

cmake_minimum_required(VERSION 3.16)
project(AlphaEngine C CXX)

# Set build type to Debug if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# Add debug definitions and flags. Use generator expressions so this works
# correctly with multi-config generators (e.g. Visual Studio) where
# CMAKE_BUILD_TYPE is empty at configure time.
add_compile_definitions($<$<CONFIG:Debug>:_DEBUG>)
if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    add_compile_options($<$<CONFIG:Debug>:/Zi> $<$<CONFIG:Debug>:/Od>)
else()
    add_compile_options($<$<CONFIG:Debug>:-g> $<$<CONFIG:Debug>:-O0>)
endif()

# Set cmake module directory
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# Version
add_definitions(-DVERSION_MAJOR=0)
add_definitions(-DVERSION_MINOR=0)
add_definitions(-DVERSION_PATCH=1)

# Set the macOS SDK path
if(APPLE)
    execute_process(COMMAND 
        xcrun --show-sdk-path OUTPUT_VARIABLE MACOS_SDK_PATH 
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    set(CMAKE_OSX_SYSROOT ${MACOS_SDK_PATH})
endif(APPLE)

# Compile options
set(LIBRARY_OUTPUT_PATH ${CMAKE_SOURCE_DIR}/Binaries/)
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_SOURCE_DIR}/Binaries/)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_VERBOSE_MAKEFILE false)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Vendor options
set(VENDOR_ROOT ${CMAKE_SOURCE_DIR}/vendor/)

# Make output directory if it doesn't exist
add_custom_target(build-time-make-directory ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory Binaries)

# Set top level directory to be searched for includes
include_directories(${CMAKE_SOURCE_DIR})

# Main executable. Sources are contributed by per-directory CMakeLists.txt
# files via target_sources(). The initial source is supplied here so the
# target is well-formed before subdirectories extend it.
add_executable(AlphaEngine runtime/main_loop.cpp)

add_subdirectory(runtime)
add_subdirectory(core)
add_subdirectory(rendering_engine)
add_subdirectory(external)

# Threads — the core worker pool (core/jobs) uses std::thread. MSVC links its
# threading support implicitly; on Linux this pulls in pthread.
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(AlphaEngine Threads::Threads)

# Use C++ standard library
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    target_compile_options(AlphaEngine PRIVATE -stdlib=libc++)
    target_link_options(AlphaEngine PRIVATE -stdlib=libc++)
endif()

# SDL3 — fetched and built statically from source
include(FetchContent)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.2.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(SDL3)
target_link_libraries(AlphaEngine SDL3::SDL3-static)

# GLM — header-only, fetched from upstream and pinned to 1.0.1
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glm)
target_link_libraries(AlphaEngine glm::glm)

# glslang — fetched and built statically. Compiles GLSL to SPIR-V at
# runtime so both the OpenGL backend (via ARB_gl_spirv) and a future
# Vulkan backend can consume the same byte blobs. ENABLE_OPT=OFF
# drops the SPIRV-Tools optimizer dependency; the engine ships
# unoptimised SPIR-V for now.
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(ENABLE_RTTI OFF CACHE BOOL "" FORCE)
set(ENABLE_CTEST OFF CACHE BOOL "" FORCE)
set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    glslang
    GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
    GIT_TAG 15.0.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glslang)
target_link_libraries(AlphaEngine
    glslang::glslang
    glslang::SPIRV
    glslang::glslang-default-resource-limits)

target_include_directories(AlphaEngine PRIVATE ${VENDOR_ROOT}stb/)

# glad — OpenGL loader (generated for GL 4.6 Core, no extensions)
add_library(glad STATIC ${VENDOR_ROOT}glad/src/gl.c)
target_include_directories(glad PUBLIC ${VENDOR_ROOT}glad/include)
target_link_libraries(AlphaEngine glad)

find_package(OpenGL REQUIRED)
target_link_libraries(AlphaEngine ${OPENGL_LIBRARIES})

# Vulkan — always linked. Both the OpenGL and Vulkan backends are
# compiled into the binary; the runtime choice is driven by
# settings::get_graphics_backend(). The Vulkan backend at
# rendering_engine/gpu/backend/vulkan/ links against the loader from
# the system Vulkan SDK; the engine already pulls glslang in for the
# OpenGL ARB_gl_spirv path, so the Vulkan backend reuses that for its
# shader compile.
find_package(Vulkan REQUIRED)
target_link_libraries(AlphaEngine Vulkan::Vulkan)

# Dear ImGui — fetched from source and built as a small static library
# from the same explicit-file pattern the rest of the tree uses. ImGui
# ships no CMakeLists, so FetchContent only populates the sources and we
# compile the core plus the SDL3 platform backend and the OpenGL3
# renderer backend ourselves. The docking branch is pinned by tag so the
# debug tooling can dock and tear off panels.
#
# ImGui is debug-only: it is linked into AlphaEngine and the
# ALPHAENGINE_HAS_IMGUI macro is defined for Debug configurations only,
# via generator expressions so this also holds for multi-config
# generators (Visual Studio) where CMAKE_BUILD_TYPE is empty at configure
# time. Release builds never link ImGui and the debug-UI layer compiles
# down to empty no-ops.
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.8-docking
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(imgui)
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
# The SDL3 backend needs the SDL headers; the OpenGL3 backend ships its
# own function loader, so no GL link dependency is required there. The
# Vulkan backend links against the loader (default Vulkan prototypes).
target_link_libraries(imgui PUBLIC SDL3::SDL3-static Vulkan::Vulkan)
target_link_libraries(AlphaEngine $<$<CONFIG:Debug>:imgui>)
target_compile_definitions(AlphaEngine PRIVATE $<$<CONFIG:Debug>:ALPHAENGINE_HAS_IMGUI>)

# Post-build
if(APPLE)
    target_link_libraries(AlphaEngine "-framework CoreFoundation -framework IOKit -framework CoreVideo")
endif(APPLE)
