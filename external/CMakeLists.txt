target_sources(AlphaEngine PRIVATE
    camera_module.cpp
    exit_module.cpp
    frame_module.cpp
    line_demo_module.cpp
)

# The FPS counter used to live here as a label-rendering game module; it
# now ships as a built-in ImGui debug panel (rendering_engine/debug_ui),
# so the standalone fps_overlay_module was removed.

# Only one scene-owning demo module is compiled at a time because each
# places its showcase geometry at the origin. line_demo_module.cpp is
# the active one: it showcases line_material + the line renderable as a
# coloured helix line strip.
#
# points_demo_module.cpp (coloured Fibonacci-sphere point cloud),
# skybox_demo_module.cpp (procedural sky + IBL sphere grid) and
# phong_demo_module.cpp (Blinn-Phong lit sphere) are the other showcases;
# swap any one filename in for line_demo_module.cpp to bring it back.

add_subdirectory(api)
