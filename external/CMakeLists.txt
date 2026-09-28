# SPDX-License-Identifier: MIT
# Copyright (c) 2015-2026 Tomislav Radanovic

# The game modules. Each registers itself from a static initialiser
# (GAME_MODULE), so each is compiled straight into the executable and never
# into a static library (see the root CMakeLists.txt). AlphaEngine links
# every one of them, and the project it runs (projects/) names the modules
# to install, so choosing a game or a showcase needs no build change:
#
#   camera        the player's fly camera, in every project
#   exit          quits on Escape, in every project
#   shadow_demo   a field of PBR spheres and tall pillars on a wide ground
#                 plane under a slowly orbiting shadow-casting sun,
#                 showcasing the auto-fit directional shadow frustum
#   physics_demo  a few crates dropped onto a pedestal that stands in a gap
#                 of the shadow demo's field, on its ground plane
#   ui_demo       a quit button in the bottom-right corner
#   fog_demo      Blinn-Phong spheres receding into linear distance fog
#   gltf_demo     the .gltf / .glb named by the ALPHAENGINE_GLTF environment
#                 variable, loaded through the glTF importer and framed
#   instanced_demo  a lattice of cubes in one instanced draw call
#   line_demo     a coloured helix line strip
#   phong_demo    a Blinn-Phong lit sphere
#   points_demo   a coloured Fibonacci-sphere point cloud
#   post_tint_demo  a lit sphere seen through a colour-tint post pass the
#                 module registers before tonemap, with bloom switched off
#                 through the renderer's pass API
#   scene_graph_demo  a parent / child node hierarchy of orbiting spheres
#                 around a point light
#   skybox_demo   a procedural sky and an IBL-lit sphere grid
target_sources(alpha_game PRIVATE
    camera_module.cpp
    exit_module.cpp
    fog_demo_module.cpp
    gltf_demo_module.cpp
    instanced_demo_module.cpp
    line_demo_module.cpp
    phong_demo_module.cpp
    physics_demo_module.cpp
    points_demo_module.cpp
    post_tint_demo_module.cpp
    scene_graph_demo_module.cpp
    shadow_demo_module.cpp
    skybox_demo_module.cpp
    ui_demo_module.cpp
)

# What the demo projects run on: AlphaEngine, which links every demo module.
add_custom_target(alpha_demos)
add_dependencies(alpha_demos AlphaEngine)

add_subdirectory(api)
