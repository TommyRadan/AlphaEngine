# SPDX-License-Identifier: MIT
# Copyright (c) 2015-2026 Tomislav Radanovic

target_sources(AlphaEngine PRIVATE
    gpu_profiler.cpp
    gpu_profiler.hpp
    material_library.cpp
    material_library.hpp
    post_settings.hpp
    render_stats.hpp
    render_world.cpp
    render_world.hpp
    renderer.cpp
    renderer.hpp
)

add_subdirectory(assets)
add_subdirectory(camera)
add_subdirectory(editor)
add_subdirectory(gpu)
add_subdirectory(lighting)
add_subdirectory(materials)
add_subdirectory(passes)
add_subdirectory(renderables)
