target_sources(AlphaEngine PRIVATE
    component.hpp
    engine.cpp
    engine.hpp
    main_loop.cpp
    node.cpp
    node.hpp
    scene_graph.cpp
    scene_graph.hpp
)

add_subdirectory(components)
