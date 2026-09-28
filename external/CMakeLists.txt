# SPDX-License-Identifier: MIT
# Copyright (c) 2015-2026 Tomislav Radanovic

# The game modules. Each registers itself from a static initialiser
# (GAME_MODULE), so each is compiled straight into a game executable and
# never into a static library (see the root CMakeLists.txt).
#
# Every game shares the player's fly camera and the Escape-to-quit module:
# both are compiled into alpha_game with the entry point and the
# game-module API.
target_sources(alpha_game PRIVATE
    camera_module.cpp
    exit_module.cpp
)

# AlphaEngine, the default game: shadow_demo_module.cpp, a field of PBR
# spheres and tall pillars spread over a wide ground plane under a slowly
# orbiting shadow-casting sun, showcasing the auto-fit directional shadow
# frustum (crisp shadows across the whole field, not just near the
# origin); physics_demo_module.cpp, a few crates dropped onto a pedestal
# that stands in a gap of that field, on its ground plane; and
# ui_demo_module.cpp, a quit button in the bottom-right corner.
target_sources(AlphaEngine PRIVATE
    physics_demo_module.cpp
    shadow_demo_module.cpp
    ui_demo_module.cpp
)

# The other showcases each place their geometry at the origin, so each is
# an executable of its own, alpha_demo_<name> (next to AlphaEngine in
# Binaries/), with the camera and exit modules:
#
#   fog          Blinn-Phong spheres receding into linear distance fog
#   gltf         the .gltf / .glb named by the ALPHAENGINE_GLTF environment
#                variable, loaded through the glTF importer and framed
#   instanced    a lattice of cubes in one instanced draw call
#   line         a coloured helix line strip
#   phong        a Blinn-Phong lit sphere
#   points       a coloured Fibonacci-sphere point cloud
#   post_tint    a lit sphere seen through a colour-tint post pass the
#                module registers before tonemap, with bloom switched off
#                through the renderer's pass API
#   scene_graph  a parent / child node hierarchy of orbiting spheres
#                around a point light
#   skybox       a procedural sky and an IBL-lit sphere grid
#
# They are left out of the default build, which builds AlphaEngine alone;
# the alpha_demos target builds them, and CI builds it on every build job.
set(ALPHAENGINE_DEMOS fog gltf instanced line phong points post_tint scene_graph skybox)

add_custom_target(alpha_demos)
foreach(demo IN LISTS ALPHAENGINE_DEMOS)
    add_executable(alpha_demo_${demo} EXCLUDE_FROM_ALL ${demo}_demo_module.cpp)
    target_link_libraries(alpha_demo_${demo} PRIVATE alpha_game)
    alpha_target_settings(alpha_demo_${demo})
    add_dependencies(alpha_demos alpha_demo_${demo})
endforeach()

add_subdirectory(api)
