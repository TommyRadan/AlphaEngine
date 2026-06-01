target_sources(AlphaEngine PRIVATE
    draw_item.hpp
    model.cpp
    model.hpp
    points.cpp
    points.hpp
    renderable.hpp
)

add_subdirectory(premade_2d)
add_subdirectory(premade_3d)
