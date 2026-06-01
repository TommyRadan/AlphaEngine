target_sources(AlphaEngine PRIVATE
    camera_module.cpp
    exit_module.cpp
    fps_overlay_module.cpp
    frame_module.cpp
    points_demo_module.cpp
)

# Only one scene-owning demo module is compiled at a time because each
# places its showcase geometry at the origin. points_demo_module.cpp is
# the active one: it showcases points_material + the points renderable as
# a coloured Fibonacci-sphere point cloud.
#
# skybox_demo_module.cpp (procedural sky + IBL sphere grid) and
# phong_demo_module.cpp (Blinn-Phong lit sphere) are the other showcases;
# swap either filename in for points_demo_module.cpp to bring it back.

add_subdirectory(api)
