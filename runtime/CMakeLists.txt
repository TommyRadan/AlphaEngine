target_sources(AlphaEngine PRIVATE
    behavior.hpp
    component.hpp
    engine.cpp
    engine.hpp
    game_module.cpp
    game_module.hpp
    gltf_instantiate.cpp
    gltf_instantiate.hpp
    main_loop.cpp
    node.cpp
    node.hpp
    reflection.cpp
    reflection.hpp
    scene_assets.cpp
    scene_assets.hpp
    scene_graph.cpp
    scene_graph.hpp
    scene_manager.cpp
    scene_manager.hpp
    scene_serializer.cpp
    scene_serializer.hpp
)

add_subdirectory(components)
add_subdirectory(physics)
