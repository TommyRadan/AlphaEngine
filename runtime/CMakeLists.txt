# SPDX-License-Identifier: MIT
# Copyright (c) 2015-2026 Tomislav Radanovic

target_sources(alpha_world PRIVATE
    behavior.hpp
    component.hpp
    engine.cpp
    engine.hpp
    engine_settings.cpp
    engine_settings.hpp
    game_module.cpp
    game_module.hpp
    gltf_instantiate.cpp
    gltf_instantiate.hpp
    node.cpp
    node.hpp
    overlay.hpp
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

# The entry point, shared by every game executable (see external/).
target_sources(alpha_game PRIVATE
    main_loop.cpp
)

add_subdirectory(animation)
add_subdirectory(components)
add_subdirectory(physics)
add_subdirectory(scripting)
