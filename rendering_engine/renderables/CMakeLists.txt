target_sources(AlphaEngine PRIVATE
    draw_item.hpp
    instanced_mesh.cpp
    instanced_mesh.hpp
    line.cpp
    line.hpp
    model.cpp
    model.hpp
    points.cpp
    points.hpp
    renderable.hpp
)

add_subdirectory(premade_2d)
add_subdirectory(premade_3d)
