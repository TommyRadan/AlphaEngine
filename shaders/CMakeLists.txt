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

# The engine's GLSL sources. Every file listed here is embedded into the
# shader_registry static library (see cmake/embed_shaders.cmake) and
# served by rendering_engine/gpu/shader_library by its path relative to
# this directory; that path is also what a shader's #include resolves.
# Add a new shader by listing it here - the list is explicit, no GLOB.
#
# include/bindings.glsl is not a checked-in file: it is generated from
# rendering_engine/gpu/shader_bindings.hpp so the C++ binding table is
# the single owner of the numbers (cmake/generate_shader_bindings.cmake).

set(ALPHAENGINE_SHADER_SOURCES
    include/brdf.glsl
    include/constants.glsl
    include/depth_utils.glsl
    include/fog.glsl
    include/ibl_common.glsl
    include/lights.glsl
    include/per_draw.glsl
    include/per_frame.glsl
    include/shadows.glsl
    materials/basic.frag.glsl
    materials/basic.vert.glsl
    materials/grid.frag.glsl
    materials/grid.vert.glsl
    materials/instanced.frag.glsl
    materials/instanced.vert.glsl
    materials/line.frag.glsl
    materials/line.vert.glsl
    materials/phong.frag.glsl
    materials/phong.vert.glsl
    materials/points.frag.glsl
    materials/points.vert.glsl
    materials/standard.frag.glsl
    materials/standard.vert.glsl
    materials/ui.frag.glsl
    materials/ui.vert.glsl
    passes/bloom_blur.frag.glsl
    passes/bloom_composite.frag.glsl
    passes/bloom_threshold.frag.glsl
    passes/fullscreen.vert.glsl
    passes/fxaa.frag.glsl
    passes/ibl_brdf_lut.comp.glsl
    passes/ibl_irradiance.comp.glsl
    passes/ibl_prefilter.comp.glsl
    passes/shadow.frag.glsl
    passes/shadow.vert.glsl
    passes/skybox.frag.glsl
    passes/skybox.vert.glsl
    passes/taa_copy.frag.glsl
    passes/taa_resolve.frag.glsl
    passes/tonemap.frag.glsl
    passes/velocity.frag.glsl
)

set(ALPHAENGINE_SHADER_GENERATED_DIR ${CMAKE_BINARY_DIR}/generated/shaders)
set(ALPHAENGINE_SHADER_BINDINGS_HEADER ${CMAKE_SOURCE_DIR}/rendering_engine/gpu/shader_bindings.hpp)
set(ALPHAENGINE_SHADER_BINDINGS_GLSL ${ALPHAENGINE_SHADER_GENERATED_DIR}/include/bindings.glsl)
set(ALPHAENGINE_SHADER_REGISTRY_SOURCE ${ALPHAENGINE_SHADER_GENERATED_DIR}/shader_registry.cpp)

# 1. bindings.glsl from the C++ binding table.
add_custom_command(
    OUTPUT ${ALPHAENGINE_SHADER_BINDINGS_GLSL}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${ALPHAENGINE_SHADER_GENERATED_DIR}/include
    COMMAND ${CMAKE_COMMAND}
        -DINPUT=${ALPHAENGINE_SHADER_BINDINGS_HEADER}
        -DOUTPUT=${ALPHAENGINE_SHADER_BINDINGS_GLSL}
        -P ${CMAKE_SOURCE_DIR}/cmake/generate_shader_bindings.cmake
    DEPENDS
        ${ALPHAENGINE_SHADER_BINDINGS_HEADER}
        ${CMAKE_SOURCE_DIR}/cmake/generate_shader_bindings.cmake
    COMMENT "Generating shaders/include/bindings.glsl from shader_bindings.hpp"
    VERBATIM
)

# 2. The registry: every listed file plus the generated bindings.glsl,
#    keyed by its path relative to shaders/.
set(shader_keys include/bindings.glsl)
set(shader_files ${ALPHAENGINE_SHADER_BINDINGS_GLSL})
foreach(shader IN LISTS ALPHAENGINE_SHADER_SOURCES)
    list(APPEND shader_keys ${shader})
    list(APPEND shader_files ${CMAKE_CURRENT_SOURCE_DIR}/${shader})
endforeach()

add_custom_command(
    OUTPUT ${ALPHAENGINE_SHADER_REGISTRY_SOURCE}
    COMMAND ${CMAKE_COMMAND}
        "-DSHADER_KEYS=${shader_keys}"
        "-DSHADER_FILES=${shader_files}"
        -DOUTPUT=${ALPHAENGINE_SHADER_REGISTRY_SOURCE}
        -P ${CMAKE_SOURCE_DIR}/cmake/embed_shaders.cmake
    DEPENDS
        ${shader_files}
        ${CMAKE_SOURCE_DIR}/cmake/embed_shaders.cmake
    COMMENT "Embedding shaders/ into shader_registry.cpp"
    VERBATIM
)

# A tiny static library rather than a source of the AlphaEngine target:
# a generated source can only be consumed by targets in the directory
# that declares its custom command, and both the engine and the unit
# tests need the registry. Its only API is the plain-C-shaped
# embedded_shaders(), so it carries no standard-library types across
# the link.
add_library(shader_registry STATIC ${ALPHAENGINE_SHADER_REGISTRY_SOURCE})
target_include_directories(shader_registry PRIVATE ${CMAKE_SOURCE_DIR})
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    target_compile_options(shader_registry PRIVATE -stdlib=libc++)
endif()

# The IDE-facing list of shader files, so they show up next to the
# sources without being compiled.
target_sources(shader_registry PRIVATE ${shader_files})
set_source_files_properties(${shader_files} PROPERTIES HEADER_FILE_ONLY ON)
