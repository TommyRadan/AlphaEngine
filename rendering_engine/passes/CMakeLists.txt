target_sources(AlphaEngine PRIVATE
    debug_pass.cpp
    debug_pass.hpp
    pass.hpp
    scene_pass.cpp
    scene_pass.hpp
    ui_pass.cpp
    ui_pass.hpp
)

add_subdirectory(post)
