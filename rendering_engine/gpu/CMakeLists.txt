# SPDX-License-Identifier: MIT
# Copyright (c) 2015-2026 Tomislav Radanovic

target_sources(AlphaEngine PRIVATE
    bind_group.hpp
    buffer.hpp
    command_encoder.cpp
    command_encoder.hpp
    device.cpp
    device.hpp
    factory.cpp
    handle.hpp
    overlay_renderer.cpp
    overlay_renderer.hpp
    pipeline.hpp
    render_target.hpp
    shader.hpp
    shader_bindings.hpp
    shader_compiler.cpp
    shader_compiler.hpp
    shader_hot_reload.cpp
    shader_hot_reload.hpp
    shader_library.cpp
    shader_library.hpp
    surface.hpp
    texture.hpp
    types.hpp
)

add_subdirectory(backend)
