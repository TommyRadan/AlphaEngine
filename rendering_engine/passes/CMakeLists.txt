# SPDX-License-Identifier: MIT
# Copyright (c) 2015-2026 Tomislav Radanovic

target_sources(alpha_render PRIVATE
    depth_prepass.cpp
    depth_prepass.hpp
    frame_resources.hpp
    pass.hpp
    pass_list.cpp
    pass_list.hpp
    point_shadow_pass.cpp
    point_shadow_pass.hpp
    projection_jitter.cpp
    projection_jitter.hpp
    resource_store.cpp
    resource_store.hpp
    scene_pass.cpp
    scene_pass.hpp
    shadow_casters.cpp
    shadow_casters.hpp
    shadow_pass.cpp
    shadow_pass.hpp
    shadow_settings.hpp
    skybox_pass.cpp
    skybox_pass.hpp
    spot_shadow_pass.cpp
    spot_shadow_pass.hpp
    ui_pass.cpp
    ui_pass.hpp
    view_globals.cpp
    view_globals.hpp
)

add_subdirectory(post)
