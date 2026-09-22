target_sources(AlphaEngine PRIVATE
    draw_item.hpp
    instanced_mesh.cpp
    instanced_mesh.hpp
    line.cpp
    line.hpp
    mesh_bounds.cpp
    mesh_bounds.hpp
    model.cpp
    model.hpp
    points.cpp
    points.hpp
    renderable.hpp
    vertex_format_check.cpp
    vertex_format_check.hpp
)

add_subdirectory(premade_2d)
add_subdirectory(premade_3d)
