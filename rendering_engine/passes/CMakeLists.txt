target_sources(AlphaEngine PRIVATE
    debug_pass.cpp
    debug_pass.hpp
    pass.hpp
    point_shadow_pass.cpp
    point_shadow_pass.hpp
    scene_pass.cpp
    scene_pass.hpp
    shadow_pass.cpp
    shadow_pass.hpp
    skybox_pass.cpp
    skybox_pass.hpp
    ui_pass.cpp
    ui_pass.hpp
)

add_subdirectory(post)
