target_sources(AlphaEngine PRIVATE
    camera_module.cpp
    exit_module.cpp
    frame_module.cpp
    shadow_demo_module.cpp
)

# The FPS counter used to live here as a label-rendering game module; it
# now ships as a built-in ImGui debug panel (rendering_engine/debug_ui),
# so the standalone fps_overlay_module was removed.

# Only one scene-owning demo module is compiled at a time because each
# places its showcase geometry at the origin. shadow_demo_module.cpp is the
# active one: a field of PBR spheres and tall pillars spread over a wide
# ground plane under a slowly orbiting shadow-casting sun, showcasing the
# auto-fit directional shadow frustum from issue #146 (crisp shadows across
# the whole field, not just near the origin).
#
# fog_demo_module.cpp (Blinn-Phong spheres receding into linear distance
# fog, issue #122), instanced_demo_module.cpp (a grid_side^3 lattice of
# cubes in one instanced draw call), scene_graph_demo_module.cpp
# (parent/child node hierarchy with orbiting cube mesh_components and a
# point light), line_demo_module.cpp (coloured helix line strip),
# points_demo_module.cpp (coloured Fibonacci-sphere point cloud),
# skybox_demo_module.cpp (procedural sky + IBL sphere grid) and
# phong_demo_module.cpp (Blinn-Phong lit sphere) are the other showcases;
# swap any one filename in for shadow_demo_module.cpp to bring it back.

add_subdirectory(api)
