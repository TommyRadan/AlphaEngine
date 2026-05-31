target_sources(AlphaEngine PRIVATE
    camera_module.cpp
    exit_module.cpp
    fps_overlay_module.cpp
    frame_module.cpp
    skybox_demo_module.cpp
)

# phong_demo_module.cpp is the previous scene showcase; it places a lit
# sphere at the origin, so it is left out of the build while
# skybox_demo_module owns the origin sphere. Swap the two filenames to
# bring the Blinn-Phong demo back.

add_subdirectory(api)
