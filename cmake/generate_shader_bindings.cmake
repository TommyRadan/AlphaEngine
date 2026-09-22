#
# Copyright (c) 2015-2026 Tomislav Radanovic
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

# Generates shaders/include/bindings.glsl from the C++ binding table in
# rendering_engine/gpu/shader_bindings.hpp, so the header is the single
# owner of the numbers both sides use.
#
# Every line of the form
#     constexpr uint32_t <name> = <number>;
# becomes
#     #define BINDING_<NAME> <number>
#
# Invoked as a script (cmake -P) by shaders/CMakeLists.txt with:
#   -DINPUT=<path to shader_bindings.hpp>
#   -DOUTPUT=<path to write bindings.glsl to>

if(NOT DEFINED INPUT OR NOT DEFINED OUTPUT)
    message(FATAL_ERROR "generate_shader_bindings.cmake needs -DINPUT=<header> -DOUTPUT=<glsl>")
endif()

file(STRINGS "${INPUT}" lines REGEX "^[ \t]*constexpr uint32_t [a-z0-9_]+ = [0-9]+;")

set(content "// Generated from rendering_engine/gpu/shader_bindings.hpp by\n")
string(APPEND content "// cmake/generate_shader_bindings.cmake. Do not edit; change the header.\n")
string(APPEND content "//\n")
string(APPEND content "// Binding numbers shared by every scene pipeline. They are unique across\n")
string(APPEND content "// all descriptor sets because the OpenGL backend flattens the sets into\n")
string(APPEND content "// one namespace per resource class (ARB_gl_spirv).\n")
string(APPEND content "#ifndef AE_BINDINGS_GLSL\n")
string(APPEND content "#define AE_BINDINGS_GLSL\n\n")

set(count 0)
foreach(line IN LISTS lines)
    string(REGEX MATCH "constexpr uint32_t ([a-z0-9_]+) = ([0-9]+);" _ "${line}")
    if(NOT CMAKE_MATCH_1)
        continue()
    endif()
    string(TOUPPER "${CMAKE_MATCH_1}" upper)
    string(APPEND content "#define BINDING_${upper} ${CMAKE_MATCH_2}\n")
    math(EXPR count "${count} + 1")
endforeach()

if(count EQUAL 0)
    message(FATAL_ERROR "generate_shader_bindings.cmake: no 'constexpr uint32_t name = N;' lines found in ${INPUT}")
endif()

string(APPEND content "\n#endif // AE_BINDINGS_GLSL\n")

# Only touch the output when it changes so the embed step (and everything
# that depends on the generated registry) is not rebuilt needlessly.
set(existing "")
if(EXISTS "${OUTPUT}")
    file(READ "${OUTPUT}" existing)
endif()
if(NOT existing STREQUAL content)
    file(WRITE "${OUTPUT}" "${content}")
endif()
