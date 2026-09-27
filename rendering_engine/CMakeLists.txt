# SPDX-License-Identifier: MIT
# Copyright (c) 2015-2026 Tomislav Radanovic

target_sources(AlphaEngine PRIVATE
    graphics_settings.cpp
    graphics_settings.hpp
    gpu_profiler.cpp
    gpu_profiler.hpp
    material_library.cpp
    material_library.hpp
    post_process_settings.cpp
    post_process_settings.hpp
    post_settings.hpp
    render_stats.hpp
    render_world.cpp
    render_world.hpp
    renderer.cpp
    renderer.hpp
    settings_registration.cpp
    settings_registration.hpp
)

add_subdirectory(camera)
add_subdirectory(debug_draw)
add_subdirectory(gpu)
add_subdirectory(lighting)
add_subdirectory(materials)
add_subdirectory(passes)
add_subdirectory(renderables)
add_subdirectory(resources)
