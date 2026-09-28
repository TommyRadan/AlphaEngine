# SPDX-License-Identifier: MIT
# Copyright (c) 2015-2026 Tomislav Radanovic

target_sources(alpha_world PRIVATE
    lua_behavior.cpp
    lua_behavior.hpp
    lua_state.hpp
    math_bindings.cpp
    script_host.cpp
    script_host.hpp
    world_bindings.cpp
)
