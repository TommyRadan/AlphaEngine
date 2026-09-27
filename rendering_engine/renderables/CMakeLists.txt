# SPDX-License-Identifier: MIT
# Copyright (c) 2015-2026 Tomislav Radanovic

target_sources(AlphaEngine PRIVATE
    draw_item.hpp
    instanced_mesh.cpp
    instanced_mesh.hpp
    line.cpp
    line.hpp
    mesh_bounds.cpp
    mesh_bounds.hpp
    model.cpp
    model.hpp
    per_draw_ring.cpp
    per_draw_ring.hpp
    per_draw_ubo.cpp
    per_draw_ubo.hpp
    points.cpp
    points.hpp
    renderable.hpp
    vertex_format_check.cpp
    vertex_format_check.hpp
)

add_subdirectory(premade_2d)
add_subdirectory(premade_3d)
