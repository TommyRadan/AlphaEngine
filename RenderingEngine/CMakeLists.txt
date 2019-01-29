#
# Copyright (c) 2015-2019 Tomislav Radanovic
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

cmake_minimum_required(VERSION 3.6)
project(RenderingEngine CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_VERBOSE_MAKEFILE false)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(VENDOR_ROOT ${CMAKE_SOURCE_DIR}/Vendor/)

# Define target sources
file(GLOB RENDERINGENGINE_SOURCE_FILES
        "*.hpp" "*.cpp"
        "Mesh/*.hpp" "Mesh/*.cpp"
        "Renderables/*.hpp" "Renderables/*.cpp"
        "Renderers/*.hpp" "Renderers/*.cpp"
        "OpenGL/*.hpp" "OpenGL/*.cpp")

# External dependencies
include_directories(${VENDOR_ROOT}SDL2/include)
include_directories(${VENDOR_ROOT}GLM/)
include_directories(${VENDOR_ROOT}Loguru/)
include_directories(${VENDOR_ROOT}GLEW/include)

# Main lib
add_library(RenderingEngine STATIC ${RENDERINGENGINE_SOURCE_FILES})
set_target_properties(RenderingEngine PROPERTIES LINKER_LANGUAGE CXX)
