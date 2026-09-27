# SPDX-License-Identifier: MIT
# Copyright (c) 2015-2026 Tomislav Radanovic

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
    scene.cpp
    scene.hpp
    scene_assets.cpp
    scene_assets.hpp
    scene_manager.cpp
    scene_manager.hpp
    scene_serializer.cpp
    scene_serializer.hpp
)

add_subdirectory(animation)
add_subdirectory(components)
add_subdirectory(physics)
add_subdirectory(scripting)
