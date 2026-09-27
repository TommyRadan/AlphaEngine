target_sources(AlphaEngine PRIVATE
    component.hpp
    engine.cpp
    engine.hpp
    gltf_instantiate.cpp
    gltf_instantiate.hpp
    main_loop.cpp
    node.cpp
    node.hpp
    scene_graph.cpp
    scene_graph.hpp
    scene_manager.cpp
    scene_manager.hpp
)

add_subdirectory(components)
