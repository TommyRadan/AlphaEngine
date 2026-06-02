target_sources(AlphaEngine PRIVATE
    camera_module.cpp
    exit_module.cpp
    frame_module.cpp
    scene_graph_demo_module.cpp
)

# The FPS counter used to live here as a label-rendering game module; it
# now ships as a built-in ImGui debug panel (rendering_engine/debug_ui),
# so the standalone fps_overlay_module was removed.

# Only one scene-owning demo module is compiled at a time because each
# places its showcase geometry at the origin. scene_graph_demo_module.cpp
# is the active one: it builds a parent/child node hierarchy (a spinning
# pivot with orbiting cube mesh_components, a second-level moon, and a
# point light via light_component) to showcase the entity/component scene
# graph and world-matrix propagation.
#
# line_demo_module.cpp (coloured helix line strip), points_demo_module.cpp
# (coloured Fibonacci-sphere point cloud), skybox_demo_module.cpp
# (procedural sky + IBL sphere grid), phong_demo_module.cpp (Blinn-Phong
# lit sphere) and instanced_demo_module.cpp (a wall of cubes drawn in one
# instanced draw call) are the other showcases; swap any one filename in for
# scene_graph_demo_module.cpp to bring it back.

add_subdirectory(api)
