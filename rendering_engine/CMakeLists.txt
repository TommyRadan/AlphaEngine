# SPDX-License-Identifier: MIT
# Copyright (c) 2015-2026 Tomislav Radanovic

target_sources(alpha_render PRIVATE
    fog.hpp
    graphics_settings.cpp
    graphics_settings.hpp
    gpu_profiler.cpp
    gpu_profiler.hpp
    material_library.cpp
    material_library.hpp
    mesh_draws.cpp
    mesh_draws.hpp
    mesh_proxy.hpp
    post_process_settings.cpp
    post_process_settings.hpp
    post_settings.hpp
    render_proxies.hpp
    render_services.hpp
    render_stats.hpp
    render_texture.cpp
    render_texture.hpp
    render_world.cpp
    render_world.hpp
    renderer.cpp
    renderer.hpp
    settings_registration.cpp
    settings_registration.hpp
    ui_draws.cpp
    ui_draws.hpp
    ui_proxy.hpp
    view.hpp
)

add_subdirectory(camera)
add_subdirectory(debug_draw)
add_subdirectory(gpu)
add_subdirectory(lighting)
add_subdirectory(materials)
add_subdirectory(passes)
add_subdirectory(renderables)
add_subdirectory(resources)
