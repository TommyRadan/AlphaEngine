# SPDX-License-Identifier: MIT
# Copyright (c) 2015-2026 Tomislav Radanovic

target_sources(AlphaEngine PRIVATE
    camera_module.cpp
    exit_module.cpp
    physics_demo_module.cpp
    shadow_demo_module.cpp
    ui_demo_module.cpp
)

# Only one scene-owning demo module is compiled at a time because each
# places its showcase geometry at the origin. shadow_demo_module.cpp is the
# active one: a field of PBR spheres and tall pillars spread over a wide
# ground plane under a slowly orbiting shadow-casting sun, showcasing the
# auto-fit directional shadow frustum (crisp shadows across the whole
# field, not just near the origin). physics_demo_module.cpp is compiled
# beside it: a few crates dropped onto a pedestal that stands in a gap of
# that field, on its ground plane.
#
# fog_demo_module.cpp (Blinn-Phong spheres receding into linear distance
# fog), instanced_demo_module.cpp (a grid_side^3 lattice of
# cubes in one instanced draw call), scene_graph_demo_module.cpp
# (parent/child node hierarchy with orbiting cube mesh_components and a
# point light), line_demo_module.cpp (coloured helix line strip),
# points_demo_module.cpp (coloured Fibonacci-sphere point cloud),
# skybox_demo_module.cpp (procedural sky + IBL sphere grid),
# phong_demo_module.cpp (Blinn-Phong lit sphere) and gltf_demo_module.cpp
# (loads the .gltf / .glb named by the ALPHAENGINE_GLTF environment variable
# through the glTF importer and frames it) are the other showcases; swap any
# one filename in for shadow_demo_module.cpp to bring it back.

add_subdirectory(api)
